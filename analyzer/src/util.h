/**
 * @author weichbr
 */

#ifndef SGX_PERF_UTIL_H
#define SGX_PERF_UTIL_H

#include <cstdint>
#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <thread>
#include <algorithm>
#include <iostream>
#include <set>
#include "calls.h"

bool hasEnding (std::string const &fullString, std::string const &ending);
template<typename T> uint64_t percentile_idx(double percentile, std::vector<T> &vec)
{
	return static_cast<uint64_t>(std::min(std::ceil(percentile * vec.size()), (double)(vec.size() - 1)));
}
template<typename T> uint64_t percentile_idx(double percentile, std::vector<T> *vec)
{
	return static_cast<uint64_t>(std::min(std::ceil(percentile * vec->size()), (double)(vec->size() - 1)));
}


std::string timeformat(uint64_t ns, bool print_ns = false);
std::string countformat(uint64_t c, uint64_t m, bool color = false);

/*
char *RED = "\x1b[31m";
char *GREEN = "\x1b[32m";
char *YELLOW = "\x1b[33m";
char *NORMAL = "\x1b[0m";
*/

constexpr char const *RED() { return "\x1b[31m"; }
constexpr char const *GREEN() { return "\x1b[32m"; }
constexpr char const *YELLOW() { return "\x1b[33m"; }
constexpr char const *BLUE() { return "\x1b[34m"; }
constexpr char const *MAGENTA() { return "\x1b[35m"; }
constexpr char const *CYAN() { return "\x1b[36m"; }
constexpr char const *WHITE() { return "\x1b[37m"; }
constexpr char const *NORMAL() { return "\x1b[0m"; }

void sql_exec(const char *sql, int (*callback)(void*,int,char**,char**) = nullptr, void *arg = nullptr);
void sql_exec(std::string const &sql, int (*callback)(void*,int,char**,char**) = nullptr, void *arg = nullptr);
void sql_exec(std::stringstream &ss, int (*callback)(void*,int,char**,char**) = nullptr, void *arg = nullptr);

bool skip_call(call_data_t *cd, std::set<uint64_t> &set);

template <typename Iterator, typename F>
class for_each_block
{
public :
	void operator()(Iterator start, Iterator end, F f) {
		//std::cout << std::this_thread::get_id() << std::endl;
		//std::this_thread::sleep_for(std::chrono::seconds(5));
		std::for_each(start, end, [&](auto& x) { f(x); });
	}
};

template <typename Iterator, typename F>
void parallel_for_each(Iterator first, Iterator last, F f)
{
	auto size = std::distance(first, last);
	if (!size)
		return;
	auto min_per_thread = 1;
	auto max_threads = (size + min_per_thread - 1) / min_per_thread;
	auto hardware_threads = (long)std::thread::hardware_concurrency();

	auto no_of_threads = std::min(max_threads, hardware_threads != 0 ? hardware_threads : 4);

	auto block_size = size / no_of_threads;

	std::vector<std::thread> vf;
	vf.reserve(no_of_threads);
	//Iterator block_start = first;
	for (int i = 0; i < (no_of_threads - 1); i++)
	{
		Iterator end = first;
		std::advance(end, block_size);
		vf.push_back(std::move(std::thread(for_each_block<Iterator, F>(),first, end,f)));
		first = end;
	}
	vf.emplace_back(for_each_block<Iterator, F>(), first, last, f);
	//std::cout << std::endl;
	//std::cout << vf.size() << std::endl;
	for(auto& x: vf)
	{
		if (x.joinable())
			x.join();
		else
			std::cout << "threads not joinable " << std::endl;
	}
}


#endif //SGX_PERF_UTIL_H
