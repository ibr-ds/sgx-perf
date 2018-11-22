/**
 * @file app.cpp
 * @author weichbr
 */


#include <iostream>
#include <pwd.h>
#include <unistd.h>
#include <ctime>
#include "app.h"
#include "sgx_urts.h"

#define ENCLAVE_FILENAME "libbenchenclave.signed.so"
#define MAX_PATH 1024

#define WARMUP_ITERATIONS (1000)

enum __mode
{
	MODE_SINGLE_CALL = 1,
	MODE_CALL_WITH_OCALL = 2,
	MODE_LONG_CALL = 3,
};

/* Global EID shared by multiple threads */
sgx_enclave_id_t global_eid = 0;

typedef struct _sgx_errlist_t {
	sgx_status_t err;
	const char *msg;
	const char *sug; /* Suggestion */
} sgx_errlist_t;

/* Error code returned by sgx_create_enclave */
static sgx_errlist_t sgx_errlist[] = {
		{
				SGX_ERROR_UNEXPECTED,
				"Unexpected error occurred.",
				NULL
		},
		{
				SGX_ERROR_INVALID_PARAMETER,
				"Invalid parameter.",
				NULL
		},
		{
				SGX_ERROR_OUT_OF_MEMORY,
				"Out of memory.",
				NULL
		},
		{
				SGX_ERROR_ENCLAVE_LOST,
				"Power transition occurred.",
				"Please refer to the sample \"PowerTransition\" for details."
		},
		{
				SGX_ERROR_INVALID_ENCLAVE,
				"Invalid enclave image.",
				NULL
		},
		{
				SGX_ERROR_INVALID_ENCLAVE_ID,
				"Invalid enclave identification.",
				NULL
		},
		{
				SGX_ERROR_INVALID_SIGNATURE,
				"Invalid enclave signature.",
				NULL
		},
		{
				SGX_ERROR_OUT_OF_EPC,
				"Out of EPC memory.",
				NULL
		},
		{
				SGX_ERROR_NO_DEVICE,
				"Invalid SGX device.",
				"Please make sure SGX module is enabled in the BIOS, and install SGX driver afterwards."
		},
		{
				SGX_ERROR_MEMORY_MAP_CONFLICT,
				"Memory map conflicted.",
				NULL
		},
		{
				SGX_ERROR_INVALID_METADATA,
				"Invalid enclave metadata.",
				NULL
		},
		{
				SGX_ERROR_DEVICE_BUSY,
				"SGX device was busy.",
				NULL
		},
		{
				SGX_ERROR_INVALID_VERSION,
				"Enclave version was invalid.",
				NULL
		},
		{
				SGX_ERROR_INVALID_ATTRIBUTE,
				"Enclave was not authorized.",
				NULL
		},
		{
				SGX_ERROR_ENCLAVE_FILE_ACCESS,
				"Can't open enclave file.",
				NULL
		},
};

/* Check error conditions for loading enclave */
void print_error_message(sgx_status_t ret)
{
	size_t idx = 0;
	size_t ttl = sizeof sgx_errlist/sizeof sgx_errlist[0];

	for (idx = 0; idx < ttl; idx++) {
		if(ret == sgx_errlist[idx].err) {
			if(NULL != sgx_errlist[idx].sug)
				printf("Info: %s\n", sgx_errlist[idx].sug);
			printf("Error: %s\n", sgx_errlist[idx].msg);
			break;
		}
	}

	if (idx == ttl)
		printf("Error: Unexpected error occurred.\n");
}

/* Initialize the enclave:
 *   Step 1: try to retrieve the launch token saved by last transaction
 *   Step 2: call sgx_create_enclave to initialize an enclave instance
 *   Step 3: save the launch token if it is updated
 */
int initialize_enclave(void)
{
	sgx_launch_token_t token = {0};
	sgx_status_t ret = SGX_ERROR_UNEXPECTED;
	int updated = 0;

	/* Step 2: call sgx_create_enclave to initialize an enclave instance */
	/* Debug Support: set 2nd parameter to 1 */
	ret = sgx_create_enclave(ENCLAVE_FILENAME, SGX_DEBUG_FLAG, &token, &updated, &global_eid, NULL);
	if (ret != SGX_SUCCESS) {
		print_error_message(ret);
		return -1;
	}

	return 0;
}

void timespec_diff(struct timespec *start, struct timespec *stop,
                   struct timespec *result)
{
	if ((stop->tv_nsec - start->tv_nsec) < 0) {
		result->tv_sec = stop->tv_sec - start->tv_sec - 1;
		result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
	} else {
		result->tv_sec = stop->tv_sec - start->tv_sec;
		result->tv_nsec = stop->tv_nsec - start->tv_nsec;
	}
}

void single_call(uint64_t iterations)
{
	struct timespec start = {}, end = {}, diff = {};

	// Warmup
	for (uint64_t i = 0; i < WARMUP_ITERATIONS; ++i)
	{
		ecall_single(global_eid);
	}
	// Start measurement
	clock_gettime(CLOCK_MONOTONIC_RAW, &start);
	for (uint64_t i = 0; i < iterations; ++i)
	{
		ecall_single(global_eid);
	}
	clock_gettime(CLOCK_MONOTONIC_RAW, &end);

	timespec_diff(&start, &end, &diff);

	fprintf(stderr, "%lu.%09lu,", diff.tv_sec, diff.tv_nsec);
	uint64_t ns = diff.tv_sec * 1000000000UL + (uint64_t)diff.tv_nsec;
	uint64_t nsper = ns / iterations;
	fprintf(stderr, "%lu\n", nsper);
}

void call_with_ocall(uint64_t iterations)
{
	struct timespec start = {}, end = {}, diff = {};

	// Warmup
	for (uint64_t i = 0; i < WARMUP_ITERATIONS; ++i)
	{
		ecall_with_ocall(global_eid);
	}
	// Start measurement
	clock_gettime(CLOCK_MONOTONIC_RAW, &start);
	for (uint64_t i = 0; i < iterations; ++i)
	{
		ecall_with_ocall(global_eid);
	}
	clock_gettime(CLOCK_MONOTONIC_RAW, &end);

	timespec_diff(&start, &end, &diff);

	fprintf(stderr, "%lu.%09lu,", diff.tv_sec, diff.tv_nsec);
	uint64_t ns = diff.tv_sec * 1000000000UL + (uint64_t)diff.tv_nsec;
	uint64_t nsper = ns / iterations;
	fprintf(stderr, "%lu\n", nsper);
}

void long_call(uint64_t iterations)
{
	struct timespec start = {}, end = {}, diff = {};

	// Warmup
	for (uint64_t i = 0; i < WARMUP_ITERATIONS; ++i)
	{
		ecall_long(global_eid);
	}
	// Start measurement
	clock_gettime(CLOCK_MONOTONIC_RAW, &start);
	for (uint64_t i = 0; i < iterations; ++i)
	{
		ecall_long(global_eid);
	}
	clock_gettime(CLOCK_MONOTONIC_RAW, &end);

	timespec_diff(&start, &end, &diff);

	fprintf(stderr, "%lu.%09lu,", diff.tv_sec, diff.tv_nsec);
	uint64_t ns = diff.tv_sec * 1000000000UL + (uint64_t)diff.tv_nsec;
	uint64_t nsper = ns / iterations;
	fprintf(stderr, "%lu\n", nsper);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	/* Initialize the enclave */
	if(initialize_enclave() < 0){
		printf("Enclave creation failed :(\n");
		return -1;
	}

	if (argc != 3)
	{
		printf("Usage: %s iterations mode\n", argv[0]);
		printf("mode:\t%u\tSingle ECall\n\t%u\tECall with OCall\n\t%u\tLong ECall\n", MODE_SINGLE_CALL, MODE_CALL_WITH_OCALL, MODE_LONG_CALL);
		sgx_destroy_enclave(global_eid);
		return -1;
	}

	uint64_t iterations = strtoul(argv[1], nullptr, 10);
	uint64_t mode = strtoul(argv[2], nullptr, 10);

	switch(mode)
	{
		case MODE_SINGLE_CALL:
			single_call(iterations);
			break;
		case MODE_CALL_WITH_OCALL:
			call_with_ocall(iterations);
			break;
		case MODE_LONG_CALL:
			long_call(iterations);
			break;
		default:
			printf("Unknown mode!\n");
			break;
	}

	sgx_destroy_enclave(global_eid);
	return 0;
}

void ocall_print_string(const char *s)
{
	std::cout << s;
}

void ocall_single()
{
	return;
}
