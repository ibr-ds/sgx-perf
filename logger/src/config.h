/**
 * @file config.h
 * @author weichbr
 */

#ifndef SGX_PERF_CONFIG_H
#define SGX_PERF_CONFIG_H

namespace sgxperf
{
	/**
	 * @brief Config class
	 */
	class Config
	{
	public:
		Config() : trace_paging(false), record_samples(false), count_aex(false), trace_aex(false), benchmode(false) {};
		~Config() = default;
		void init();

		/**
		 * @brief
		 * @return true, if sampling is enabled, false otherwise
		 */
		bool is_sampling_enabled() { return record_samples; }

		/**
		 * @brief
		 * @return true, if tracing is enabled, false otherwise
		 */
		bool is_tracing_enabled() { return trace_paging; }

		/**
		 * @brief
		 * @return false, if neither sampling nor tracing are enabled, true otherwise
		 */
		bool is_sampling_or_tracing_enabled() { return record_samples || trace_paging; }

		/**
		 * @brief
		 * @return true, if AEX counting is enabled, false otherwise
		 */
		bool is_aex_counting_enabled() { return count_aex; }

		/**
		 * @brief Implies active AEX counting.
		 * @return true, if AEX tracing is enabled, false otherwise.
		 */
		bool is_aex_tracing_enabled() { return trace_aex; }

		/**
		 * @brief
		 * @return true, if benchmark mode is enabled, false otherwise.
		 */
		bool is_benchmark_mode_enabled() { return benchmode; }
	private:
		bool trace_paging;
		bool record_samples;
		bool count_aex;
		bool trace_aex;
		bool benchmode;
	};
}

#endif //SGX_PERF_CONFIG_H
