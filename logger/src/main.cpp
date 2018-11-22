/**
 * @file main.cpp
 * @author weichbr
 */

#include "libc_calls.h"
#include "urts_calls.h"
#include "store.h"
#include "perf.h"
#include "config.h"

#include <unistd.h>
#include <csignal>
#include <cstring>
#include <iostream>
#include <execinfo.h>

const char service_interp[] __attribute__((section(".interp"))) = "/lib64/ld-linux-x86-64.so.2";

sgxperf::EventStore *event_store = nullptr;
sgxperf::Perf *perf = nullptr;
sgxperf::Config *config = nullptr;

#define NAME "sgx-perf"

/**
 * @brief Entry point for the shared object, simply prints usage info as the logger has to be invoked with LD_PRELOAD on a normal application
 */
extern "C" void libmain(void)
{
	// FIXME: print the binary name. Sadly when invoked directly, we don't get argc/argv
	printf("This is the logger for %s mode\n", is_hw_mode() ? "HW" : "SIM");
	printf("Usage: LD_PRELOAD=./liblogger%s.so <app>\n", is_hw_mode() ? "" : "sim");
	_exit(0);
}

static void destroy(); //!< Forward declaration of destroy

typedef void (*signal_handler_t)(int, siginfo_t *, void *);
extern signal_handler_t old_handlers[12];

/**
 * @brief Our signal handler. Catches faults inside the enclave.
 * @param signum
 * @param siginfo
 * @param context
 */
void sigint_handler(int signum, siginfo_t *siginfo, void *context)
{
	// Save rbx because if fault occurred inside enclave this contains tcs address
	tcs_t *tcs_addr = nullptr;
	__asm__("mov %%rbx, %0" : "=g"(tcs_addr) : : "rax", "rbx", "rcx");

	std::cout << "Caught signal " << signum << std::endl;

	auto se = new sgxperf::SignalEvent(signum, nullptr, siginfo->si_code);

	if (signum == SIGILL || signum == SIGSEGV || signum == SIGFPE || signum == SIGBUS || signum == SIGTRAP)
	{
		// Signal is a fault, so record the address.
		// Address inside the enclave are missing the bottom 12 bits, so we can only see the page.
		// So find out whether it is an enclave address

		void *faddr = siginfo->si_addr;
		auto thread = event_store->get_thread();

		if (thread->last_enclave != nullptr)
		{
			auto encl = thread->last_enclave;
			if (encl->encl_start >= faddr && encl->encl_end < faddr)
			{
				// Fault happened inside enclave, so read out SSA to get real address
				auto ssa_addr = (ssa_gpr_t *)(tcs_addr + 0x2);
				ssa_addr = (ssa_gpr_t *)((uint8_t *)ssa_addr - 184);
				se->set_fault_addr(reinterpret_cast<void *>(ssa_addr->rip));
			}
			else
			{
				se->set_fault_addr(siginfo->si_addr);
			}
		}
		else
		{
			se->set_fault_addr(siginfo->si_addr);
		}

		std::cout << "Fault address is " << se->get_fault_addr() << std::endl;
	}

	event_store->insert_event(se);

	if (signum == SIGINT || signum == SIGILL || signum == SIGSEGV || signum == SIGFPE || signum == SIGBUS || signum == SIGTRAP)
	{
		if (old_handlers[signum] != nullptr)
		{
			std::cout << "Forwarding signal" << std::endl;
			old_handlers[signum](signum, siginfo, context);
		}
		else
		{
			destroy();
			exit(0);
		}
	}

	if (signum == SIGABRT)
	{
		std::cout << "!!! SIGABRT received, exiting" << std::endl;

		int nptrs = 0;
		void *buffer[100];
		nptrs = backtrace(buffer, 100);
		backtrace_symbols_fd(buffer, nptrs, STDOUT_FILENO);

		destroy();
		exit(0);
	}
}

/**
 * @brief Main method of logger, calls the entry point libmain()
 */
int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;
	libmain();
	return 1;
}

/**
 * @brief Constructor
 */
__attribute__((constructor))
static void initialize()
{
	// Initialize streams, see https://stackoverflow.com/a/8785008
	std::ios_base::Init _initializer;

	struct sigaction sig_act = {};
	sgxperf::ThreadCreationEvent *tce = nullptr;

	if (event_store != nullptr)
	{
		std::cout << "!!! Already initialized!" << std::endl;
		return;
	}
	std::cout << "=== Initializing " << NAME << std::endl;

	// Initialize config
	config = new sgxperf::Config();
	config->init();

	// Initialize call interceptor
	if (initialize_libc_calls() < 0)
	{
		goto initerror;
	}
	if (initialize_urts_calls() < 0)
	{
		goto initerror;
	}

	// Initialize Event Store
	event_store = new sgxperf::EventStore();

	if (event_store->create_database() < 0)
	{
		goto initerror;
	}

	tce = new sgxperf::ThreadCreationEvent(pthread_self(), nullptr);
	event_store->insert_event(tce);

	// Initialize perf
	perf = new sgxperf::Perf();
	perf->init();

	// Register signal handlers
	sig_act.sa_sigaction = sigint_handler;
	sig_act.sa_flags = SA_SIGINFO | SA_NODEFER | SA_RESTART;
	sigaction(SIGINT, &sig_act, nullptr);
	sigaction(SIGILL, &sig_act, nullptr);
	sigaction(SIGSEGV, &sig_act, nullptr);
	sigaction(SIGFPE, &sig_act, nullptr);
	sigaction(SIGBUS, &sig_act, nullptr);
	sigaction(SIGTRAP, &sig_act, nullptr);
	sigaction(SIGABRT, &sig_act, nullptr);

	perf->start_sampling();

	std::cout << "=== Done initializing" << std::endl;
	return;

	initerror:
	std::cout << "!!! Error initializing!" << std::endl;
	exit(-1);
}

/**
 * @brief Destructor
 */
__attribute__((destructor))
static void destroy()
{
	if (event_store->is_finalized())
		return;

	perf->stop_sampling();

	event_store->finalize();
	//event_store->printSummary();
	std::stringstream ss;
	ss << "out-" << getpid() << ".db";
	auto filename = ss.str();
	std::cout << "=== Writing to " << filename << std::endl;
	event_store->write_summary(filename);
	std::cout << "=== Shutting down " << NAME << std::endl;
}
