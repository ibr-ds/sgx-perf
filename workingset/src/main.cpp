/**
 * @author weichbr
 */

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <csignal>
#include <unistd.h>

#include <sgx_eid.h>
#include <sgx.h>
#include <sgx_urts.h>
#include <dlfcn.h>
#include <vector>
#include <rwlock.h>
#include <sys/mman.h>
#include <sys/user.h>
#include <cstring>

#include "elfparser.h"

const char service_interp[] __attribute__((section(".interp"))) = "/lib64/ld-linux-x86-64.so.2";

static sgx_status_t (*real_sgx_create_enclave)(const char *, const int, sgx_launch_token_t *, int *, sgx_enclave_id_t *, sgx_misc_attribute_t *) = nullptr;
static sgx_status_t (*real_sgx_destroy_enclave)(const sgx_enclave_id_t) = nullptr;

extern "C" struct CEnclave {
	void *vtable;
	sgx_enclave_id_t enclave_id;
	void *start_address;
	uint64_t size;
};

typedef void *(*CEnclavePoolInstance)();
typedef CEnclave *(*CEnclavePoolGetEnclave)(void *thiz, const sgx_enclave_id_t eid);

CEnclavePoolInstance cenclavepoolinstance = nullptr;
CEnclavePoolGetEnclave cenclavepoolgetenclave = nullptr;

class Enclave
{
public:
	explicit Enclave(sgx_enclave_id_t eid, void *encl_start, size_t size) : eid(eid),
	                                                                        encl_start(encl_start),
	                                                                        size(size),
	                                                                        creation_time(0),
	                                                                        destruction_time(UINT64_MAX),
	                                                                        page_counter(0)
	{
		encl_end = (void *)((uint64_t)encl_start + size);
		size_t pages = size/PAGE_SIZE + (size % PAGE_SIZE > 0 ? 1 : 0);
		page_status = new uint8_t[pages];
		memset(page_status, 0, pages);
	}
	virtual ~Enclave() = default;

	bool is_within_enclave(void *addr)
	{
		return encl_start <= addr && addr <= encl_end;
	}

	bool is_within_lifetime(uint64_t time)
	{
		return creation_time <= time && time <= destruction_time;
	}

	void reset_page_counter()
	{
		size_t pages = size/PAGE_SIZE + (size % PAGE_SIZE > 0 ? 1 : 0);
		memset(page_status, 0, pages);
	}

	void update_page_counter()
	{
		page_counter = 0;
		size_t pages = size/PAGE_SIZE + (size % PAGE_SIZE > 0 ? 1 : 0);
		for (size_t i = 0; i < pages; ++i)
		{
			if (page_status[i] != 0)
				page_counter++;
		}
	}

	sgx_enclave_id_t eid;
	void *encl_start;
	void *encl_end;
	size_t size;
	uint64_t creation_time;
	uint64_t destruction_time;
	uint64_t page_counter;
	uint8_t *page_status;
};

std::vector<Enclave *> *enclaves = nullptr;
rwlock_t enclaves_lock = {};

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

int initialize_urts_calls()
{
	real_sgx_create_enclave = (sgx_status_t (*)(const char *, const int, sgx_launch_token_t *, int *,
	                                            sgx_enclave_id_t *,
	                                            sgx_misc_attribute_t *)) dlsym(RTLD_NEXT, "sgx_create_enclave");
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

	// Find the CEnclavePool functions
	Dl_info dl_info = {};
	dladdr(reinterpret_cast<void *>(real_sgx_create_enclave), &dl_info);
	std::string bin(dl_info.dli_fname);
	std::string cenclpoolinst_sym("_ZN12CEnclavePool8instanceEv");
	std::string cenclpoolgetencl_sym("_ZN12CEnclavePool11get_enclaveEm");

	// First, get the CEnclavePool instance
	auto cenclpoolinst_addr = get_address_for_symbol(bin, cenclpoolinst_sym);
	cenclavepoolinstance = (CEnclavePoolInstance)((uint64_t)dl_info.dli_fbase + (uint64_t)cenclpoolinst_addr);

	auto cenclpoolgetencl_addr = get_address_for_symbol(bin, cenclpoolgetencl_sym);
	cenclavepoolgetenclave = (CEnclavePoolGetEnclave)((uint64_t)dl_info.dli_fbase + (uint64_t)cenclpoolgetencl_addr);

	return 0;
}

extern "C" sgx_status_t sgx_create_enclave(const char *file_name, const int debug, sgx_launch_token_t *launch_token, int *launch_token_updated, sgx_enclave_id_t *enclave_id, sgx_misc_attribute_t *misc_attr)
{
	sgx_status_t ret = real_sgx_create_enclave(file_name, debug, launch_token, launch_token_updated, enclave_id, misc_attr);
	if (ret != SGX_SUCCESS)
	{
		return ret;
	}

	// Lets find the enclave addresses
	auto cenclpoolinst = cenclavepoolinstance();

	// Now, get the CEnclave instance for the given enclave id
	auto cenclinst = cenclavepoolgetenclave(cenclpoolinst, *enclave_id);

	auto encl = new Enclave(*enclave_id, cenclinst->start_address, cenclinst->size);
	struct timespec t = {};
	clock_gettime(CLOCK_MONOTONIC_RAW, &t);
	encl->creation_time = static_cast<uint64_t>(t.tv_sec * 1000000000 + t.tv_nsec);

	std::cout << "Enclave created" << std::endl;

	// Remove all permissions from all enclaves pages
	mprotect(encl->encl_start, encl->size, PROT_NONE);

	write_lock(&enclaves_lock);
	enclaves->push_back(encl);
	write_unlock(&enclaves_lock);

	return ret;
}

extern "C" sgx_status_t sgx_destroy_enclave(const sgx_enclave_id_t eid)
{
	sgx_status_t ret = real_sgx_destroy_enclave(eid);
	struct timespec t = {};
	clock_gettime(CLOCK_MONOTONIC_RAW, &t);
	auto destruction_time = static_cast<uint64_t>(t.tv_sec * 1000000000 + t.tv_nsec);

	std::cout << "Enclave destroyed" << std::endl;
	// Lock map for modification
	write_lock(&enclaves_lock);
	// Modify destruction time
	auto it = enclaves->begin();
	while (it != enclaves->end())
	{
		auto encl = *it;
		if (encl->eid == eid && encl->destruction_time == UINT64_MAX)
		{
			encl->destruction_time = destruction_time;
			break;
		}
		it++;
	}
	// Unlock
	write_unlock(&enclaves_lock);

	return ret;
}

typedef void (*signal_handler_t)(int, siginfo_t *, void *);
signal_handler_t old_handlers[31] = {};

void handler(int signum, siginfo_t *siginfo, void *context)
{
	(void)context;
	if (signum != SIGSEGV)
	{
		return;
	}

	void *addr = siginfo->si_addr;


	read_lock(&enclaves_lock);

	auto it = enclaves->begin();
	while (it != enclaves->end())
	{
		auto encl = *it;
		// Only look at enclaves that are not destroyed yet and which address space includes the fault address
		if (encl->is_within_enclave(addr) && encl->destruction_time == UINT64_MAX)
		{
			auto page_addr = (void *)(((uint64_t)addr) & 0xfffffffffffff000);
			auto page_offset = reinterpret_cast<size_t>((uint64_t)page_addr - (uint64_t)encl->encl_start) / PAGE_SIZE;

			if (!__atomic_test_and_set(encl->page_status+page_offset, __ATOMIC_RELAXED))
			{
				// Setting all permissions is fine, as the real permissions are enforced by SGX
				mprotect(page_addr, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC);
			}
			read_unlock(&enclaves_lock);
			return;
		}
		it++;
	}
	read_unlock(&enclaves_lock);

	old_handlers[signum](signum, siginfo, context);
}

void print_summary();

void reset_handler(int signum, siginfo_t *siginfo, void *context)
{
	(void)context;
	(void)siginfo;
	if (signum != SIGUSR1)
		return;

	write_lock(&enclaves_lock);

	print_summary();

	auto it = enclaves->begin();
	while (it != enclaves->end())
	{
		auto encl = *it;
		encl->reset_page_counter();
		mprotect(encl->encl_start, encl->size, PROT_NONE);
		it++;
	}
	write_unlock(&enclaves_lock);

	//old_handlers[signum](signum, siginfo, context);
}

void sigint(int signum, siginfo_t *siginfo, void *context);

static int (*real_sigaction)(int, const struct sigaction*, struct sigaction*) = nullptr;
static __sighandler_t (*real_signal)(int, __sighandler_t) = nullptr;

int initialize_libc_calls()
{
	real_sigaction = (int(*)(int, const struct sigaction*, struct sigaction*))dlsym(RTLD_NEXT, "sigaction");
	if (real_sigaction == nullptr)
	{
		printf("!!! Error getting real sigaction \n");
		return -1;
	}

	real_signal = (__sighandler_t(*)(int, __sighandler_t))dlsym(RTLD_NEXT, "signal");
	if (real_signal == nullptr)
	{
		printf("!!! Error getting real signal \n");
		return -1;
	}

	return 0;
}

extern "C" int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
	if (signum == SIGSEGV || signum == SIGINT)
	{
		if (act && (act->sa_sigaction == handler || act->sa_sigaction == sigint))
		{
			// This is our SIGINT handler
			return real_sigaction(signum, act, oldact);
		}
		else
		{
			signal_handler_t old_handler = old_handlers[signum];
			if (oldact != nullptr)
			{
				oldact->sa_sigaction = old_handler;
				oldact->sa_flags = SA_SIGINFO;
			}
			if (act != nullptr)
			{
				old_handlers[signum] = act->sa_sigaction;
			}
			return 0;
		}
	}
	else if (signum == SIGUSR1)
	{
		if (act && act->sa_sigaction == reset_handler)
		{
			// This is our SIGINT handler
			return real_sigaction(signum, act, oldact);
		}
		else
		{
			signal_handler_t old_handler = old_handlers[signum];
			if (oldact != nullptr)
			{
				oldact->sa_sigaction = old_handler;
				oldact->sa_flags = SA_SIGINFO;
			}
			if (act != nullptr)
			{
				old_handlers[signum] = act->sa_sigaction;
			}
			return 0;
		}
	}
	return real_sigaction(signum, act, oldact);
}

extern "C" __sighandler_t signal(int signum, __sighandler_t _handler)
{
	if (signum == SIGSEGV || signum == SIGINT)
	{
		if (_handler == (__sighandler_t)handler || _handler == (__sighandler_t)sigint)
		{
			// This is our SIGINT handler
			return real_signal(signum, _handler);
		}
		else
		{
			if (_handler != nullptr)
			{
				old_handlers[signum] = (signal_handler_t)handler;
			}
			return (__sighandler_t)old_handlers[signum];
		}
	}
	else if (signum == SIGUSR1)
	{
		if (_handler == (__sighandler_t)reset_handler)
		{
			// This is our SIGINT handler
			return real_signal(signum, _handler);
		}
		else
		{
			if (_handler != nullptr)
			{
				old_handlers[signum] = (signal_handler_t)reset_handler;
			}
			return (__sighandler_t)old_handlers[signum];
		}
	}
	return real_signal(signum, _handler);
}


extern "C" void libmain(void)
{
	// FIXME: print the binary name. Sadly when invoked directly, we don't get argc/argv
	printf("This is the workingset analyzer for %s mode\n", is_hw_mode() ? "HW" : "SIM");
	printf("Usage: LD_PRELOAD=./libenclws%s.so <app>\n", is_hw_mode() ? "" : "sim");
	_exit(0);
}

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;
	libmain();
	return 1;
}

void print_summary()
{
	std::cout << "=== Workingset overview" << std::endl;
	auto it = enclaves->begin();
	while (it != enclaves->end())
	{
		auto encl = *it;
		encl->update_page_counter();
		uint64_t bytes = encl->page_counter * PAGE_SIZE;

		std::cout << "Enclave " << encl->eid << ": " << encl->page_counter << " Pages = " << bytes << "B (~" << bytes/1024.0/1024.0 << "MiB)" << std::endl;

		it++;
	}
}

/**
 * @brief Destructor
 */
__attribute__((destructor))
static void destroy()
{
	print_summary();
}

void sigint(int signum, siginfo_t *siginfo, void *context)
{
	if (signum != SIGINT)
		return;
	(void)siginfo;
	(void)context;
	print_summary();
	exit(0);
}

/**
 * @brief Constructor
 */
__attribute__((constructor))
static void initialize()
{
	struct sigaction sig_act = {};

	std::cout << "=== Initializing working set analyzer" << std::endl;
	if (initialize_urts_calls() < 0)
	{
		goto initerror;
	}
	if (initialize_libc_calls() < 0)
	{
		goto initerror;
	}

	enclaves = new std::vector<Enclave *>();

	// Register signal handlers
	sig_act.sa_sigaction = handler;
	sig_act.sa_flags = SA_SIGINFO | SA_NODEFER | SA_RESTART;
	sigaction(SIGSEGV, &sig_act, nullptr);
	sig_act.sa_sigaction = sigint;
	sigaction(SIGINT, &sig_act, nullptr);

	sig_act.sa_sigaction = reset_handler;
	sigaction(SIGUSR1, &sig_act, nullptr);

	printf("=== Done initializing\n");
	return;

	initerror:
	std::cout << "!!! Error initializing!" << std::endl;
	exit(-1);
}
