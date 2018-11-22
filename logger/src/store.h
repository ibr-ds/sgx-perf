/**
 * @file store.h
 * @author weichbr
 */

#include <queue>
#include <list>
#include <chrono>
#include <pthread.h>
#include <mutex>
#include <memory>
#include <map>
#include <unordered_map>
#include <atomic>
#include <rwlock.h>

#include "events.h"
#include "config.h"

#ifndef SGX_PERF_STORE_H
#define SGX_PERF_STORE_H

namespace sgxperf
{
	/**
	 * @brief Class representing an enclave
	 */
	class Enclave
	{
	public:
		explicit Enclave(sgx_enclave_id_t eid, void *encl_start, size_t size) : eid(eid),
		                                                                        encl_start(encl_start),
		                                                                        size(size),
		                                                                        orig_table(nullptr),
		                                                                        subst_ocall_table(nullptr),
		                                                                        creation_time(0),
		                                                                        destruction_time(UINT64_MAX)
		{
			encl_end = (void *)((uint64_t)encl_start + size);
		}
		virtual ~Enclave() = default;

		/**
		 * @brief Checks, whether the specified address lies within this enclave
		 * @param addr An address
		 * @return true, if the address is within this enclave, false otherwise
		 */
		bool is_within_enclave(void *addr)
		{
			return encl_start <= addr && addr <= encl_end;
		}

		/**
		 * @brief Checks, whether the given timestamp is within the lifetime of this enclave
		 * @param time A timestamp
		 * @return
		 */
		bool is_within_lifetime(uint64_t time)
		{
			return creation_time <= time && time <= destruction_time;
		}

		sgx_enclave_id_t eid; ///< The id of this enclave
		void *encl_start; ///< Start address of this enclave
		void *encl_end; ///< End address of this enclave
		size_t size; ///< Size of this enclave
		struct ocall_table *orig_table; ///< Pointer to the original OCall table of this enclave
		struct ocall_table *subst_ocall_table; ///< Pointer to our interceptor OCall table for this enclave
		uint64_t creation_time; ///< Timestamp of the EnclaveCreationEvent
		uint64_t destruction_time; ///< Timestamp of the EnclaveDestructionEvent. Can be UINT64_MAX to indicate that the enclave has not been destroyed yet.
	};

	/**
	 * @brief Class representing a thread
	 */
	class Thread
	{
	public:
		explicit Thread(pthread_t id, uint64_t uid) : id(id),
		                                              sql_id(uid),
		                                              current_call(nullptr),
		                                              last_enclave(nullptr),
		                                              name(""),
		                                              events() {}
		virtual ~Thread() = default;
		pthread_t id; ///< pthread id of the thread
		uint64_t sql_id; ///< SQL id of the thread
		EnclaveCallEvent *current_call; ///< Pointer to the current E/OCall this thread is in.
		Enclave *last_enclave; ///< Pointer to an Enclave object representing the last enclave that has been entered by this thread.
		std::string name; ///< The name of this thread.
		std::queue<Event *> events; ///< All events associated with this thread.
	private:
	};

	/**
	 * @brief The event store that manages all events.
	 */
	class EventStore
	{
	public:
		EventStore();
		virtual ~EventStore();
		void finalize();
		/**
		 * @brief Indicates, if this event store is finalized.
		 * @return true, if finalized, false otherwise
		 */
		bool is_finalized() { return finalized; }
		void insert_event(pthread_t involved_thread, Event *event);
		void insert_event(Event *event);
		void write_summary(std::string &filename);
		Thread *get_thread();
		int create_database();
		void sql_exec(const char *sql);
		void sql_exec(std::string const &sql);
		void sql_exec(std::stringstream &ss);

		rwlock_t enclave_map_lock; ///< Lock for the enclave_map
		std::unordered_map<sgx_enclave_id_t, Enclave *> enclave_map; ///< Maps enclave ids to enclaves

		rwlock_t tcs_map_lock;
		std::map<void *, EnclaveEvent *> tcs_map;
	private:
		uint64_t thread_id; ///< Counter that holds the next thread id that is to be given to a new thread
		sqlite3 *db; ///< SQLite database that holds the events
		void create_summary();
		rwlock_t thread_events_lock; ///< Read-Write lock for the thread_events map
		std::unordered_map<pthread_t, Thread *> thread_events; ///< Maps pthread ids to Thread objects
		std::list<Thread *> finished_thread_events; ///< List that stores all threads that have finished execution
		bool finalized; ///< Indicates whether the event store is finalized
		uint64_t end_time; ///< End time of the event collection
		Thread *main_thread; ///< Thread object of the main thread
		uint64_t start_time; ///< Start time of the event collection
	};
}


#endif //SGX_PERF_STORE_H
