/**
 * @file urts_calls.cpp
 * @author weichbr
 */

#include <sgx_attributes.h>
#include <sgx_error.h>
#include <sgx_urts.h>
#include <dlfcn.h>
#include <cstring>
#include <unistd.h>
#include <memory>
#include <sys/mman.h>
#include <fcntl.h>
#include <iostream>

#include "urts_calls.h"
#include "elfparser.h"
#include "store.h"
#include "config.h"
#include "events.h"

extern sgxperf::EventStore *event_store;
extern sgxperf::Config *config;

static sgx_status_t (*real_sgx_create_enclave)(const char *, const int, sgx_launch_token_t *, int *, sgx_enclave_id_t *, sgx_misc_attribute_t *) = nullptr;
static sgx_status_t (*real_sgx_destroy_enclave)(const sgx_enclave_id_t) = nullptr;
static sgx_status_t (*real_sgx_ecall)(const sgx_enclave_id_t, const int, struct ocall_table *, void *) = nullptr;
static int (*real_sgx_thread_wait_untrusted_event_ocall)(const void *self) = nullptr;
static int (*real_sgx_thread_set_untrusted_event_ocall)(const void *self) = nullptr;

static void patch_aep();

CEnclavePoolInstance cenclavepoolinstance = nullptr;
CEnclavePoolGetEvent cenclavepoolgetevent = nullptr;
CEnclavePoolGetEnclave cenclavepoolgetenclave = nullptr;

/**
 * @brief Initializes the intercepted urts calls
 */
int initialize_urts_calls()
{
	real_sgx_create_enclave = (sgx_status_t (*)(const char *, const int, sgx_launch_token_t *, int *, sgx_enclave_id_t *, sgx_misc_attribute_t *)) dlsym(RTLD_NEXT, "sgx_create_enclave");
	if (real_sgx_create_enclave == nullptr)
	{
		printf("!!! Error getting real sgx_create_enclave \n");
		return -1;
	}

	real_sgx_destroy_enclave = (sgx_status_t (*)(const sgx_enclave_id_t)) dlsym(RTLD_NEXT, "sgx_destroy_enclave");
	if (real_sgx_destroy_enclave == nullptr)
	{
		printf("!!! Error getting real sgx_destroy_enclave \n");
		return -1;
	}

	real_sgx_ecall = (sgx_status_t (*)(const sgx_enclave_id_t, const int, struct ocall_table *, void *)) dlsym(RTLD_NEXT, "sgx_ecall");
	if (real_sgx_ecall == nullptr)
	{
		printf("!!! Error getting real sgx_ecall \n");
		return -1;
	}

	real_sgx_thread_wait_untrusted_event_ocall = (int (*)(const void *self))dlsym(RTLD_NEXT, "sgx_thread_wait_untrusted_event_ocall");
	real_sgx_thread_set_untrusted_event_ocall = (int (*)(const void *self))dlsym(RTLD_NEXT, "sgx_thread_set_untrusted_event_ocall");
	
	if (real_sgx_thread_wait_untrusted_event_ocall == nullptr || real_sgx_thread_set_untrusted_event_ocall == nullptr)
	{
		printf("!!! Could not get untrusted thread synchronization ocalls \n");
		return -1;
	}

	// Patch AEP if we are in HW mode
	if (is_hw_mode() && config->is_aex_counting_enabled())
		patch_aep();

	// Find the CEnclavePool functions
	Dl_info dl_info = {};
	dladdr(reinterpret_cast<void *>(real_sgx_create_enclave), &dl_info);
	std::string bin(dl_info.dli_fname);
	std::string cenclpoolinst_sym("_ZN12CEnclavePool8instanceEv");
	std::string cenclpoolgetencl_sym("_ZN12CEnclavePool11get_enclaveEm");
	std::string cenclpoolgetevent_sym("_ZN12CEnclavePool9get_eventEPKv");

	// First, get the CEnclavePool instance
	auto cenclpoolinst_addr = get_address_for_symbol(bin, cenclpoolinst_sym);
	if (cenclpoolinst_addr == nullptr)
	{
		printf("!!! Could not get CEnclavePool::instance(). Check urts for stripped symbols\n");
		return -1;
	}
	cenclavepoolinstance = (CEnclavePoolInstance)((uint64_t)dl_info.dli_fbase + (uint64_t)cenclpoolinst_addr);

	auto cenclpoolgetevent_addr = get_address_for_symbol(bin, cenclpoolgetevent_sym);
	if (cenclpoolgetevent_addr == nullptr)
	{
		printf("!!! Could not get CEnclavePool::getEvent(). Check urts for stripped symbols\n");
		return -1;
	}
	cenclavepoolgetevent = (CEnclavePoolGetEvent)((uint64_t)dl_info.dli_fbase + (uint64_t)cenclpoolgetevent_addr);

	auto cenclpoolgetencl_addr = get_address_for_symbol(bin, cenclpoolgetencl_sym);
	if (cenclpoolgetencl_addr == nullptr)
	{
		printf("!!! Could not get CEnclavePool::getEnclave(). Check urts for stripped symbols\n");
		return -1;
	}
	cenclavepoolgetenclave = (CEnclavePoolGetEnclave)((uint64_t)dl_info.dli_fbase + (uint64_t)cenclpoolgetencl_addr);

	return 0;
}

/**
 * @brief Read from enclave memory, only works if enclave is a debug enclave.
 * @param addr Address to read from
 * @param buffer Buffer to read into
 * @param size Size of the buffer
 * @param read_nr Optional pointer, value set to how many bytes were read
 * @return true on success, false otherwise.
 */
bool read_from_enclave(void *addr, void *buffer, size_t size, size_t *read_nr = nullptr)
{
	static int fd = 0;
	char filename[64];
	ssize_t len = 0;

	if (fd == 0)
	{
		snprintf(filename, 64, "/proc/%d/mem", (int)getpid());
		fd = open(filename, O_RDONLY | O_LARGEFILE);
		if (fd == -1)
		{
			fd = 0;
			printf("/!\\ Error opening enclave memory file\n");
			return false;
		}
	}

	if(lseek64(fd, reinterpret_cast<__off64_t>(addr), SEEK_SET) == -1)
	{
		printf("/!\\ Error seeking to offset\n");
		goto out;
		//return false;
	}
	if((len = read(fd, buffer, size)) < 0)
	{
		printf("/!\\ Error reading enclave memory file\n");
		goto out;
		return false;
	}
	else if(read_nr != nullptr)
	{
		*read_nr = (size_t)len;
	}
	out:
	close(fd);
	fd = 0;
	return true;
}

// Disable optimizations
#pragma GCC push_options
#pragma GCC optimize ("O0")

extern "C" void __really_new_aep();

/**
 * @brief Our own AEP, used for counting AEX.
 * This is actually three symbols: __new_aep_prologue, __new_aep and __really_new_aep.
 * __new_aep is the actual AEP entry point whereas the other two designate the function prologue (which we skip) and the real new aep function.
 */
extern "C" void __attribute__((optimize("O0"))) __new_aep_prologue()
{
	// If rax is 3, then this was the AEX
	__asm__("__new_aep:\n"
	        "push %rax\n"
	        "push %rbx\n"
	        "push %rcx\n"
	        "cmpq $3, %rax\n"
	        "je __after_get_aep_check\n"
	        "lea -0x0d(%rip), %rax\n"
	        "retq\n"
	        "__after_get_aep_check:\n"
	        "call __aep_call_lbl\n"
	        "pop %rcx\n"
	        "pop %rbx\n"
	        "pop %rax\n"
	        "enclu\n"
	        "ud2");
}

/**
 * @brief Since we want to do some real stuff here, we need our own function that is called from __new_aep so we get access to the stack.
 * We need to do this, because the stackframe is still the one from __morestack which we must not touch.
 */
__asm__("__aep_call_lbl:");
extern "C" __attribute__((optimize("O0"))) void __really_new_aep()
{
	/*
	// Save TCS address so we can read out SSA
	tcs_t *tcs_addr = nullptr;
	__asm__("mov %%rbx, %0" : "=g"(tcs_addr) : : "rax", "rbx", "rcx");
	auto ssa_addr = (ssa_gpr_t *)(tcs_addr + 0x2);
	ssa_addr = (ssa_gpr_t *)((uint8_t *)ssa_addr - 184);
	auto exit_info = &ssa_addr->exit_info;

	uint8_t ei[sizeof(exit_info_t)];
	bool r = read_from_enclave(exit_info, ei, sizeof(ei));
	if (!r)
	{
		printf("/!\\ Error reading from enclave!\n");
	}

	exit_info = reinterpret_cast<exit_info_t *>(ei);

	// With SGXv2 we can find out if the AEX was caused by a pagefault.

	if (exit_info->vector != 0)
		printf("v: 0x%x; t: 0x%x, v: 0x%x\n", exit_info->vector, exit_info->exit_type, exit_info->valid);
	*/

	// Count the AEX
	auto t = event_store->get_thread();
	if (t)
	{
		auto ecall = dynamic_cast<sgxperf::EnclaveECallEvent *>(t->current_call);
		if (ecall == nullptr)
		{
			// Not an ecall event, should never happen
			std::cout << "/!\\ AEP hit while not in an ECall!" << std::endl;
			return;
		}
		ecall->aex_counter++;
		if (config->is_aex_tracing_enabled())
		{
			auto aexe = new sgxperf::EnclaveAEXEvent(ecall);
			event_store->insert_event(aexe);
		}
	}
}

#pragma GCC pop_options

/**
 * @brief Our own AEP entry point.
 */
extern "C" void __new_aep();

/**
 * @brief This method patches the AEP inside the urts with a jump to our AEP.
 */
static void patch_aep()
{
	// First, find the address of the urts
	Dl_info dl_info = {};
	dladdr(reinterpret_cast<void *>(real_sgx_create_enclave), &dl_info);

	// Now search for the enclu, the urts contains two, one for eenter and one for eexit
	auto first_enclu = static_cast<uint8_t *>(dl_info.dli_fbase);
	while(true)
	{
		if (first_enclu[0] == 0x0f && first_enclu[1] == 0x01 && first_enclu[2] == 0xd7)
		{
			break;
		}
		first_enclu++;
	}

	auto second_enclu = first_enclu + 3; // Skip the first enclu
	while(true)
	{
		if (second_enclu[0] == 0x0f && second_enclu[1] == 0x01 && second_enclu[2] == 0xd7)
		{
			break;
		}
		second_enclu++;
	}

	uint64_t diff2 = 0;
	uint32_t jmptarget = 0;

	// This is the offset for the jump because we don't patch the enclu directly but 4 bytes after the enclu (+5 bytes jump)
#define JMP_OFFSET (0x09)

	if (second_enclu > reinterpret_cast<void *>(__new_aep))
	{
		// new aep before urts
		diff2 = reinterpret_cast<uint64_t>(second_enclu) - reinterpret_cast<uint64_t>(__new_aep);
		jmptarget = static_cast<uint32_t>(~diff2 + 1);
		jmptarget += JMP_OFFSET;
	}
	else
	{
		// new aep after enclu
		diff2 = reinterpret_cast<uint64_t>(__new_aep) - reinterpret_cast<uint64_t>(second_enclu);
		jmptarget = static_cast<uint32_t>(diff2);
		jmptarget -= JMP_OFFSET;
	}

	auto jmpbyte1 = static_cast<uint8_t>((jmptarget & 0x000000ff) >> 0);
	auto jmpbyte2 = static_cast<uint8_t>((jmptarget & 0x0000ff00) >> 8);
	auto jmpbyte3 = static_cast<uint8_t>((jmptarget & 0x00ff0000) >> 16);
	auto jmpbyte4 = static_cast<uint8_t>((jmptarget & 0xff000000) >> 24);
	//Patching AEP enclu to a NOP-sled with a jmp
	uint8_t *enclu = second_enclu;

	// Add write permissions and patch code
	mprotect((void *)((uint64_t)enclu & ~0xFFF), 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC);
	enclu[0] = 0x90; // NOP sled
	enclu[1] = 0x90;
	enclu[2] = 0x90;
	enclu[3] = 0x90;
	enclu[4] = 0xe9; // JMP
	enclu[5] = jmpbyte1;
	enclu[6] = jmpbyte2;
	enclu[7] = jmpbyte3;
	enclu[8] = jmpbyte4;
	enclu[9] = 0x0f; // UD2
	enclu[10] = 0x0b;

	// Remove write again
	mprotect((void *)((uint64_t)enclu & ~0xFFF), 0x1000, PROT_READ | PROT_EXEC);

	std::cout << "/i\\ AEP patched" << std::endl;
}

/**
 * @return @c true, if we are running in hardware mode,
 */
bool is_hw_mode()
{
#ifdef HWMODE
	return true;
#else
	return false;
#endif
}

/**
 * Function that creates an enclave.
 * Fires an @c EnclaveCreationEvent.
 * @param[in] file_name A @c string containing the enclave binary file name.
 * @param[in] debug A @c bool, specifying whether the enclave is a debug enclave or not.
 * @param[in,out] launch_token An optional pointer to memory to save the launch token after creation or to hand over a previously generated launch token.
 * @param[out] launch_token_updated An optional pointer to an @c int that is set to non-zero, if the launch token in @p launch_token has been updated.
 * @param[out] enclave_id A pointer to memory to save the ID of the launched enclave.
 * @param misc_attr TBW
 */
extern "C" sgx_status_t sgx_create_enclave(const char *file_name, const int debug, sgx_launch_token_t *launch_token, int *launch_token_updated, sgx_enclave_id_t *enclave_id, sgx_misc_attribute_t *misc_attr)
{
	sgx_status_t ret = real_sgx_create_enclave(file_name, debug, launch_token, launch_token_updated, enclave_id, misc_attr);
	if (ret != SGX_SUCCESS)
	{
		std::string sname(file_name);
		event_store->insert_event(new sgxperf::EnclaveCreationEvent(*enclave_id, sname, ret, 0, 0));
		return ret;
	}

	// Lets find the enclave addresses
	auto cenclpoolinst = cenclavepoolinstance();

	// Now, get the CEnclave instance for the given enclave id
	auto cenclinst = cenclavepoolgetenclave(cenclpoolinst, *enclave_id);

	// Save addresses
	auto enclave_start = reinterpret_cast<uint64_t>(cenclinst->start_address);
	auto enclave_end = reinterpret_cast<uint64_t>(cenclinst->start_address) + cenclinst->size;

	auto encl = new sgxperf::Enclave(*enclave_id, cenclinst->start_address, cenclinst->size);

	std::string sname(file_name);
	auto ece = new sgxperf::EnclaveCreationEvent(*enclave_id, sname, ret, enclave_start, enclave_end);
	encl->creation_time = ece->get_time();

	// Lock map for modification
	write_lock(&event_store->enclave_map_lock);
	// Add pair
	event_store->enclave_map[*enclave_id] = encl;
	// Unlock
	write_unlock(&event_store->enclave_map_lock);

	event_store->insert_event(ece);
	return ret;
}

/**
 * Function that destroys the enclave with ID @p eid.
 * Fires an @c EnclaveDestructionEvent.
 * @param[in] eid The enclave ID of the enclave to destroy.
 */
extern "C" sgx_status_t sgx_destroy_enclave(const sgx_enclave_id_t eid)
{
	sgx_status_t ret = real_sgx_destroy_enclave(eid);

	auto ede = new sgxperf::EnclaveDestructionEvent(eid, ret);

	// Lock map for modification
	read_lock(&event_store->enclave_map_lock);
	// Modify destruction time
	event_store->enclave_map[eid]->destruction_time = ede->get_time();
	// Unlock
	read_unlock(&event_store->enclave_map_lock);

	event_store->insert_event(ede);

	return ret;
}

/**
 * Our instrumented OCall bridge that is called inside @c __ocall_bridge_caller_code.
 * Fires @c EnclaveOCallEvent and @c EnclaveOCallReturnEvent.
 * @param[in] arg Optional pointer to argument object for the OCall
 * @param[in] eid ID of enclave that performed the OCall
 * @param[in] ocall_id ID of the OCall
 */
extern "C" int __ocall_bridge(const void *arg, sgx_enclave_id_t eid, uint32_t ocall_id)
{
	// Get the original ocall_map and find the corresponding function pointer in it.
	read_lock(&event_store->enclave_map_lock);
	auto bridge = (int (*)(const void *)) event_store->enclave_map[eid]->orig_table->table[ocall_id];
	read_unlock(&event_store->enclave_map_lock);

	auto t = event_store->get_thread();

	auto ocall = new sgxperf::EnclaveOCallEvent(eid, ocall_id, arg, t->current_call);
	event_store->insert_event(ocall);
	t->current_call = ocall;

	int ret = bridge(arg);

	event_store->insert_event(new sgxperf::EnclaveOCallReturnEvent(ocall, ret));
	t->current_call = t->current_call->get_previous_call();

	return ret;
}

/**
 * @brief Overload the synchronization OCalls
 */
extern "C" int sgx_thread_wait_untrusted_event_ocall(const void *self)
{
	// Get argument
	//auto cenclave = cenclavepoolinstance();
	//auto hevent = cenclavepoolgetevent(cenclave, self);
	// According to the source, event is a pointer to calloc(1, sizeof(int)), so lets dereference it as int *
	//hevent = (void *)(*(int *)hevent);

	if (self == nullptr)
		return SGX_ERROR_INVALID_PARAMETER;

	auto t = event_store->get_thread();
	auto event = new sgxperf::EnclaveSyncWaitEvent(dynamic_cast<sgxperf::EnclaveOCallEvent *>(t->current_call));

	write_lock(&event_store->tcs_map_lock);
	event_store->tcs_map.insert(std::pair<void *, sgxperf::EnclaveEvent *>((void *)self, event));
	write_unlock(&event_store->tcs_map_lock);

	event_store->insert_event(event);

	return real_sgx_thread_wait_untrusted_event_ocall(self);
}

/**
 * @brief Overload the synchronization OCalls
 */
extern "C" int sgx_thread_set_untrusted_event_ocall(const void *waiter)
{
	// Get argument
	//auto cenclave = cenclavepoolinstance();
	//auto hevent = cenclavepoolgetevent(cenclave, waiter);
	// According to the source, event is a pointer to calloc(1, sizeof(int)), so lets dereference it as int *
	//hevent = (void *)(*(int *)hevent);

	if (waiter == nullptr)
		return SGX_ERROR_INVALID_PARAMETER;

	auto t = event_store->get_thread();

	write_lock(&event_store->tcs_map_lock);
	sgxperf::EnclaveEvent *wait_event = nullptr;

	auto it = event_store->tcs_map.find((void *)waiter);
	if (it != event_store->tcs_map.end())
	{
		wait_event = it->second;
		event_store->tcs_map.erase(it);
	}
	write_unlock(&event_store->tcs_map_lock);

	if (wait_event != nullptr)
	{
		auto event = new sgxperf::EnclaveSyncSetEvent(dynamic_cast<sgxperf::EnclaveOCallEvent *>(t->current_call), dynamic_cast<sgxperf::EnclaveSyncWaitEvent *>(wait_event));
		event_store->insert_event(event);
	}

	return real_sgx_thread_set_untrusted_event_ocall(waiter);
}

/**
 * @brief Overload the synchronization OCalls
 */
extern "C" int sgx_thread_set_multiple_untrusted_events_ocall(const void **waiters, size_t total)
{
	if (waiters == nullptr || *waiters == nullptr)
		return SGX_ERROR_INVALID_PARAMETER;

	for (size_t i = 0; i < total; ++i)
	{
		int ret = sgx_thread_set_untrusted_event_ocall(*waiters++);
		if (ret != SGX_SUCCESS)
			return ret;
	}

	return SGX_SUCCESS;
}

/**
 * @brief Overload the synchronization OCalls. This is the real implementation taken from the urts.
 */
extern "C" int sgx_thread_setwait_untrusted_events_ocall(const void *waiter, const void *self)
{
	int ret = sgx_thread_set_untrusted_event_ocall(waiter);
	if (ret != SGX_SUCCESS) return ret;

	return sgx_thread_wait_untrusted_event_ocall(self);
}

/**
 * Unused variable, only used inside the @c ocall_bridge.
 */
static int (*__ocall_bridge_addr)(const void *, sgx_enclave_id_t, uint32_t) = __ocall_bridge;

/**
 * Unused function. Used for generating the @c __ocall_bridge_caller_code array.
 */
extern "C" int ocall_bridge(const void *arg)
{
	return __ocall_bridge_addr(arg, 0x12345566, 0x56781122);
//	__asm__ volatile("mov $0xababababefefefef, %rax\n"
//			"mov $0x12345678, %esi\n"
//			"mov $0xeeffeeff, %edi\n"
//			"call *%rax\n"
//			"ret\n");
//	return 0;
}

/**
 * Contains machine code for own dynamically generated OCall bridge.
 */
static uint8_t __ocall_bridge_caller_code[] = {
		/* 00 */ 0x55,                                                       // push %rbp
		/* 01 */ 0x48, 0x89, 0xe5,                                           // mov %rsp,%rbp
		/* 04 */ 0x48, 0x83, 0xec, 0x10,                                     // sub $0x10, %rsp
		/* 08 */ 0x48, 0x89, 0x7d, 0xf8,                                     // mov %rdi,-0x8(%rbp)
		/* 0c */ 0x48, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // movabs $0x0000000000000000,%rax // address of __ocall_bridge
		/* 16 */ 0x48, 0x8b, 0x4d, 0xf8,                                     // mov -0x8(%rbp),%rcx
		/* 1a */ 0xba, 0x00, 0x00, 0x00, 0x00,                               // mov $0x56781122,%edx            // OCall ID
		/* 1f */ 0x48, 0xbe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // movabs $0x0000000000000000,%rsi // Enclave ID
		/* 24 */ 0x48, 0x89, 0xcf,                                           // mov %rcx,%rdi
		/* 27 */ 0xff, 0xd0,                                                 // callq *%rax
		/* 29 */ 0xc9,                                                       // leaveq
		/* 2a */ 0xc3                                                        // retq
};

/**
 * @brief Function that performs an ECall with ID @p ecall_id to an enclave @p eid.
 * Fires @c EnclaveECallEvent and @c EnclaveECallReturnEvent.
 * @param[in] eid The ID of the enclave to call
 * @param[in] ecall_id The ID of the ECall
 * @param[in] ocall_table A pointer to an <tt>struct ocall_table</tt> object containing all possible OCalls
 * @param[in] arg_struct A pointer to the optional ECall argument object
 */
extern "C" sgx_status_t sgx_ecall(const sgx_enclave_id_t eid, const int ecall_id, struct ocall_table *ocall_table, void *arg_struct)
{
	// We need to replace the ocall_table with our own to intercept all OCalls
	// Try to find the ocall_table for this enclave
	read_lock(&event_store->enclave_map_lock);
	auto encl = event_store->enclave_map[eid];
	read_unlock(&event_store->enclave_map_lock);
	// FIXME: Potential race if the first ecall is made in parallel by multiple threads.
	if (encl->orig_table == nullptr && encl->subst_ocall_table == nullptr)
	{
		// We don't have one, so create one
		// Copy old table
		size_t data_size = sizeof(uint32_t) + ocall_table->count * sizeof(void *);
		auto new_table = (struct ocall_table *)malloc(data_size);
		memcpy(new_table, ocall_table, data_size);

		// Replace function pointers in table
		for (uint32_t i = 0; i < ocall_table->count; ++i)
		{
			// We do "dynamic code creation" here in C++
			// __ocall_bridge_caller_code contains bytecode that calls __ocall_bridge with three arguments that we need to supply
			// First, get some executable memory
			auto new_bridge = (uint8_t *)mmap(nullptr, sizeof(__ocall_bridge_caller_code), PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
			memcpy(new_bridge, __ocall_bridge_caller_code, sizeof(__ocall_bridge_caller_code));

			// Set address of __ocall_bridge
			new_bridge[0x0e] = (uint8_t)(((uint64_t)__ocall_bridge >> 0) & 0xff);
			new_bridge[0x0f] = (uint8_t)(((uint64_t)__ocall_bridge >> 8) & 0xff);
			new_bridge[0x10] = (uint8_t)(((uint64_t)__ocall_bridge >> 16) & 0xff);
			new_bridge[0x11] = (uint8_t)(((uint64_t)__ocall_bridge >> 24) & 0xff);
			new_bridge[0x12] = (uint8_t)(((uint64_t)__ocall_bridge >> 32) & 0xff);
			new_bridge[0x13] = (uint8_t)(((uint64_t)__ocall_bridge >> 40) & 0xff);
			new_bridge[0x14] = (uint8_t)(((uint64_t)__ocall_bridge >> 48) & 0xff);
			new_bridge[0x15] = (uint8_t)(((uint64_t)__ocall_bridge >> 56) & 0xff);
			// Set OCall ID
			new_bridge[0x1b] = (uint8_t)((i >> 0) & 0xff);
			new_bridge[0x1c] = (uint8_t)((i >> 8) & 0xff);
			new_bridge[0x1d] = (uint8_t)((i >> 16) & 0xff);
			new_bridge[0x1e] = (uint8_t)((i >> 24) & 0xff);
			// Set Enclave ID
			new_bridge[0x21] = (uint8_t)((eid >> 0) & 0xff);
			new_bridge[0x22] = (uint8_t)((eid >> 8) & 0xff);
			new_bridge[0x23] = (uint8_t)((eid >> 16) & 0xff);
			new_bridge[0x24] = (uint8_t)((eid >> 24) & 0xff);
			new_bridge[0x25] = (uint8_t)((eid >> 32) & 0xff);
			new_bridge[0x26] = (uint8_t)((eid >> 40) & 0xff);
			new_bridge[0x27] = (uint8_t)((eid >> 48) & 0xff);
			new_bridge[0x28] = (uint8_t)((eid >> 56) & 0xff);
			// Set function pointer
			new_table->table[i] = (void *)new_bridge;
			mprotect(new_bridge, sizeof(__ocall_bridge_caller_code), PROT_READ | PROT_EXEC);
		}

		// Create table mapping
		encl->orig_table = ocall_table;
		encl->subst_ocall_table = new_table;
		/*while(!event_store->ocall_latch.test_and_set(std::memory_order_acquire));
		event_store->ocall_table_map[eid] = std::pair<struct ocall_table *, struct ocall_table *>(ocall_table, new_table);
		event_store->ocall_latch.clear(std::memory_order_release);
		 */
	}
	// Retrieve our ocall_table
	ocall_table = encl->subst_ocall_table;

	// TODO: read caller address from stack. should be rbp+8
	//void *caller = nullptr;
	//__asm__("movq 8(%%rbp), %0" : "=r" (caller));

	//std::cout << "Called from %p\n" << caller << std::endl;

	// Reset AEX counter
	sgxperf::Thread *t = event_store->get_thread();
	t->last_enclave = encl;

	auto ecall = new sgxperf::EnclaveECallEvent(eid, ecall_id, arg_struct, t->current_call);
	event_store->insert_event(ecall);
	t->current_call = ecall;

	sgx_status_t ret = real_sgx_ecall(eid, ecall_id, ocall_table, arg_struct);

	event_store->insert_event(new sgxperf::EnclaveECallReturnEvent(ecall, ret, dynamic_cast<sgxperf::EnclaveECallEvent *>(t->current_call)->aex_counter));
	t->current_call = t->current_call->get_previous_call();
	return ret;
}
