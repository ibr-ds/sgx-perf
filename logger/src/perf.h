/**
 * @file perf.h
 * @author weichbr
 */

#ifndef SGX_PERF_PERF_H
#define SGX_PERF_PERF_H

#include <cstdint>
#include <thread>
#include <linux/perf_event.h>

extern "C" struct __perf_sample_event
{
	struct perf_event_header header; // 4+2+2 = 8
	uint64_t rip;                    // 8
	uint32_t pid, tid;               // 4+4 = 8
};
typedef struct __perf_sample_event perf_sample_event_t;

namespace sgxperf
{
	/**
	 * @brief Class for tracing and sampling events.
	 */
	class Perf
	{
	public:
		Perf() : perf_s_fd(-1), perf_kprobe_fd(-1), select_max_fd(-1), sample_buffer(nullptr), sample_collector(nullptr){}
		~Perf() = default;
		void init();

		void start_sampling();
		void stop_sampling();
	private:
		int perf_s_fd;
		int perf_kprobe_fd;
		int select_max_fd;
		void *sample_buffer;
		std::thread *sample_collector;
		std::string kprobe_path;

		void sampler_thread();
		void sample_poll();
		void tracer_poll();
	};
}

#endif //SGX_PERF_PERF_H
