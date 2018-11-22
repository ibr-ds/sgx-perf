/**
 * @author weichbr
 */

#ifndef SGX_PERF_SYNCHRO_H
#define SGX_PERF_SYNCHRO_H

#include <cstdlib>
#include <cstdint>

typedef enum class __sync_ocall_type
{
	WAIT,
	SET,
	SETWAIT,
	SETMULT
} sync_ocall_type_t;

typedef struct __sync_ocall
{
	sync_ocall_type_t type;
	uint64_t time;
} sync_ocall_t;

typedef struct __sync_event
{
	uint64_t wait_parent_id;
	uint64_t wait_thread_id;
	uint64_t wait_eid;
	bool has_set;
	uint64_t set_parent_id;
	uint64_t set_thread_id;
	uint64_t set_eid;
	uint64_t time;
} sync_event_t;

extern uint64_t SgxThreadWaitUntrustedEventOcallId;
extern uint64_t SgxThreadSetUntrustedEventOcallId;
extern uint64_t SgxThreadSetWaitUntrustedEventsOcallId;
extern uint64_t SgxThreadSetMultipleUntrustedEventsOcallId;

void analyze_synchro();

#endif //SGX_PERF_SYNCHRO_H
