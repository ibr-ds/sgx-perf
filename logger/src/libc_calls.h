/**
 * @file libc_calls.h
 * @author weichbr
 */

#ifndef SGX_PERF_LIBC_CALLS_H
#define SGX_PERF_LIBC_CALLS_H

#include <cstdint>

int initialize_libc_calls();

typedef struct __std_thread_arg
{
	void *_vtable;
	struct __std_thread_arg *_M_this_ptr;
	void *base_object; // Pointer to 0x10 bytes before self
	uint64_t arg;
	void *orig_function;
} std_thread_arg_t;

#endif //SGX_PERF_LIBC_CALLS_H
