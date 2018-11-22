/**
 * @file enclave.cpp
 * @author weichbr
 */

#include "enclave.h"
#define LOOPS (1000000)

void ecall_void()
{
	ocall_print_string("yay\n");
}

void ecall_single()
{
}

void ecall_with_ocall()
{
	ocall_single();
}

void ecall_long()
{
	for (uint64_t i = 0; i < LOOPS; ++i)
	{
		__asm__("pause");
	}
}
