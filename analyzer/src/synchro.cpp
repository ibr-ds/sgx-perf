/**
 * @author weichbr
 */

#include "main.h"

#include <iostream>
#include <algorithm>

extern std::map<uint64_t, enclave_data_t> encls;

/**
 * Synchronization analyzer
 */

uint64_t SgxThreadWaitUntrustedEventOcallId = 0;
uint64_t SgxThreadSetUntrustedEventOcallId = 0;
uint64_t SgxThreadSetWaitUntrustedEventsOcallId = 0;
uint64_t SgxThreadSetMultipleUntrustedEventsOcallId = 0;

uint64_t EnclaveSyncWaitEventId = 0;
uint64_t EnclaveSyncSetEventId = 0;

bool has_sync_ocalls = false;

int ocall_id_callback(void *arg, int count, char **data, char **columns)
{
	(void)arg;
	(void)count;
	(void)columns;
	if (hasEnding(std::string(data[1]), "sgx_thread_wait_untrusted_event_ocall"))
	{
		SgxThreadWaitUntrustedEventOcallId = strtoul(data[0], nullptr, 10);
	}
	else if (hasEnding(std::string(data[1]), "sgx_thread_set_untrusted_event_ocall"))
	{
		SgxThreadSetUntrustedEventOcallId = strtoul(data[0], nullptr, 10);
	}
	else if (hasEnding(std::string(data[1]), "sgx_thread_setwait_untrusted_events_ocall"))
	{
		SgxThreadSetWaitUntrustedEventsOcallId = strtoul(data[0], nullptr, 10);
	}
	else if (hasEnding(std::string(data[1]), "sgx_thread_set_multiple_untrusted_events_ocall"))
	{
		SgxThreadSetMultipleUntrustedEventsOcallId = strtoul(data[0], nullptr, 10);
	}

	has_sync_ocalls = true;

	return 0;
}

int event_id_callback(void *arg, int count, char **data, char **columns)
{
	(void)arg;
	(void)count;
	(void)columns;
	if (hasEnding(std::string(data[1]), "EnclaveSyncWaitEvent"))
	{
		EnclaveSyncWaitEventId = strtoul(data[0], nullptr, 10);
	}
	else if (hasEnding(std::string(data[1]), "EnclaveSyncSetEvent"))
	{
		EnclaveSyncSetEventId = strtoul(data[0], nullptr, 10);
	}

	return 0;
}

uint64_t found_sync_ocalls = 0;

int found_sync_ocalls_callback(void *arg, int count, char **data, char **columns)
{
	(void)arg;
	(void)count;
	(void)columns;
	found_sync_ocalls = strtoul(data[0], nullptr, 10);
	return 0;
}

std::vector<sync_event_t> sync_events;

int wait_event_callback(void *arg, int count, char **data, char **columns)
{
	(void) arg;
	(void) count;
	(void) columns;

	sync_event_t se = {};
	se.wait_parent_id = strtoul(data[2], nullptr, 10);
	se.wait_thread_id = strtoul(data[0], nullptr, 10);
	se.wait_eid = strtoul(data[1], nullptr, 10);
	se.has_set = data[3] != nullptr;
	if (se.has_set)
	{
		se.set_parent_id = strtoul(data[5], nullptr, 10);
		se.set_thread_id = strtoul(data[3], nullptr, 10);
		se.set_eid = strtoul(data[4], nullptr, 10);
		se.time = strtoul(data[6], nullptr, 10);
	}
	sync_events.push_back(se);
	return 0;
}

uint64_t ocall_percentile(double percentile, std::vector<sync_ocall_t *> &vec)
{
	return vec[percentile_idx(percentile, vec)]->time;
}

void print_ocall_percentile(uint8_t percentile, std::vector<sync_ocall_t *> &vec)
{
	uint64_t percentile_ns = ocall_percentile(percentile/100.0f, vec);
	uint64_t percentile_us = percentile_ns / 1000;
	std::cout << "(i) " << (int)percentile << "th percentile:  " << percentile_ns << "ns / " << percentile_us << "µs" << std::endl;
}

void analyze_synchro()
{
	std::stringstream ss;
	std::cout << "=== Analyzing synchronization OCalls" << std::endl;

	ss << "select id, symbol_name from ocalls as oc where symbol_name like \"%sgx_thread%untrusted_event%_ocall\";";

	sql_exec(ss, ocall_id_callback);

	if (!has_sync_ocalls)
	{
		std::cout << "(i) No sync ocalls found." << std::endl;
		return;
	}

	ss << "select COUNT(*) from events as e "
			"where e.type = " << EnclaveOCallEventId << " "
			   "and e.call_id in (" << SgxThreadWaitUntrustedEventOcallId << ", "
	   << SgxThreadSetUntrustedEventOcallId << ", "
	   << SgxThreadSetWaitUntrustedEventsOcallId << ", "
	   << SgxThreadSetMultipleUntrustedEventsOcallId << ");";

	sql_exec(ss, found_sync_ocalls_callback);

	std::cout << "(i) Found " << found_sync_ocalls << " synchronization OCalls" << std::endl;

	if (found_sync_ocalls == 0)
	{
		return;
	}

	ss << "select id, name from event_map;";
	sql_exec(ss, event_id_callback);

	ss << "select waitevent.involved_thread as wait_thread, waitevent.eid as wait_eid, ecallwait.call_id as wait_parent_id, ocallset.involved_thread as set_thread, ecallset.eid as set_eid, ecallset.call_id as set_parent_id, (setevent.time - waitevent.time) as resolvetime from events as waitevent\n"
			"join events as ocallwait on waitevent.call_event = ocallwait.id\n"
			"join events as ecallwait on ecallwait.id = ocallwait.call_event\n"
			"left join events as setevent on setevent.arg = waitevent.id\n"
			"left join events as ocallset on setevent.call_event = ocallset.id\n"
			"left join events as ecallset on ecallset.id = ocallset.call_event\n"
			"where waitevent.type = " << EnclaveSyncWaitEventId << ";";

	sql_exec(ss, wait_event_callback);

	std::cout << sync_events.size() << " wait events" << std::endl;

	uint64_t num_less_1us = 0, num_less_5us = 0, num_less_10us = 0, num_less_20us = 0, num_less_100us = 0;

	std::for_each(sync_events.begin(), sync_events.end(), [&num_less_1us, &num_less_5us, &num_less_10us, &num_less_20us, &num_less_100us](sync_event_t &se) {
		auto wcd = encls[se.wait_eid].ecalls[se.wait_parent_id];
		//std::cout << "{" << se.wait_thread_id << "} " << "[" << se.wait_parent_id << "] " << *wcd->name;
		if (se.has_set)
		{
			auto scd = encls[se.wait_eid].ecalls[se.wait_parent_id];
			//std::cout << " --(" << timeformat(se.time, true) << ")-> " << "{" << se.set_thread_id << "} " << "[" << se.set_parent_id << "] " << *scd->name;

			if (se.time < 1000)
			{
				num_less_1us++;
			}
			else if (se.time < 5000)
				num_less_5us++;
			else if (se.time < 10000)
				num_less_10us++;
			else if (se.time < 20000)
				num_less_20us++;
			else if (se.time < 100000)
				num_less_100us++;

		}
		//std::cout << std::endl;
	});

	std::cout << "<   1µs : " << countformat(num_less_1us, sync_events.size(), true) << std::endl;
	std::cout << "<   5µs : " << countformat(num_less_5us, sync_events.size(), true) << std::endl;
	std::cout << "<  10µs : " << countformat(num_less_10us, sync_events.size(), true) << std::endl;
	std::cout << "<  20µs : " << countformat(num_less_20us, sync_events.size(), true) << std::endl;
	std::cout << "< 100µs : " << countformat(num_less_100us, sync_events.size(), true) << std::endl;


}
