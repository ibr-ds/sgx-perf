/**
 * @author weichbr
 */

#include <iostream>
#include <algorithm>
#include <numeric>
#include <map>
#include <atomic>
#include <cstring>
#include <cassert>
#include "main.h"

#include <fstream>
#include <sys/stat.h>

std::map<uint64_t, enclave_data_t> encls;
std::map<uint64_t, thread_t> threads;
general_data_t general_data = {};

static int general_callback(void *arg, int count, char **data, char **columns)
{
	(void)arg;
	(void)count;
	(void)columns;

	if (strcmp(data[0], "start_time") == 0)
	{
		general_data.starttime = strtoul(data[1], nullptr, 10);
	}
	else if (strcmp(data[0], "end_time") == 0)
	{
		general_data.endtime = strtoul(data[1], nullptr, 10);
	}
	else if (strcmp(data[0], "main_thread") == 0)
	{
		general_data.main_thread = strtoul(data[1], nullptr, 10);
	}

	return 0;
}

static int ecalls_callback(void *arg, int count, char **data, char **columns)
{
	(void)arg;
	(void)count;
	(void)columns;
	uint64_t id = strtoul(data[0], nullptr, 10);
	uint64_t eid = strtoul(data[1], nullptr, 10);

	auto *c = new call_data_t;
	c->type = call_type_t::ECALL;
	c->call_id = id;
	c->name = new std::string(data[2]);
	c->all_stats = {};
	c->stats_95th = {};
	c->exectimes = new std::vector<uint64_t>();
	c->single_calls = new std::vector<single_call_data_t *>();
	c->has_direct_parents = false;
	c->num_ecall_called_from_ocalls = 0;
	c->direct_parents_data = nullptr;
	c->has_indirect_parents = false;
	c->indirect_parents_data = nullptr;
	c->aex_counts = new std::vector<uint64_t>();
	encls[eid].ecalls.push_back(c);
	encls[eid].ecall_count = 0;

	return 0;
}

static int ocalls_callback(void *arg, int count, char **data, char **columns)
{
	(void)arg;
	(void)count;
	(void)columns;
	uint64_t id = strtoul(data[0], nullptr, 10);
	uint64_t eid = strtoul(data[1], nullptr, 10);

	auto *c = new call_data_t;
	c->type = call_type_t::OCALL;
	c->call_id = id;
	c->name = new std::string(data[2]);
	c->all_stats = {};
	c->stats_95th = {};
	c->exectimes = new std::vector<uint64_t>();
	c->single_calls = new std::vector<single_call_data_t *>();
	c->has_direct_parents = false;
	c->num_ecall_called_from_ocalls = 0;
	c->direct_parents_data = nullptr;
	c->has_indirect_parents = false;
	c->indirect_parents_data = nullptr;
	c->aex_counts = nullptr;
	encls[eid].ocalls.push_back(c);
	encls[eid].ocall_count = 0;

	return 0;
}

static int thread_callback(void *arg, int count, char **data, char **columns)
{
	(void) arg;
	(void) count;
	(void) columns;
	uint64_t id = strtoul(data[0], nullptr, 10);
	uint64_t pthread_id = strtoul(data[1], nullptr, 10);
	uint64_t events = strtoul(data[2], nullptr, 10);
	threads[id].id = id;
	threads[id].pthread_id = pthread_id;
	threads[id].calls = new single_call_data_t[events];
	threads[id].last_call = nullptr;
	threads[id].next_call_index = 0;
	return 0;
}

static int call_data_callback(void *arg, int count, char **data, char **columns)
{
	(void)arg;
	(void)count;
	(void)columns;
	// 0 = internal id
	// 1 = type
	// 2 = thread
	// 3 = call id
	// 4 = eid
	// 5 = exectime
	// 6 = aex count
	// 7 = parent call id
	// 8 = start time
	// 9 = end time
	uint64_t iid = strtoul(data[0], nullptr, 10);
	uint64_t type = strtoul(data[1], nullptr, 10);
	uint64_t tid = strtoul(data[2], nullptr, 10);
	uint64_t id = strtoul(data[3], nullptr, 10);
	uint64_t eid = strtoul(data[4], nullptr, 10);
	uint64_t exectime = strtoul(data[5], nullptr, 10);
	uint64_t starttime = strtoul(data[8], nullptr, 10);
	uint64_t endtime = strtoul(data[9], nullptr, 10);
	if (type == 15) // FIXME: use event_map table to get event ids
	{
		// ecall

		auto thiz = encls[eid].ecalls[id];

		if (data[6] != nullptr)
		{
			uint64_t aex_count = strtoul(data[6], nullptr, 10);
			thiz->aex_counts->push_back(aex_count);
			thiz->all_stats.aexs += aex_count;
		}

		encls[eid].first_ecall_start = encls[eid].first_ecall_start > starttime ? starttime : encls[eid].first_ecall_start;
		encls[eid].last_ecall_end = encls[eid].last_ecall_end < endtime ? endtime : encls[eid].last_ecall_end;

		thiz->exectimes->push_back(exectime);
		thiz->all_stats.sum += exectime;

		single_call_data_t scd = {};
		scd.event_id = iid;
		scd.call_id = id;
		scd.type = call_type_t::ECALL;
		scd.start = starttime;
		scd.end = endtime;
		scd.exec = exectime;
		scd.parent = nullptr;
		auto &t = threads[tid];

		if (data[7] != nullptr)
		{
			uint64_t parent_event_id = strtoul(data[7], nullptr, 10);
			for (uint64_t i = t.next_call_index - 1; ; --i)
			{
				if (t.calls[i].event_id == parent_event_id)
				{
					auto &p = t.calls[i];
					//thiz->direct_parents->push_back(t.calls[i].call_id);
					auto &dpcd = thiz->direct_parents_data->at(p.call_id);
					thiz->has_direct_parents = true;
					thiz->num_ecall_called_from_ocalls++;
					dpcd.count++;
					dpcd.call_data = encls[eid].ocalls[p.call_id];
					auto timediff_start  = scd.start - p.start;
					auto timediff_end = p.end - scd.end;
					if (timediff_start < 10000)
						dpcd.num_less_than_10us_from_start++;
					else if (timediff_start < 20000)
						dpcd.num_less_than_20us_from_start++;
					if (timediff_end < 10000)
						dpcd.num_less_than_10us_from_end++;
					else if (timediff_end < 20000)
						dpcd.num_less_than_20us_from_end++;

					scd.parent = &(t.calls[i]);
					break;
				}
				else
				{
					assert(i != 0); // If i == 0 then we did not find a parent
				}
			}
		}

		if (t.next_call_index != 0)
		{
			// Find the next ecall that is on the same level
			auto c = &(t.calls[t.next_call_index - 1]);
			while (c && c > scd.parent)
			{
				if (c->type == call_type_t::ECALL)
				{
					if (c->parent == scd.parent)
					{
						// scd:  single_call_data of this call
						// thiz: call_data of this call
						// c:    single_call_data of the indirect parent (predecessor call of same type on same level)
						// ipcd: parent_call_data of the indirect parent (but referenced by this call)
						uint64_t timediff = scd.start - c->end;
						auto &ipcd = thiz->indirect_parents_data->at(c->call_id);

						thiz->has_indirect_parents = true;
						ipcd.call_data = encls[eid].ecalls[c->call_id];
						ipcd.count++;

						if (timediff < 1000)
							ipcd.num_less_1us++;
						else if (timediff < 5000)
							ipcd.num_less_5us++;
						else if (timediff < 10000)
							ipcd.num_less_10us++;
						else if (timediff < 20000)
							ipcd.num_less_20us++;
						break;
					}
					c = c->parent ? c->parent - 1 : nullptr;
					continue;
				}
				else if (c->type == call_type_t::OCALL)
				{
					c = c->parent;
					continue;
				}
			}
		}

		t.calls[t.next_call_index] = scd;
		t.next_call_index++;

		thiz->single_calls->push_back(&t.calls[t.next_call_index]);
	}
	else if (type == 17)
	{
		// ocall
		auto thiz = encls[eid].ocalls[id];
		thiz->exectimes->push_back(exectime);
		thiz->all_stats.sum += exectime;

		single_call_data_t scd = {};
		scd.event_id = iid;
		scd.call_id = id;
		scd.type = call_type_t::OCALL;
		scd.start = starttime;
		scd.end = endtime;
		scd.exec = exectime;
		scd.parent = nullptr;
		auto &t = threads[tid];

		if (data[7] != nullptr)
		{
			uint64_t parent_event_id = strtoul(data[7], nullptr, 10);
			for (uint64_t i = t.next_call_index - 1; ; --i)
			{
				if (t.calls[i].event_id == parent_event_id)
				{
					auto &p = t.calls[i];
					//thiz->direct_parents->push_back(t.calls[i].call_id);
					auto &dpcd = thiz->direct_parents_data->at(p.call_id);
					thiz->has_direct_parents = true;
					dpcd.count++;
					dpcd.call_data = encls[eid].ecalls[p.call_id];
					auto timediff_start  = scd.start - p.start;
					auto timediff_end = p.end - scd.end;
					if (timediff_start < 10000)
						dpcd.num_less_than_10us_from_start++;
					else if (timediff_start < 20000)
						dpcd.num_less_than_20us_from_start++;
					if (timediff_end < 10000)
						dpcd.num_less_than_10us_from_end++;
					else if (timediff_end < 20000)
						dpcd.num_less_than_20us_from_end++;

					scd.parent = &(t.calls[i]);
					break;
				}
				else
				{
					assert(i != 0); // If i == 0 then we did not find a parent
				}
			}
		}

		if (t.next_call_index != 0)
		{
			// Find the next ocall that is on the same level
			auto c = &(t.calls[t.next_call_index - 1]);
			while (c && c > scd.parent)
			{
				if (c->type == call_type_t::OCALL)
				{
					if (c->parent == scd.parent)
					{
						uint64_t timediff = scd.start - c->end;
						auto &pcd = thiz->indirect_parents_data->at(c->call_id);
						thiz->has_indirect_parents = true;
						pcd.call_data = encls[eid].ocalls[c->call_id];
						pcd.count++;
						if (timediff < 1000)
							pcd.num_less_1us++;
						else if (timediff < 5000)
							pcd.num_less_5us++;
						else if (timediff < 10000)
							pcd.num_less_10us++;
						else if (timediff < 20000)
							pcd.num_less_20us++;
						break;
					}
					c = c->parent ? c->parent - 1 : nullptr;
					continue;
				}
				else if (c->type == call_type_t::ECALL)
				{
					c = c->parent;
					continue;
				}
			}
		}

		t.calls[t.next_call_index] = scd;
		t.next_call_index++;

		thiz->single_calls->push_back(&t.calls[t.next_call_index]);
	}

	return 0;
}

static void calc_stats(stats_t &s, call_data_t *c)
{
	if (s.calls > 0)
	{
		s.avg = s.sum / s.calls;

		std::vector<int64_t> diff(s.calls);
		std::transform(c->exectimes->begin(), c->exectimes->begin() + s.calls, diff.begin(), [&s](uint64_t x) { return x - s.avg; });
		s.sq_sum = (uint64_t)std::inner_product(diff.begin(), diff.end(), diff.begin(), 0L);

		s.num_less_1us = static_cast<uint64_t>(std::count_if(c->exectimes->begin(), c->exectimes->begin() + s.calls, [](uint64_t val) { return val < 1000;}));
		s.num_less_5us = static_cast<uint64_t>(std::count_if(c->exectimes->begin(), c->exectimes->begin() + s.calls, [](uint64_t val) { return val < 5000;}));
		s.num_less_10us = static_cast<uint64_t>(std::count_if(c->exectimes->begin(), c->exectimes->begin() + s.calls, [](uint64_t val) { return val < 10000;}));

		s.std = (uint64_t)std::sqrt(s.sq_sum / s.calls);
	}
}

static void calc_aex_stats(stats_t &s, call_data_t *c)
{
	s.calls = c->aex_counts->size();
	if (s.calls == 0)
	{
		return;
	}

	s.sum = std::accumulate(c->aex_counts->begin(), c->aex_counts->begin() + s.calls, 0UL);
	s.min = *std::min_element(c->aex_counts->begin(), c->aex_counts->begin() + s.calls);
	s.max = *std::max_element(c->aex_counts->begin(), c->aex_counts->begin() + s.calls);

	if (c->aex_counts != nullptr)
	{
		s.avg = s.sum / s.calls;
		std::vector<int64_t> diff(s.calls);
		std::transform(c->aex_counts->begin(), c->aex_counts->begin() + s.calls, diff.begin(), [&s](uint64_t x) { return x - s.avg; });
		s.sq_sum = (uint64_t)std::inner_product(diff.begin(), diff.end(), diff.begin(), 0L);
		s.std = (uint64_t)std::sqrt(s.sq_sum / s.calls);
	}
}

static bool batch_opportunity(parent_call_data_t &pcd)
{
	// check for batching opportunities

	auto c = pcd.call_data;

	auto r = (pcd.count / (double)c->all_stats.calls);
	if (r <= config.batching_weights.lambda)
	{
		// If the call was only a indirect parent less than 35% of the time, then don't consider it for batching
		return false;
	}

	auto w = 0.0;
	w += (pcd.num_less_1us / (double)pcd.count) * config.batching_weights.alpha;
	w += (pcd.num_less_5us / (double)pcd.count) * config.batching_weights.beta;
	w += (pcd.num_less_10us / (double)pcd.count) * config.batching_weights.gamma;
	w += (pcd.num_less_20us / (double)pcd.count) * config.batching_weights.delta;

	// If the weight is more than 0.5 than we think batching is good
	return w > config.batching_weights.epsilon;
}

static bool merge_opportunity(parent_call_data_t &pcd, call_data_t *c)
{

	// TODO: we also must take timediff between actual call and indirect parent into account (c.start - ipcd.end) and look at those counts + their execution times
	// TODO: so it should be a combination of reordering + the current merging/batching

	auto r = (pcd.count / (double)c->all_stats.calls);
	if (r <= config.merging_weights.lambda)
	{
		// If the call was only an indirect parent less than 35% of the time, then don't consider it for merging
		return false;
	}

	auto w = 0.0;
	w += (pcd.num_less_1us / (double)pcd.count) * config.merging_weights.alpha;
	w += (pcd.num_less_5us / (double)pcd.count) * config.merging_weights.beta;
	w += (pcd.num_less_10us / (double)pcd.count) * config.merging_weights.gamma;
	w += (pcd.num_less_20us / (double)pcd.count) * config.merging_weights.delta;

	// If the weight is more than 0.5 than we think batching is good
	return w > config.merging_weights.epsilon;
}

static bool duplication_or_move_opportunity(call_data_t *c)
{
	auto r = (c->stats_95th.num_less_1us / (double) c->stats_95th.calls) > config.duplication_weights.alpha;
	r = r || (c->stats_95th.num_less_5us / (double) c->stats_95th.calls) > config.duplication_weights.beta;
	r = r || (c->stats_95th.num_less_10us / (double) c->stats_95th.calls) > config.duplication_weights.gamma;

	return r;
}

static bool reorder_start_opportunity(parent_call_data_t &pcd)
{
	auto w = 0.0;
	w += (pcd.num_less_than_10us_from_start / (double)pcd.count) * config.reordering_weights.alpha;
	w += (pcd.num_less_than_20us_from_start / (double)pcd.count) * config.reordering_weights.beta;

	return w > config.reordering_weights.gamma;
}

static bool reorder_end_opportunity(parent_call_data_t &pcd)
{
	auto w = 0.0;
	w += (pcd.num_less_than_10us_from_end / (double)pcd.count) * config.reordering_weights.alpha;
	w += (pcd.num_less_than_20us_from_end / (double)pcd.count) * config.reordering_weights.beta;

	return w > config.reordering_weights.gamma;
}

void print_call_data(enclave_data_t &e, call_data_t *c, uint64_t print_min)
{
	if (c->all_stats.calls < print_min)
	{
		return;
	}
	std::cout << "| / " << WHITE() << "[" << c->call_id << "] " << *c->name << NORMAL() << std::endl;
	if (c->type == call_type_t::ECALL)
		std::cout << "| | Calls: " << countformat(c->all_stats.calls, e.ecall_count) << std::endl;
	if (c->type == call_type_t::OCALL)
		std::cout << "| | Calls: " << countformat(c->all_stats.calls, e.ocall_count) << std::endl;
	if (c->all_stats.calls > 0)
	{
		std::cout << "| | Overall duration: " << timeformat(c->all_stats.sum, true) << std::endl;
		std::cout << "| | Ø duration: " << timeformat(c->all_stats.avg, true) << " ± "
		          << timeformat(c->all_stats.std, true) << std::endl;
		std::cout << "| | Longest call took " << timeformat(c->exectimes->at(c->exectimes->size() - 1), true)
		          << std::endl;
		if (c->type == call_type_t::ECALL)
		{
			std::cout << "| | # called directly: " << countformat(c->all_stats.calls - c->num_ecall_called_from_ocalls, c->all_stats.calls) << std::endl;
			std::cout << "| | # called from ocall: " << countformat(c->num_ecall_called_from_ocalls, c->all_stats.calls) << std::endl;
			if (c->num_ecall_called_from_ocalls == c->all_stats.calls)
			{
				std::cout << "| | \\ " << YELLOW() << "/!\\ Call can be made private." << NORMAL() << std::endl;
			}
		}
		if (c->type == call_type_t::OCALL)
			std::cout << "| | # < 1µs: " << countformat(c->all_stats.num_less_1us, c->all_stats.calls, true) << std::endl;
		std::cout << "| | # < 5µs: " << countformat(c->all_stats.num_less_5us, c->all_stats.calls, true) << std::endl;
		std::cout << "| | # < 10µs: " << countformat(c->all_stats.num_less_10us, c->all_stats.calls, true) << std::endl;
		if (c->type == call_type_t::OCALL)
		{
			auto w = duplication_or_move_opportunity(c);
			if (w)
			{
				std::cout << "| | " << YELLOW() << "/!\\ Duplicate or move this OCall into the enclave" << NORMAL() << std::endl;
			}
		}
		std::cout << "| |" << std::endl;
		std::cout << "| | 50% of calls are faster than "
		          << timeformat(c->exectimes->at(percentile_idx(50 / 100.0, c->exectimes))) << std::endl;
		std::cout << "| | 75% of calls are faster than "
		          << timeformat(c->exectimes->at(percentile_idx(75 / 100.0, c->exectimes))) << std::endl;
		std::cout << "| | 95% of calls are faster than "
		          << timeformat(c->exectimes->at(percentile_idx(95 / 100.0, c->exectimes))) << std::endl;
		std::cout << "| | | Ø duration: " << timeformat(c->stats_95th.avg, true) << " ± "
		          << timeformat(c->stats_95th.std, true) << std::endl;
		if (c->type == call_type_t::OCALL)
			std::cout << "| | | # < 1µs: " << countformat(c->stats_95th.num_less_1us, c->stats_95th.calls, true) << std::endl;
		std::cout << "| | | # < 5µs: " << countformat(c->stats_95th.num_less_5us, c->stats_95th.calls, true) << std::endl;
		std::cout << "| | | # < 10µs: " << countformat(c->stats_95th.num_less_10us, c->stats_95th.calls, true) << std::endl;
		if (c->type == call_type_t::OCALL || c->num_ecall_called_from_ocalls > 0)
		{
			std::cout << "| |" << std::endl;
			std::cout << "| | Direct successor of" << std::endl;
			std::for_each(c->direct_parents_data->begin(), c->direct_parents_data->end(), [c](parent_call_data_t &pc) {
				if (pc.call_data == nullptr)
				{
					return;
				}
				std::cout << "| | | " << WHITE() << "[" << pc.call_data->call_id << "] " << *pc.call_data->name
				          << NORMAL() << " " << countformat(pc.count, c->all_stats.calls) << std::endl;
				std::cout << "| | | | # < 10µs from start: " << countformat(pc.num_less_than_10us_from_start, pc.count) << std::endl;
				std::cout << "| | | | # < 20µs from start: " << countformat(pc.num_less_than_20us_from_start, pc.count) << std::endl;
				if (reorder_start_opportunity(pc))
				{
					std::cout << "| | | | " << YELLOW() << "/!\\ Reorder [" << c->call_id << "] to execute before call to [" << pc.call_data->call_id << "]" << NORMAL() << std::endl;
				}
				std::cout << "| | | | # < 10µs from end: " << countformat(pc.num_less_than_10us_from_end, pc.count) << std::endl;
				std::cout << "| | | | # < 20µs from end: " << countformat(pc.num_less_than_20us_from_end, pc.count) << std::endl;
				if (reorder_end_opportunity(pc))
				{
					std::cout << "| | | | " << YELLOW() << "/!\\ Reorder [" << c->call_id << "] to execute after call to [" << pc.call_data->call_id << "]" << NORMAL() << std::endl;
				}
				std::cout << "| | |" << std::endl;
			});
		}

		if (c->type == call_type_t::ECALL && !c->aex_counts->empty())
		{
			std::cout << "| |" << std::endl;
			std::cout << "| | # AEX during all calls: " << c->aex_stats.sum << std::endl;
			std::cout << "| | Ø AEX count per call: " << c->aex_stats.avg << " ± " << c->aex_stats.std << std::endl;
			std::cout << "| | Highest AEX count: " << c->aex_stats.max << std::endl;
			std::cout << "| | Lowest AEX count: " << c->aex_stats.min << std::endl;
		}

		if (c->has_indirect_parents)
		{
			std::cout << "| |" << std::endl;
			std::cout << "| | Indirect successor of" << std::endl;
			std::for_each(c->indirect_parents_data->begin(), c->indirect_parents_data->end(), [c](parent_call_data_t &pc) {
				if (pc.call_data == nullptr)
				{
					return;
				}
				std::cout << "| | | " << ((pc.call_data == c) ? CYAN() : WHITE()) << "[" << pc.call_data->call_id << "] " << *pc.call_data->name
				          << NORMAL() << " " << countformat(pc.count, c->all_stats.calls) << std::endl;
				std::cout << "| | | | # < 1µs: " << countformat(pc.num_less_1us, pc.count) << std::endl;
				std::cout << "| | | | # < 5µs: " << countformat(pc.num_less_5us, pc.count) << std::endl;
				std::cout << "| | | | # < 10µs: " << countformat(pc.num_less_10us, pc.count) << std::endl;
				std::cout << "| | | | # < 20µs: " << countformat(pc.num_less_20us, pc.count) << std::endl;
				if (pc.call_data == c)
				{
					// This is the same call
					if (batch_opportunity(pc))
						// If the weight is more than 0.5 than we think batching is good
						std::cout << "| | | | " << YELLOW() << "/!\\ Batching opportunity" << NORMAL() << std::endl;
				}
				else
				{
					// This is a different call
					if (merge_opportunity(pc, c))
						std::cout << "| | | | " << YELLOW() << "/!\\ Merging opportunity" << NORMAL() << std::endl;
				}

				std::cout << "| | |" << std::endl;
			});
		}
	}
	std::cout << "| \\ ___" << std::endl;
	std::cout << "|" << std::endl;
}

void export_call_data_histogram(call_data_t *c, uint8_t percentile)
{
	if (config.call_data_filename.empty())
	{
		return;
	}

	if (c->type == call_type_t::ECALL && skip_call(c, config.ecall_set))
	{
		return;
	}

	if (c->type == call_type_t::OCALL && skip_call(c, config.ocall_set))
	{
		return;
	}

	int res = mkdir(config.call_data_filename.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	if (res != 0)
	{
		int e = errno;
		if (e != EEXIST)
		{
			std::cout << "/!\\Error creating folder: " << strerror(e) << std::endl;
			return;
		}
	}

	std::stringstream ss;
	ss << config.call_data_filename;
	ss << "/";
	ss << *c->name;
	ss << "_";
	ss << (int)percentile;
	ss << "_hist.dat";
	std::ofstream datafile(ss.str());

	uint64_t size = percentile_idx(percentile / 100.0f, c->exectimes);
	auto maxit = std::max_element(c->exectimes->begin(), c->exectimes->begin() + size);
	auto max = *maxit;
	auto minit = std::min_element(c->exectimes->begin(), c->exectimes->begin() + size);
	auto min = *minit;

	// we want 100 bins from min to max
	uint64_t bins = 100;
	if (max - min < 100)
		bins = max - min;
	if (bins == 0)
		bins = 1;
	uint64_t binwidth = ((max - min) / bins)+1;
	uint64_t bin[bins+1] = {};

	for (uint64_t i = 0; i < size; ++i)
	{
		auto val = (*(c->exectimes))[i];
		assert(val <= max);
		auto n = (val - min) / binwidth;
		assert (n < bins+1);
		bin[n]++;
	}

	for (uint64_t i = 0; i < bins+1; ++i)
	{
		datafile << min + i*binwidth << "," << bin[i] << std::endl;
	}

	datafile.close();
}

void export_call_data_scatter(call_data_t *c, uint8_t percentile)
{
	if (config.call_data_filename.empty())
	{
		return;
	}

	if (c->type == call_type_t::ECALL && skip_call(c, config.ecall_set))
	{
		return;
	}

	if (c->type == call_type_t::OCALL && skip_call(c, config.ocall_set))
	{
		return;
	}

	int res = mkdir(config.call_data_filename.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	if (res != 0)
	{
		int e = errno;
		if (e != EEXIST)
		{
			std::cout << "/!\\Error creating folder: " << strerror(e) << std::endl;
			return;
		}
	}

	std::stringstream ss;
	ss << config.call_data_filename;
	ss << "/";
	ss << *c->name;
	ss << "_";
	ss << (int)percentile;
	ss << "_scatter.dat";
	std::ofstream datafile(ss.str());

	uint64_t size = percentile_idx(percentile / 100.0f, c->exectimes);
	auto maxit = std::max_element(c->exectimes->begin(), c->exectimes->begin() + size);
	auto max = *maxit;
	auto minit = std::min_element(c->exectimes->begin(), c->exectimes->begin() + size);
	auto min = *minit;

	auto it = c->single_calls->begin();

	while (it != c->single_calls->end())
	{
		auto scd = *it;
		if (scd->exec > max || scd->exec < min)
		{
			++it;
			continue;
		}

		// we want csv with "timestamp,exectime" format
		datafile << scd->end - general_data.starttime << "," << scd->exec << std::endl;

		++it;
	}

	datafile.close();
}

void analyze_calls()
{
	std::stringstream ss;

	ss << "select key, value from general order by key asc;";
	sql_exec(ss, general_callback);

	std::cout << "=== General Info" << std::endl;

	std::cout << "Runtime: " << timeformat(general_data.endtime - general_data.starttime, true);
	std::cout << std::endl;

	std::cout << "=== Analyzing ECalls/OCalls" << std::endl;

	std::cout << "iii Loading ecall symbols" << std::endl << std::flush;

	ss << "select id, eid, symbol_name from ecalls order by id asc;";
	sql_exec(ss, ecalls_callback);

	std::cout << "iii Loading ocall symbols" << std::endl << std::flush;

	ss << "select id, eid, symbol_name from ocalls order by id asc;";
	sql_exec(ss, ocalls_callback);

	parallel_for_each(encls.begin(), encls.end(), [] (std::pair<const uint64_t, enclave_data_t> &p) {
		auto &e = encls[p.first];
		std::for_each(e.ecalls.begin(), e.ecalls.end(), [&e] (call_data_t *c) {
			c->direct_parents_data = new std::vector<parent_call_data_t>(e.ocalls.size());
			c->indirect_parents_data = new std::vector<parent_call_data_t>(e.ecalls.size());
		});
		std::for_each(e.ocalls.begin(), e.ocalls.end(), [&e] (call_data_t *c) {
			c->direct_parents_data = new std::vector<parent_call_data_t>(e.ecalls.size());
			c->indirect_parents_data = new std::vector<parent_call_data_t>(e.ocalls.size());
		});
	});

	std::cout << "iii Loading threads" << std::endl << std::flush;

	// processing threads
	ss << "select t.id, t.pthread_id, count(e.id) as events from events as e inner join threads as t on e.involved_thread = t.id inner join events as s on e.call_event = s.id group by t.id order by t.id asc";
	sql_exec(ss, thread_callback);

	std::cout << "iii Loading calls" << std::endl << std::flush;

	// Processing calls
	ss << "select s.id, e.type, s.involved_thread as thread, s.call_id, s.eid, e.time-s.time as exectime, e.aex_count, s.call_event as parent_call, s.time as starttime, e.time as endtime from events as e inner join events as s on s.id = e.call_event where e.type = 15 or e.type = 17 order by s.involved_thread, s.time asc;";
	sql_exec(ss, call_data_callback);

	std::cout << "iii Generating statistics" << std::endl << std::flush;

	parallel_for_each(encls.begin(), encls.end(), [](std::pair<const uint64_t, enclave_data_t> &p) {
		auto &e = encls[p.first];
		//auto e = p.second;
		std::atomic<uint64_t> ecall_count(0);
		parallel_for_each(e.ecalls.begin(), e.ecalls.end(), [&ecall_count] (call_data_t *c) {
			c->all_stats.calls = c->exectimes->size();
			ecall_count += c->all_stats.calls;

			std::sort(c->exectimes->begin(), c->exectimes->end());
			calc_stats(c->all_stats, c);
			calc_aex_stats(c->aex_stats, c);

			c->stats_95th.calls = percentile_idx(95 / 100.0, c->exectimes);
			c->stats_95th.sum = std::accumulate(c->exectimes->begin(), c->exectimes->begin() + c->stats_95th.calls, 0UL);
			calc_stats(c->stats_95th, c);
		});
		e.ecall_count = ecall_count;

		std::atomic<uint64_t> ocall_count(0);
		parallel_for_each(e.ocalls.begin(), e.ocalls.end(), [&ocall_count] (call_data_t *c) {
			c->all_stats.calls = c->exectimes->size();
			ocall_count += c->all_stats.calls;

			std::sort(c->exectimes->begin(), c->exectimes->end());
			calc_stats(c->all_stats, c);

			c->stats_95th.calls = percentile_idx(95 / 100.0, c->exectimes);
			c->stats_95th.sum = std::accumulate(c->exectimes->begin(), c->exectimes->begin() + c->stats_95th.calls, 0UL);
			calc_stats(c->stats_95th, c);
		});
		e.ocall_count = ocall_count;


	});

	std::cout << "iii Sorting" << std::endl << std::flush;

	// Sort
	parallel_for_each(encls.begin(), encls.end(), [](std::pair<const uint64_t, enclave_data_t> &p) {
		auto &e = encls[p.first];
		e.ecalls_sorted = e.ecalls;
		e.ocalls_sorted = e.ocalls;
		std::sort(e.ecalls_sorted.begin(), e.ecalls_sorted.end(), [](call_data_t *a, call_data_t *b) { return a->all_stats.calls > b->all_stats.calls; });
		std::sort(e.ocalls_sorted.begin(), e.ocalls_sorted.end(), [](call_data_t *a, call_data_t *b) { return a->all_stats.calls > b->all_stats.calls; });
	});

	// And print
	std::cout << "(i) General statistics" << std::endl;
	std::for_each(encls.begin(), encls.end(), [](std::pair<const uint64_t, enclave_data_t> &p) {
		auto &e = encls[p.first];
		std::cout << "Enclave " << p.first << " (" << "..." << "): " << e.ecalls.size() << " ecalls / " << e.ocalls.size() << " ocalls" << std::endl;
		std::cout << "| " << std::count_if(e.ecalls_sorted.begin(), e.ecalls_sorted.end(), [](call_data_t *c) { return c->all_stats.calls > 0; }) << " ecalls called " << e.ecall_count << " times" << std::endl;
		std::cout << "| " << std::count_if(e.ecalls_sorted.begin(), e.ecalls_sorted.end(), [](call_data_t *c) { return c->all_stats.calls > 0; }) << " ocalls called " << e.ocall_count << " times" << std::endl;
		std::cout << "| Active time: " << timeformat(e.last_ecall_end-e.first_ecall_start, true) << std::endl;
		std::cout << "| First ecall started after " << timeformat(e.first_ecall_start-general_data.starttime, true) << std::endl;
		std::cout << "| Last ecall ended after " << timeformat(e.last_ecall_end-general_data.starttime, true) << std::endl;
	});
	std::cout << std::endl;

	std::cout << "(i) ECall statistics" << std::endl;
	std::for_each(encls.begin(), encls.end(), [](std::pair<const uint64_t, enclave_data_t> &p) {
		//auto &e = encls[p.first];
		auto eid = p.first;
		auto &e = encls[p.first];
		std::cout << "/ Enclave " << eid << std::endl;

		uint64_t all_less_5us = std::accumulate(e.ecalls_sorted.begin(), e.ecalls_sorted.end(), (uint64_t)0, [](uint64_t a, call_data_t *c) { return a + c->all_stats.num_less_5us; });
		uint64_t all_less_10us = std::accumulate(e.ecalls_sorted.begin(), e.ecalls_sorted.end(), (uint64_t)0, [](uint64_t a, call_data_t *c) { return a + c->all_stats.num_less_10us; });

		std::cout << "| " << std::endl;

		std::cout << "| # < 5µs: " << countformat(all_less_5us, e.ecall_count, true) << std::endl;
		std::cout << "| # < 10µs: " << countformat(all_less_10us, e.ecall_count, true) << std::endl;
		std::cout << "| " << std::endl;

		std::for_each(e.ecalls_sorted.begin(), e.ecalls_sorted.end(), [&e] (call_data_t *c) {
			print_call_data(e, c, config.ecall_call_minimum);
			export_call_data_histogram(c, 100);
			export_call_data_histogram(c, 99);
			export_call_data_histogram(c, 95);
			export_call_data_scatter(c, 100);
			export_call_data_scatter(c, 99);
			export_call_data_scatter(c, 95);
		});
		std::cout << "\\ ___" << std::endl;
	});
	std::cout << std::endl;

	std::cout << "(i) OCall statistics" << std::endl;
	std::for_each(encls.begin(), encls.end(), [](std::pair<const uint64_t, enclave_data_t> &p) {
		//auto &e = encls[p.first];
		auto eid = p.first;
		auto &e = encls[p.first];
		std::cout << "/ Enclave " << eid << std::endl;

		uint64_t all_less_1us = std::accumulate(e.ocalls_sorted.begin(), e.ocalls_sorted.end(), (uint64_t)0, [](uint64_t a, call_data_t *c) { return a + c->all_stats.num_less_1us; });
		uint64_t all_less_5us = std::accumulate(e.ocalls_sorted.begin(), e.ocalls_sorted.end(), (uint64_t)0, [](uint64_t a, call_data_t *c) { return a + c->all_stats.num_less_5us; });
		uint64_t all_less_10us = std::accumulate(e.ocalls_sorted.begin(), e.ocalls_sorted.end(), (uint64_t)0, [](uint64_t a, call_data_t *c) { return a + c->all_stats.num_less_10us; });

		std::cout << "| " << std::endl;
		std::cout << "| # < 1µs: " << countformat(all_less_1us, e.ocall_count, true) << std::endl;
		std::cout << "| # < 5µs: " << countformat(all_less_5us, e.ocall_count, true) << std::endl;
		std::cout << "| # < 10µs: " << countformat(all_less_10us, e.ocall_count, true) << std::endl;
		std::cout << "| " << std::endl;

		std::for_each(e.ocalls_sorted.begin(), e.ocalls_sorted.end(), [&e] (call_data_t *c) {
			print_call_data(e, c, config.ocall_call_minimum);
			export_call_data_histogram(c, 100);
			export_call_data_histogram(c, 99);
			export_call_data_histogram(c, 95);
			export_call_data_scatter(c, 100);
			export_call_data_scatter(c, 99);
			export_call_data_scatter(c, 95);
		});
		std::cout << "\\ ___" << std::endl;
	});
	std::cout << std::endl;
}
