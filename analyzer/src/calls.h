/**
 * @author weichbr
 */

#ifndef SGX_PERF_CALLS_H
#define SGX_PERF_CALLS_H

#include <cstdint>
#include <vector>

typedef enum class __call_type
{
	UNDEF = 0,
	ECALL = 1,
	OCALL = 2,
} call_type_t;

typedef struct __single_call_data
{
	uint64_t event_id;
	uint64_t call_id;
	call_type_t type;
	uint64_t start;
	uint64_t end;
	uint64_t exec;
	struct __single_call_data *parent;
} single_call_data_t;

typedef struct __thread_data
{
	uint64_t id;
	pthread_t pthread_id;
	single_call_data_t *last_call;
	uint64_t next_call_index;
	single_call_data_t *calls;
} thread_t;

typedef struct __stats_data
{
	uint64_t sum;
	uint64_t sq_sum;
	uint64_t avg;
	uint64_t calls;
	uint64_t median;
	uint64_t std;
	uint64_t aexs;
	uint64_t num_less_1us;
	uint64_t num_less_5us;
	uint64_t num_less_10us;
} stats_t;

struct __call_data;

typedef struct __parent_call_data
{
	uint64_t count;
	struct __call_data *call_data;
	// For direct parents:
	uint64_t num_less_than_10us_from_start;
	uint64_t num_less_than_20us_from_start;
	uint64_t num_less_than_10us_from_end;
	uint64_t num_less_than_20us_from_end;
	// For indirect parents:
	uint64_t num_less_1us;
	uint64_t num_less_5us;
	uint64_t num_less_10us;
	uint64_t num_less_20us;
} parent_call_data_t;

typedef struct __call_data
{
	call_type_t type;
	uint64_t call_id;
	std::string *name;
	std::vector<uint64_t> *exectimes;
	std::vector<single_call_data_t *> *single_calls;
	std::vector<uint64_t> *aex_counts;
	//std::vector<uint64_t> *direct_parents;
	bool has_direct_parents;
	uint64_t num_ecall_called_from_ocalls;
	std::vector<parent_call_data_t> *direct_parents_data;
	bool has_indirect_parents;
	std::vector<parent_call_data_t> *indirect_parents_data;
	stats_t all_stats;
	stats_t stats_95th;
} call_data_t;

typedef struct __enclave_data
{
	__enclave_data() : eid(0), ecall_count(0), ocall_count(0), first_ecall_start(UINT64_MAX), last_ecall_end(0) {}
	uint64_t eid;
	std::vector<call_data_t *> ecalls;
	std::vector<call_data_t *> ecalls_sorted;
	uint64_t ecall_count;
	std::vector<call_data_t *> ocalls;
	std::vector<call_data_t *> ocalls_sorted;
	uint64_t ocall_count;
	uint64_t first_ecall_start;
	uint64_t last_ecall_end;
} enclave_data_t;

typedef struct __general_data
{
	uint64_t starttime;
	uint64_t endtime;
	uint64_t main_thread;
} general_data_t;

void analyze_calls();

#endif //SGX_PERF_CALLS_H
