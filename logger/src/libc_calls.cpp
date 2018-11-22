/**
 * @file libc_calls.cpp
 * @author weichbr
 */

#include <memory>
#include <pthread.h>
#include <dlfcn.h>
#include <cstring>

#include "libc_calls.h"
#include "store.h"
#include "main.h"

extern sgxperf::EventStore *event_store;

static int (*real_pthread_create)(pthread_t *, const pthread_attr_t *, void * (*)(void *), void *) = nullptr;
static int (*real_pthread_setname_np)(pthread_t, const char *) = nullptr;
static int (*real_sigaction)(int, const struct sigaction*, struct sigaction*) = nullptr;
static __sighandler_t (*real_signal)(int, __sighandler_t) = nullptr;

/**
 * Initializes the intercepted libc calls
 */
int initialize_libc_calls()
{
	real_pthread_create = (int (*)(pthread_t *, const pthread_attr_t *, void * (*)(void *), void *)) dlsym(RTLD_NEXT, "pthread_create");
	if (real_pthread_create == nullptr)
	{
		printf("!!! Error getting real pthread_create \n");
		return -1;
	}

	real_pthread_setname_np = (int (*)(pthread_t, const char *)) dlsym(RTLD_NEXT, "pthread_setname_np");
	if (real_pthread_setname_np == nullptr)
	{
		printf("!!! Error getting real pthread_setname_np \n");
		return -1;
	}

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

/**
 * @brief Argument struct for the intercepter thread.
 */
typedef struct intercepter_thread_arg
{
	pthread_t creator_thread;
	void *(*orig_start)(void *);
	void *orig_arg;
} intercepter_thread_arg_t;

/**
 * @brief New thread start function that fires @c ThreadCreationEvent and @c ThreadDestructionEvent
 */
void *intercepter_thread_start(void *arg)
{
	auto *args = (intercepter_thread_arg_t *)arg;

	// Storing thread creation
	event_store->insert_event(new sgxperf::ThreadCreationEvent(args->creator_thread, args->orig_arg));

	// Calling original thread start function
	void *ret = args->orig_start(args->orig_arg);

	// Storing thread destruction
	event_store->insert_event(new sgxperf::ThreadDestructionEvent(args->creator_thread, ret));

	return ret;
}

#undef pthread_create
/**
 * @brief Overwritten thread creation function to allow tracking of all created threads.
 */
extern "C" int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*orig_start)(void *), void *arg) throw()
{
	auto args = new intercepter_thread_arg_t();
	args->creator_thread = pthread_self();
	args->orig_arg = arg;
	args->orig_start = orig_start;
	arg = (void *)args;

	// TODO: If orig_start is libstdc++.6.so+0xb8c60 then this thread was created using std::thread.
	// TODO: In that case, we need to extract the real orig_start from arg.
	// TODO: However, we need to be aware of version differences
	// I found the real address at ((uint64_t**)arg)[4], don't know if this reliable
	// gdb: p /x *(uint64_t[10]*)arg in break pthread_create

	/*
	// Test if orig_start lies within libstdc++, if so, this might be a std::thread
	Dl_info dl_info = {};
	dladdr((const void *)orig_start, &dl_info);

	if (strstr(dl_info.dli_fname, "libstdc++"))
	{
		auto arg_obj = (__std_thread_arg *) args->orig_arg;
	}
	*/

	auto e = new sgxperf::ThreadCreatorEvent();

	int ret = real_pthread_create(thread, attr, intercepter_thread_start, arg);

	e->set_info(*thread, (void *) orig_start, ret);
	event_store->insert_event(e);

	return ret;
}

#undef pthread_setname_np
/**
 * @brief Capture thread name setting
 */
extern "C" int pthread_setname_np(pthread_t thread, const char *name)
{
	std::string sname(name);
	auto e = new sgxperf::ThreadSetNameEvent(thread, sname);
	event_store->insert_event(e);
	int ret = real_pthread_setname_np(thread, name);
	e->set_info(ret);
	return ret;
}


typedef void (*signal_handler_t)(int, siginfo_t *, void *);

signal_handler_t old_handlers[12] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

#undef sigaction
/**
 * @brief Capture signal handler registration and register our own
 */
extern "C" int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
	if (signum == SIGINT || signum == SIGILL || signum == SIGSEGV  || signum == SIGFPE || signum == SIGBUS || signum == SIGTRAP)
	{
		if (act && act->sa_sigaction == sigint_handler)
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
	// TODO capture event
	return real_sigaction(signum, act, oldact);
}

/**
 * @brief Capture signal handler registration and register our own
 */
extern "C" __sighandler_t signal(int signum, __sighandler_t handler)
{
	if (signum == SIGINT || signum == SIGILL || signum == SIGSEGV  || signum == SIGFPE || signum == SIGBUS || signum == SIGTRAP)
	{
		if (handler == (__sighandler_t)sigint_handler)
		{
			// This is our SIGINT handler
			return real_signal(signum, handler);
		}
		else
		{
			if (handler != nullptr)
			{
				old_handlers[signum] = (signal_handler_t)handler;
			}
			return (__sighandler_t)old_handlers[signum];
		}
	}
	return real_signal(signum, handler);
}

