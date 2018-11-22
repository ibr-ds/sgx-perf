/**
 * @author weichbr
 */

#ifndef SGX_PERF_MAIN_H
#define SGX_PERF_MAIN_H

#include "util.h"
#include "synchro.h"
#include "calls.h"
#include "graph.h"
#include "security.h"
#include "sqlite3.h"
#include <set>

typedef struct __weigths
{
	double alpha;
	double beta;
	double gamma;
	double delta;
	double epsilon;
	double lambda;
} weights_t;

typedef struct __config
{
	uint64_t ecall_call_minimum;
	uint64_t ocall_call_minimum;
	struct
	{
		bool calls;
		bool sync;
		bool sec;
	} phases;
	weights_t duplication_weights;
	weights_t reordering_weights;
	weights_t merging_weights;
	weights_t batching_weights;
	std::set<uint64_t> ecall_set;
	std::set<uint64_t> ocall_set;
	std::string graph;
	std::string call_data_filename;
	std::string edl_path;
} config_t;

extern sqlite3 *db;
extern config_t config;
extern uint64_t EnclaveOCallEventId;
extern uint64_t EnclaveOCallReturnEventId;

#endif //SGX_PERF_MAIN_H
