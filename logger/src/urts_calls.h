/**
 * @file urts_calls.h
 * @author weichbr
 */

#include <cstdint>
#include <cstddef>
#include <sgx_eid.h>

#ifndef SGX_PERF_URTS_CALLS_H
#define SGX_PERF_URTS_CALLS_H

int initialize_urts_calls();
bool is_hw_mode();

/**
 * @brief struct describing the OCall table
 */
extern "C" struct ocall_table {
	uint32_t count; ///< Number of OCalls managed by this table
	void *table[]; ///< Array mapping OCall IDs to function pointers
};

extern "C" struct CEnclave {
	void *vtable;
	sgx_enclave_id_t enclave_id;
	void *start_address;
	uint64_t size;
};


// tct_t, exit_info_t and ssa_gpr_t taken from Intel SGX SDK
typedef struct __tcs
{
	uint64_t            reserved0;       /* (0) */
	uint64_t            flags;           /* (8)bit 0: DBGOPTION */
	uint64_t            ossa;            /* (16)State Save Area */
	uint32_t            cssa;            /* (24)Current SSA slot */
	uint32_t            nssa;            /* (28)Number of SSA slots */
	uint64_t            oentry;          /* (32)Offset in enclave to which control is transferred on EENTER if enclave INACTIVE state */
	uint64_t            reserved1;       /* (40) */
	uint64_t            ofs_base;        /* (48)When added to the base address of the enclave, produces the base address FS segment inside the enclave */
	uint64_t            ogs_base;        /* (56)When added to the base address of the enclave, produces the base address GS segment inside the enclave */
	uint32_t            ofs_limit;       /* (64)Size to become the new FS limit in 32-bit mode */
	uint32_t            ogs_limit;       /* (68)Size to become the new GS limit in 32-bit mode */
#define TCS_RESERVED_LENGTH 4024
	uint8_t             reserved[TCS_RESERVED_LENGTH];  /* (72) */
}tcs_t;

static_assert(sizeof(tcs_t) == 4096, "TCS struct must be size of page!");

typedef struct __exit_info
{
	uint32_t    vector:8;                /* Exception number of exceptions reported inside enclave */
	uint32_t    exit_type:3;             /* 3: Hardware exceptions, 6: Software exceptions */
	uint32_t    reserved:20;
	uint32_t    valid:1;                 /* 0: unsupported exceptions, 1: Supported exceptions */
} exit_info_t;

typedef struct __ssa_gpr
{
	uint64_t    rax;                    /* (0) */
	uint64_t    rcx;                    /* (8) */
	uint64_t    rdx;                    /* (16) */
	uint64_t    rbx;                    /* (24) */
	uint64_t    rsp;                    /* (32) */
	uint64_t    rbp;                    /* (40) */
	uint64_t    rsi;                    /* (48) */
	uint64_t    rdi;                    /* (56) */
	uint64_t    r8;                     /* (64) */
	uint64_t    r9;                     /* (72) */
	uint64_t    r10;                    /* (80) */
	uint64_t    r11;                    /* (88) */
	uint64_t    r12;                    /* (96) */
	uint64_t    r13;                    /* (104) */
	uint64_t    r14;                    /* (112) */
	uint64_t    r15;                    /* (120) */
	uint64_t    rflags;                 /* (128) */
	uint64_t    rip;                    /* (136) */
	uint64_t    rsp_u;                  /* (144) untrusted stack pointer. saved by EENTER */
	uint64_t    rbp_u;                  /* (152) untrusted frame pointer. saved by EENTER */
	exit_info_t exit_info;              /* (160) contain information for exits */
	uint32_t    reserved;               /* (164) padding to multiple of 8 bytes */
	uint64_t    fs;                     /* (168) FS register */
	uint64_t    gs;                     /* (176) GS register */
} ssa_gpr_t;

typedef void *(*CEnclavePoolInstance)();
typedef void *(*CEnclavePoolGetEvent)(void *thiz, void const *self);
typedef CEnclave *(*CEnclavePoolGetEnclave)(void *thiz, const sgx_enclave_id_t eid);

typedef struct __ms_sgx_thread_wait_untrusted_event_ocall
{
	void const *self;
} ms_sgx_thread_wait_untrusted_event_ocall_t;

typedef struct __ms_sgx_thread_set_untrusted_event_ocall
{
	void const *waiter;
} ms_sgx_thread_set_untrusted_event_ocall_t;

typedef struct __ms_sgx_thread_set_multiple_untrusted_events_ocall
{
	void const **waiters;
	size_t total;
} ms_sgx_thread_set_multiple_untrusted_events_ocall_t;

typedef struct __ms_sgx_thread_setwait_untrusted_events_ocall
{
	void const *waiter; ///< Set is called with this
	void const *self; ///< Wait is called with this
} ms_sgx_thread_setwait_untrusted_events_ocall_t;

bool read_from_enclave(void *addr, void *buffer, size_t size, size_t *read_nr);

#endif //SGX_PERF_URTS_CALLS_H
