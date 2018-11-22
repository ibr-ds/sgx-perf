/**
 * @file perf.cpp
 * @author weichbr
 */

#include "perf.h"
#include "events.h"
#include "store.h"
#include "config.h"
#include <unistd.h>
#include <sys/syscall.h>
#include <iostream>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/user.h>
#include <fcntl.h>
#include <poll.h>
#include <sstream>
#include <cstring>
#include <sys/stat.h>

#define PAGE_CNT (32)
#define BUFFER_SIZE (PAGE_CNT * PAGE_SIZE)

#define TRACE_BASE_PATH "/sys/kernel/debug/tracing/"

extern sgxperf::EventStore *event_store;
extern sgxperf::Config *config;

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
	auto ret = syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
	return ret;
}

/**
 * @brief Helper function to write to tracing files
 * @param name The file inside /sys/kernel/tracing
 * @param val The value that should be written to the file
 * @param append Whether the value should be written or appended
 * @return 0 on success, non-zero otherwise.
 */
static int __write_tracing_file(std::string &name, std::string &val, bool append)
{
	const char *file;
	int fd, ret = -1;
	ssize_t size = val.length();
	int flags = O_WRONLY;
	std::stringstream ss;
	ss << TRACE_BASE_PATH << name;

	auto s = ss.str();
	file = s.c_str();

	if (!file)
	{
		std::cout << "cannot get tracing file: " << name << std::endl;
		return -1;
	}

	if (append)
		flags |= O_APPEND;
	else
		flags |= O_TRUNC;

	fd = open(file, flags);
	if (fd < 0)
	{
		std::cout << "cannot open tracing file: " << file << ": " << strerror(errno) << std::endl;
		goto out;
	}
	if (write(fd, val.c_str(), size) == size)
		ret = 0;
	else
	{
		std::cout << "write '" << val << "' to " << file << " failed: " << strerror(errno) << std::endl;
	}

	close(fd);
	out:
	// put_tracing_file() // omitted because it just frees
	return ret;
}

/**
 * @brief Wrapper for __write_tracing_file that always writes
 * @param name The file inside /sys/kernel/tracing
 * @param val The value that should be written to the file
 * @return 0 on success, non-zero otherwise.
 */
static int write_tracing_file(std::string name, std::string val)
{
	return __write_tracing_file(name, val, false);
}

/**
 * @brief Wrapper for __write_tracing_file that always appends
 * @param name The file inside /sys/kernel/tracing
 * @param val The value that should be written to the file
 * @return 0 on success, non-zero otherwise.
 */
static int append_tracing_file(std::string name, std::string val)
{
	return __write_tracing_file(name, val, true);
}

/**
 * @brief Resets all used tracing files to default values.
 */
static void reset_tracing()
{
	write_tracing_file("current_tracer", "nop");
	write_tracing_file("tracing_on", "0");
	write_tracing_file("kprobe_events", " ");
	write_tracing_file("set_ftrace_filter", " ");
	write_tracing_file("set_ftrace_notrace", " ");
	write_tracing_file("set_graph_function", " ");
	write_tracing_file("set_graph_notrace", " ");
}

/**
 * @brief Initializes the perf system
 */
void sgxperf::Perf::init()
{
	// Check, if sampling is enabled
	if (config->is_sampling_enabled())
	{
		struct perf_event_attr pea_s = {};
		pea_s.size = sizeof(pea_s);
		pea_s.exclude_user = 0;
		pea_s.exclude_kernel = 1;
		pea_s.exclude_hv = 1;
		pea_s.disabled = 1;
		pea_s.sample_freq = 100;
		pea_s.freq = 1;
		pea_s.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID;
		pea_s.wakeup_events = 100;
		pea_s.watermark = 0;

		perf_s_fd = static_cast<int>(perf_event_open(&pea_s, 0, -1, -1, 0));
		if (perf_s_fd > select_max_fd)
		{
			select_max_fd = perf_s_fd;
		}

		if (perf_s_fd == -1)
		{
			std::cout << "/!\\ Error opening perf for samples" << std::endl;
			exit(-1);
		}

		int flags = fcntl(perf_s_fd, F_GETFL, 0);
		if (fcntl(perf_s_fd, F_SETFL, flags | O_NONBLOCK) == -1)
		{
			std::cout << "/!\\ Error setting flags" << std::endl;
			exit(-1);
		}

		sample_buffer = mmap(nullptr, (1 + PAGE_CNT) * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, perf_s_fd, 0);
		if (sample_buffer == nullptr)
		{
			std::cout << "/!\\ Error mmaping perf" << std::endl;
			exit(-1);
		}
	}

	// Check if tracing is enabled
	if (config->is_tracing_enabled())
	{
		if (write_tracing_file("trace", "0") < 0)
		{
			std::cout << "/!\\ Could not reset trace file!" << std::endl;
			exit(-1);
		}
		reset_tracing();

		// Build the krpobe instance path
		std::stringstream ss;
		ss << "instances/sgxperf-",
				ss << getpid();
		kprobe_path = ss.str();
	}
}

/**
 * @brief Method for reading out the perf sample buffer.
 */
void sgxperf::Perf::sample_poll()
{
	auto page_header = (struct perf_event_mmap_page *)sample_buffer;

	uint64_t data_head = page_header->data_head;
	asm volatile("" ::: "memory");

	while (data_head != page_header->data_tail)
	{
		auto begin = ((uint64_t)sample_buffer + PAGE_SIZE) + (page_header->data_tail % BUFFER_SIZE);
		auto event = (struct perf_event_header *)(begin);
		auto end =  ((uint64_t)sample_buffer + PAGE_SIZE) + ((page_header->data_tail + event->size) % BUFFER_SIZE);
		if (end < begin)
		{
			std::cout << "Wrap around" << std::endl;
			goto loop;
		}

		if (event->misc != PERF_RECORD_MISC_USER)
			goto loop;

		if (event->type == PERF_RECORD_SAMPLE)
		{
			//auto s = (perf_sample_event_t *)event;
			//std::cout << "\t" << "RIP: 0x" << std::hex << s->rip << " PID/TID: " << std::dec << s->pid << "/" << s->tid;
			//TODO count accesses per address
		}

		loop:
		asm volatile("" ::: "memory");
		page_header->data_tail += event->size;

		data_head = page_header->data_head;
		asm volatile("" ::: "memory");
	}
}

/**
 * @brief Method for reading out the kprobe buffer.
 */
void sgxperf::Perf::tracer_poll()
{
	char buf[4096] = {};
	while (true)
	{
		ssize_t n = read(perf_kprobe_fd, buf, sizeof(buf));
		if (n <= 0)
		{
			break;
		}
		// Parse the timestamp and address
		char *c = buf, *addr_end, *addr_start, *t_start, *t_end, *f_start, *f_end;
		uint64_t address = 0;
		while ((t_end = strstr(c, ":")) != nullptr)
		{
			// t_end now point to end of timestamp, backtrack until space is found
			t_start = t_end;
			uint64_t timestamp = 0;
			uint64_t d = 1000;
			while (*t_start != ' ')
			{
				if (*t_start >= 48 && *t_start <= 57)
				{
					timestamp += (*t_start - 48) * d;
					d *= 10;
				}
				t_start--;
				if (t_start < c)
				{
					// We have a buffer underrun, this should not happen
					break;
				}
			}
			if (t_start < c)
			{
				// Buffer underrun
				break;
			}
			// Now search for the function name
			f_start = t_end + sizeof(" sgxperffault: (");
			f_end = strstr(f_start, "+");
			if (f_end == nullptr)
			{
				c = strstr(t_end, "\n");
				continue;
			}
			// Hacky fast check what function was triggered
			if (f_start[5] == 'l')
			{
				addr_start = strstr(c, "addr=");
				address = std::strtoul(addr_start+5, &addr_end, 16);
				auto epie = new EnclavePageInEvent(UINT64_MAX, address);
				epie->set_time(timestamp);
				event_store->insert_event(epie);
			}
			else if (f_start[5] == 'w')
			{
				addr_start = strstr(c, "addr=");
				address = std::strtoul(addr_start+5, &addr_end, 16);
				auto epoe = new EnclavePageOutEvent(UINT64_MAX, address);
				epoe->set_time(timestamp);
				event_store->insert_event(epoe);
			}
			else
			{
				c = strstr(t_end, "\n");
				continue;
			}
			c = addr_end;
		}
	}
}

/**
 * @brief Our own thread that reads out the sample/kprobe buffers during execution.
 */
void sgxperf::Perf::sampler_thread()
{
	while (true)
	{
		fd_set fdset = {};
		FD_ZERO(&fdset);
		if (config->is_sampling_enabled()) FD_SET(perf_s_fd, &fdset);
		if (config->is_tracing_enabled()) FD_SET(perf_kprobe_fd, &fdset);
		int sret = select(select_max_fd + 1, &fdset, nullptr, nullptr, nullptr);
		if (sret == -1)
		{
			std::cout << "/!\\ select error " << errno << std::endl;
			return;
		}

		if (FD_ISSET(perf_kprobe_fd, &fdset))
		{
			tracer_poll();
		}
		if (FD_ISSET(perf_s_fd, &fdset))
		{
			sample_poll();
		}
	}
}

/**
 * @brief Starts the perf system.
 */
void sgxperf::Perf::start_sampling()
{
	if (config->is_sampling_enabled())
	{
		ioctl(perf_s_fd, PERF_EVENT_IOC_RESET, 0);
		ioctl(perf_s_fd, PERF_EVENT_IOC_ENABLE, 0);
	}

	if (config->is_tracing_enabled())
	{
		std::stringstream ss;
		ss << TRACE_BASE_PATH;
		ss << kprobe_path;
		std::string kprobe_full_path = ss.str();

		// Check if there already is an instance folder
		struct stat sb = {};
		if (stat(kprobe_full_path.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode))
		{
			// Then delete it
			if (rmdir(kprobe_full_path.c_str()) < 0)
			{
				std::cout << "/!\\ Could not delete " << kprobe_full_path  << ":" << strerror(errno) << std::endl;
				exit(-1);
			}
		}

		if (mkdir(kprobe_full_path.c_str(), 0766) < 0)
		{
			std::cout << "/!\\ Could not create " << kprobe_full_path << std::endl;
			exit(-1);
		}

		if (write_tracing_file(kprobe_path + "/trace_clock", "mono_raw") < 0)
		{
			std::cout << "/!\\ Could not set clock!" << std::endl;
			exit(-1);
		}

		if (write_tracing_file("kprobe_events", "p:sgxperffaul1 sgx_eldu addr=+0(%si)") < 0)
		{
			std::cout << "/!\\ Could not set kprobe filter!" << std::endl;
			exit(-1);
		}

		if (append_tracing_file("kprobe_events", "p:sgxperffaul2 sgx_ewb addr=+0(%si)") < 0)
		{
			std::cout << "/!\\ Could not set kprobe filter!" << std::endl;
			exit(-1);
		}

		if (write_tracing_file(kprobe_path + "/events/kprobes/sgxperffaul1/enable", "1") < 0)
		{
			std::cout << "/!\\ Could not enable tracer for ELDU!" << std::endl;
			exit(-1);
		}

		if (write_tracing_file(kprobe_path + "/events/kprobes/sgxperffaul2/enable", "1") < 0)
		{
			std::cout << "/!\\ Could not enable tracer for EWB!" << std::endl;
			exit(-1);
		}

		ss << "/trace_pipe";
		std::string trace_pipe_path = ss.str();
		perf_kprobe_fd = open(trace_pipe_path.c_str(), O_RDONLY);
		if (perf_kprobe_fd < 0)
		{
			std::cout << "/!\\ Could not open trace pipe!" << std::endl;
			exit(-1);
		}
		if (perf_kprobe_fd > select_max_fd)
		{
			select_max_fd = perf_kprobe_fd;
		}
		int flags = fcntl(perf_kprobe_fd, F_GETFL, 0);
		if (fcntl(perf_kprobe_fd, F_SETFL, flags | O_NONBLOCK) == -1)
		{
			std::cout << "/!\\ Error setting flags" << std::endl;
			exit(-1);
		}
	}

	if (config->is_sampling_or_tracing_enabled())
	{
		sample_collector = new std::thread([this] () { sampler_thread(); });
		pthread_setname_np(sample_collector->native_handle(), "sgxperf sample collector");
	}
}

/**
 * @brief Stops the perf system.
 */
void sgxperf::Perf::stop_sampling()
{
	if (config->is_sampling_enabled())
	{
		ioctl(perf_s_fd, PERF_EVENT_IOC_DISABLE, 0);
	}

	if (config->is_tracing_enabled())
	{
		if (write_tracing_file(kprobe_path + "/events/kprobes/sgxperffaul1/enable", "0") < 0)
		{
			std::cout << "/!\\ Could not disable tracer!" << std::endl;
		}
		if (write_tracing_file(kprobe_path + "/events/kprobes/sgxperffaul2/enable", "0") < 0)
		{
			std::cout << "/!\\ Could not disable tracer!" << std::endl;
		}
		close(perf_kprobe_fd);
		reset_tracing();

		// Delete instances folder
		std::stringstream ss;
		ss << TRACE_BASE_PATH;
		ss << kprobe_path;
		std::string kprobe_full_path = ss.str();

		if (rmdir(kprobe_full_path.c_str()) < 0)
		{
			std::cout << "/!\\ Could not delete " << kprobe_full_path << ":" << strerror(errno) << std::endl;
		}
	}
}
