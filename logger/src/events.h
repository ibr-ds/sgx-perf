/**
 * @file events.h
 * @author weichbr
 */

#include <sstream>
#include <chrono>
#include <memory>
#include <utility>
#include <sgx_eid.h>
#include <sgx_error.h>
#include <iostream>

#include "sqlite3.h"
#include "urts_calls.h"

#ifndef SGX_PERF_EVENTS_H
#define SGX_PERF_EVENTS_H

namespace sgxperf
{

/**
 * @brief Event type enum. Maps integers to event types for use in database.
 */
	typedef enum class __event_type
	{
		Event = 0,
		SignalEvent,
		ThreadEvent,
		ThreadCreationEvent,
		ThreadCreatorEvent,
		ThreadDestructionEvent,
		ThreadSetNameEvent,
		EnclaveEvent,
		EnclaveCreationEvent,
		EnclaveDestructionEvent,
		EnclavePagingEvent,
		EnclavePageOutEvent,
		EnclavePageInEvent,
		EnclaveCallEvent,
		EnclaveECallEvent,
		EnclaveECallReturnEvent,
		EnclaveOCallEvent,
		EnclaveOCallReturnEvent,
		EnclaveSyncWaitEvent,
		EnclaveSyncSetEvent,
		EnclaveAEXEvent,

		First = (int) Event, ///< Not a real event type but a helper to get the first element. Allows writing code that references the first element even when new types are added.
		Last = (int) EnclaveAEXEvent, ///< Not a real event type but a helper to get the last element. Allows writing code that references the last element even when new types are added.
	} EventType;

/**
 * @brief Helper operator for iterating the enum.
 * @param e The previous event type.
 * @return The next event type.
 */
	inline __event_type operator++(__event_type &e)
	{
		return e = (__event_type) (((std::underlying_type<__event_type>::type) (e) + 1));
	}

/**
 * @brief Base class for all events.
 */
	class Event
	{
	private:
		timespec temp;
	public:
		Event()
				: thread(pthread_self()), thread_id(UINT64_MAX), sql_id(UINT64_MAX),
				  core(static_cast<uint32_t>(sched_getcpu()))
		{
			clock_gettime(CLOCK_MONOTONIC_RAW, &temp);
			time = static_cast<uint64_t>(temp.tv_nsec + temp.tv_sec * 1000000000);
		}

		virtual ~Event() = default;

		/**
		 * @brief Returns the timestamp at which this event has been created.
		 * @return The current timestamp.
		 */
		uint64_t get_time()
		{
			return time;
		}

		/**
		 * @brief Sets the timestap at which this event has been created.
		 * @param ns The new timestamp.
		 */
		void set_time(uint64_t ns)
		{
			time = ns;
		}

		/**
		 * @brief Returns the type of this Event.
		 * @return
		 */
		virtual EventType get_type()
		{
			return EventType::Event;
		}

		/**
		 * @brief Binds the variables of this event to the sqlite variables of a query.
		 * @param stm The prepared statement onto which to bind the variables.
		 */
		void sql_bind(sqlite3_stmt *stm)
		{
			sqlite3_reset(stm);
			sqlite3_clear_bindings(stm);
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":time"), static_cast<sqlite3_int64>(time));
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":involved_thread"), static_cast<sqlite3_int64>(thread_id));
			sqlite3_bind_int(stm, sqlite3_bind_parameter_index(stm, ":type"), static_cast<int>(get_type()));
			sqlite3_bind_int(stm, sqlite3_bind_parameter_index(stm, ":core"), core);
			add_binds(stm);
		}

		virtual void pre_insert(sqlite3_stmt *event_stm_p, sqlite3 *db)
		{
			(void)event_stm_p;
			(void)db;
		};

		/**
		 * @brief Returns the unique SQL id of this event.
		 * @return
		 */
		uint64_t get_sql_id()
		{
			return sql_id;
		}

		/**
		 * @brief Sets the unique SQL id of this event. Uniqueness is enforced by the database, not by this application!
		 * @param sql_id The new SQL id.
		 */
		void set_sql_id(uint64_t sql_id)
		{
			this->sql_id = sql_id;
		}

		/**
		 * @brief Sets the thread id of this event.
		 * @param thread_id The new thread id.
		 */
		void set_thread_id(uint64_t thread_id)
		{
			this->thread_id = thread_id;
		}

		uint64_t get_thread_id()
		{
			return this->thread_id;
		}

	protected:
		/**
		* @brief Binds all information of this event to the SQLite statement @p stm. This method must be implemented by derived classes.
		* @param stm The sqlite statement onto which variables should be bound.
		*/
		virtual void add_binds(sqlite3_stmt *stm) = 0;

		uint64_t time; ///< Timestamp of the event.
		pthread_t thread; ///< pthread id of the thread which fired this event.
		uint64_t thread_id; ///< Internal id of the thread which fired this event. This is necessary, as pthread ids might be reused.
		uint64_t sql_id; ///< The SQL id of this event.
		uint32_t core; ///< The CPU core this thread was executing on during event creation.
	};

/**
 * @brief Event representing a Linux signal.
 */
	class SignalEvent : public Event
	{
	public:
		explicit SignalEvent(int signum, void *fault_addr, int code)
				: Event(), signum(signum), fault_addr(fault_addr), code(code)
		{}

		void add_binds(sqlite3_stmt *stm) override
		{
			sqlite3_bind_int(stm, sqlite3_bind_parameter_index(stm, ":arg"), signum);
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":start_address"), reinterpret_cast<int64_t>(fault_addr));
			sqlite3_bind_int(stm, sqlite3_bind_parameter_index(stm, ":return_value"), code);
		}

		EventType get_type() override
		{
			return EventType::SignalEvent;
		}

		/**
		 * @brief Sets the faulting address.
		 * @param fault_addr
		 */
		void set_fault_addr(void *fault_addr)
		{
			this->fault_addr = fault_addr;
		}

		/**
		 * @brief Gets the faulting address.
		 * @return
		 */
		void *get_fault_addr()
		{
			return this->fault_addr;
		}

	protected:
		int signum; ///< The signal number.
		void *fault_addr; ///< The faulting address. This is only set for signals, that have an associated faulting address.
		int code; ///< The signal code identifying the cause of the signal.
	};

/**
 * @brief Event representing generic thread events.
 */
	class ThreadEvent : public Event
	{
	public:
		explicit ThreadEvent(pthread_t thread) : Event(), other_thread(thread), other_thread_id(UINT64_MAX)
		{}

		void add_binds(sqlite3_stmt *stm) override
		{
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":other_thread"), static_cast<sqlite3_int64>(other_thread_id));
		}

		EventType get_type() override
		{
			return EventType::ThreadEvent;
		}

		/**
		 * @brief Returns the pthread id of the other involved thread.
		 * @return
		 */
		pthread_t get_other_thread_pthread_id()
		{
			return other_thread;
		}

		/**
		 * @brief Returns the internal thread id of the other involved thread.
		 * @return
		 */
		uint64_t get_other_thread_id()
		{
			return other_thread_id;
		}

		/**
		 * @brief Sets the internal thread id of the other involved thread.
		 * @param other_thread_id
		 */
		void setOtherThreadID(uint64_t other_thread_id)
		{
			this->other_thread_id = other_thread_id;
		}

	protected:
		pthread_t other_thread; ///< pthread id of the other thread involved in this ThreadEvent
		uint64_t other_thread_id; ///< Internal id of the other thread
	};

/**
 * @brief Event representing thread creations as seen by the created thread
 */
	class ThreadCreationEvent : public ThreadEvent
	{
	public:
		ThreadCreationEvent(pthread_t thread, void *arg) : ThreadEvent(thread), arg(arg)
		{}

		void add_binds(sqlite3_stmt *stm) override
		{
			ThreadEvent::add_binds(stm);
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":arg"), reinterpret_cast<sqlite3_int64>(arg));
		}

		EventType get_type() override
		{
			return EventType::ThreadCreationEvent;
		}

	protected:
		void *arg; ///< Argument that was given to the thread
	};

/**
 * @brief Event representing thread creations as seen by the creating thread.
 */
	class ThreadCreatorEvent : public ThreadEvent
	{
	public:
		ThreadCreatorEvent() : ThreadEvent(0), start_function(nullptr), ret(0)
		{}

		void add_binds(sqlite3_stmt *stm) override
		{
			ThreadEvent::add_binds(stm);
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":start_function"), reinterpret_cast<sqlite3_int64>(start_function));
			sqlite3_bind_int(stm, sqlite3_bind_parameter_index(stm, ":return_value"), ret);
		}

		EventType get_type() override
		{
			return EventType::ThreadCreatorEvent;
		}

		/**
		 * @brief Sets the variables for this event. This is needed, as the event is created before this information is known to the creator.
		 * @param created_thread The pthread id of the thread that has been created by the calling thread.
		 * @param start_function The start function of the created thread.
		 * @param ret The return value pf pthread_create, indicated creation error.
		 */
		void set_info(pthread_t created_thread, void *start_function, int ret)
		{
			other_thread = created_thread;
			this->start_function = start_function;
			this->ret = ret;
		}

		/**
		 * @brief Returns the start function of the created thread
		 * @return
		 */
		void *get_start_function()
		{
			return this->start_function;
		}

	protected:
		void *start_function; ///< Function pointer to the start function of the thread.
		int ret; ///< Return value of pthread_create.
	};

/**
 * @brief Event representing thread destruction.
 */
	class ThreadDestructionEvent : public ThreadEvent
	{
	public:
		ThreadDestructionEvent(pthread_t thread, void *ret) : ThreadEvent(thread), ret(ret)
		{}

		void add_binds(sqlite3_stmt *stm) override
		{
			ThreadEvent::add_binds(stm);
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":return_value"), reinterpret_cast<sqlite3_int64>(ret));
		}

		EventType get_type() override
		{
			return EventType::ThreadDestructionEvent;
		}

	protected:
		void *ret; ///< Return value of the original start function.
	};

/**
 * @brief Event representing thread name setting.
 */
	class ThreadSetNameEvent : public ThreadEvent
	{
	public:
		ThreadSetNameEvent(pthread_t modified_thread, std::string &name)
				: ThreadEvent(modified_thread), name(name), ret(-1)
		{}

		void add_binds(sqlite3_stmt *stm) override
		{
			ThreadEvent::add_binds(stm);
			sqlite3_bind_text(stm, sqlite3_bind_parameter_index(stm, ":name"), name.c_str(), static_cast<int>(name.length()), SQLITE_STATIC);
			sqlite3_bind_int(stm, sqlite3_bind_parameter_index(stm, ":return_value"), ret);
		}

		EventType get_type() override
		{
			return EventType::ThreadSetNameEvent;
		}

		/**
		 * @brief Sets the return value of pthread_set_name_np.
		 * @param ret
		 */
		void set_info(int ret)
		{
			this->ret = ret;
		}

		/**
		 * @brief Returns the name that was set.
		 * @return
		 */
		std::string get_name()
		{
			return name;
		}

	protected:
		std::string name; ///< The name of the thread.
		int ret; ///< The return value of pthread_set_name_np.
	};

/**
 * @brief Event representing generic enclave events.
 */
	class EnclaveEvent : public Event
	{
	public:
		explicit EnclaveEvent(sgx_enclave_id_t eid) : Event(), eid(eid)
		{}

		void add_binds(sqlite3_stmt *stm) override
		{
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":eid"), static_cast<sqlite3_int64>(eid));
		}

		EventType get_type() override
		{
			return EventType::EnclaveEvent;
		}

		/**
		 * @brief Returns the enclave id of the enclave to which this event belongs.
		 * @return
		 */
		sgx_enclave_id_t get_eid()
		{
			return eid;
		}

		/**
		 * @brief Sets enclave id of the enclave to which this event belongs.
		 * @param eid
		 */
		void set_eid(sgx_enclave_id_t eid)
		{
			this->eid = eid;
		}

	protected:
		sgx_enclave_id_t eid; ///< id of the enclave participating in the event.
	};

/**
 * @brief Event representing enclave creations.
 */
	class EnclaveCreationEvent : public EnclaveEvent
	{
	public:
		EnclaveCreationEvent(sgx_enclave_id_t eid, std::string &file_name, sgx_status_t ret, uint64_t enclave_start,
		                     uint64_t enclave_end)
				: EnclaveEvent(eid), file_name(file_name), ret(ret), enclave_start(enclave_start),
				  enclave_end(enclave_end)
		{}

		void add_binds(sqlite3_stmt *stm) override
		{
			EnclaveEvent::add_binds(stm);
			sqlite3_bind_text(stm, sqlite3_bind_parameter_index(stm, ":file_name"), file_name.c_str(), static_cast<int>(file_name.length()), SQLITE_STATIC);
			sqlite3_bind_int(stm, sqlite3_bind_parameter_index(stm, ":return_value"), ret);
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":enclave_start"), static_cast<sqlite3_int64>(enclave_start));
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":enclave_end"), static_cast<sqlite3_int64>(enclave_end));
		}

		EventType get_type() override
		{
			return EventType::EnclaveCreationEvent;
		}

		/**
		 * @brief Returns the file name of the enclave.
		 * @return
		 */
		std::string get_file_name()
		{
			return file_name;
		}

	protected:
		std::string file_name; ///< File name of the enclave.
		sgx_status_t ret; ///< Return value of sgx_create_enclave.
		uint64_t enclave_start; ///< Start address of the enclave.
		uint64_t enclave_end; ///< End address of the enclave.
	};

/**
 * @brief Event representing enclave destruction.
 */
	class EnclaveDestructionEvent : public EnclaveEvent
	{
	public:
		EnclaveDestructionEvent(sgx_enclave_id_t eid, sgx_status_t ret) : EnclaveEvent(eid), ret(ret)
		{}

		void add_binds(sqlite3_stmt *stm) override
		{
			EnclaveEvent::add_binds(stm);
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":return_value"), static_cast<sqlite3_int64>(ret));
		}

		EventType get_type() override
		{
			return EventType::EnclaveDestructionEvent;
		}

	protected:
		sgx_status_t ret; ///< Return value of sgx_destroy_enclave.
	};

/**
 * @brief Event representing enclave paging.
 */
	class EnclavePagingEvent : public EnclaveEvent
	{
	public:
		EnclavePagingEvent(sgx_enclave_id_t eid, uint64_t address) : EnclaveEvent(eid), address(address)
		{}

		void add_binds(sqlite3_stmt *stm) override
		{
			EnclaveEvent::add_binds(stm);
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":arg"), static_cast<sqlite3_int64>(address));
		}

		EventType get_type() override
		{
			return EventType::EnclavePagingEvent;
		}

		/**
		 * @brief Returns the virtual address of the page that was paged in or paged out.
		 * @return
		 */
		uint64_t get_address()
		{
			return address;
		}

	protected:
		uint64_t address; ///< Address of the page
	};

/**
 * @brief Event representing enclave pages getting swapped out.
 */
	class EnclavePageOutEvent : public EnclavePagingEvent
	{
	public:
		EnclavePageOutEvent(sgx_enclave_id_t eid, uint64_t address) : EnclavePagingEvent(eid, address)
		{}

		EventType get_type() override
		{
			return EventType::EnclavePageOutEvent;
		}
	};

/**
 * @brief Event representing enclave pages getting swapped out.
 */
	class EnclavePageInEvent : public EnclavePagingEvent
	{
	public:
		EnclavePageInEvent(sgx_enclave_id_t eid, uint64_t address) : EnclavePagingEvent(eid, address)
		{}

		EventType get_type() override
		{
			return EventType::EnclavePageInEvent;
		}
	};


	class EnclaveCallEvent : public EnclaveEvent
	{
	public:
		EnclaveCallEvent(sgx_enclave_id_t eid, int call_id, void const *arg, EnclaveCallEvent *previous_call) : EnclaveEvent(eid), call_id(call_id), arg(arg), previous_call(previous_call) {}

		void add_binds(sqlite3_stmt *stm) override
		{
			EnclaveEvent::add_binds(stm);
			sqlite3_bind_int(stm, sqlite3_bind_parameter_index(stm, ":call_id"), call_id);
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":arg"), (sqlite3_int64) arg);
			if (previous_call != nullptr)
			{
				sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":call_event"), static_cast<sqlite3_int64>(previous_call->get_sql_id()));
			}
		}

		void pre_insert(sqlite3_stmt *event_stm_p, sqlite3 *db) override
		{
			if (previous_call != nullptr && previous_call->get_sql_id() == UINT64_MAX)
			{
				previous_call->pre_insert(event_stm_p, db);

				// Insert that first
				previous_call->sql_bind(event_stm_p);
				sqlite3_step(event_stm_p);
				previous_call->set_sql_id(static_cast<uint64_t>(sqlite3_last_insert_rowid(db)));
			}
		}

		EventType get_type() override
		{
			return EventType::EnclaveCallEvent;
		}

		EnclaveCallEvent *get_previous_call()
		{
			return previous_call;
		}

		void set_arg(void const *arg)
		{
			this->arg = arg;
		}

	protected:
		int call_id; ///< id of the call.
		void const *arg;
		EnclaveCallEvent *previous_call; ///< Pointer to the previous call or nullptr of there was none.
	};

/**
 * @brief Event representing ECalls
 */
	class EnclaveECallEvent : public EnclaveCallEvent
	{
	public:
		EnclaveECallEvent(sgx_enclave_id_t eid, int ecall_id, void const *arg, EnclaveCallEvent *previous_call) : EnclaveCallEvent(eid, ecall_id, arg, previous_call), aex_counter(0)
		{}

		void add_binds(sqlite3_stmt *stm) override
		{
			EnclaveCallEvent::add_binds(stm);
		}

		EventType get_type() override
		{
			return EventType::EnclaveECallEvent;
		}

		uint64_t aex_counter;
	};

/**
 * @brief Event representing returning ECall.
 */
	class EnclaveECallReturnEvent : public EnclaveEvent
	{
	public:
		EnclaveECallReturnEvent(EnclaveECallEvent *ecallEvent, sgx_status_t ret, uint64_t aex_count)
				: EnclaveEvent(ecallEvent->get_eid()), ecall_event(ecallEvent), ret(ret), aex_count(aex_count)
		{}

		void add_binds(sqlite3_stmt *stm) override
		{
			EnclaveEvent::add_binds(stm);
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":call_event"), static_cast<sqlite3_int64>(ecall_event->get_sql_id()));
			sqlite3_bind_int(stm, sqlite3_bind_parameter_index(stm, ":return_value"), ret);
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":aex_count"), static_cast<sqlite3_int64>(aex_count));
		}

		void pre_insert(sqlite3_stmt *event_stm_p, sqlite3 *db) override
		{
			if (ecall_event->get_sql_id() == UINT64_MAX)
			{
				ecall_event->pre_insert(event_stm_p, db);

				// Insert inner event first
				ecall_event->sql_bind(event_stm_p);
				sqlite3_step(event_stm_p);
				ecall_event->set_sql_id(static_cast<uint64_t>(sqlite3_last_insert_rowid(db)));
			}
		}

		EventType get_type() override
		{
			return EventType::EnclaveECallReturnEvent;
		}

		/**
		 * @brief Return the EnclaveECallEvent that fired before this return event.
		 * @return
		 */
		EnclaveECallEvent *get_ecall_event()
		{
			return ecall_event;
		}

	protected:
		EnclaveECallEvent *ecall_event; ///< Corresponding EnclaveECallEvent.
		sgx_status_t ret; ///< Return value of the ECall.
		uint64_t aex_count; ///< Number of AEX' this ECall experienced.
	};

/**
 * @brief Event representing OCalls.
 */
	class EnclaveOCallEvent : public EnclaveCallEvent
	{
	public:
		EnclaveOCallEvent(sgx_enclave_id_t eid, uint32_t ocall_id, void const *arg, EnclaveCallEvent *previous_call) : EnclaveCallEvent(eid, ocall_id, arg, previous_call)
		{}

		void add_binds(sqlite3_stmt *stm) override
		{
			EnclaveCallEvent::add_binds(stm);
		}

		EventType get_type() override
		{
			return EventType::EnclaveOCallEvent;
		}
	};

/**
 * @brief Event representing returning OCalls.
 */
	class EnclaveOCallReturnEvent : public EnclaveEvent
	{
	public:
		EnclaveOCallReturnEvent(EnclaveOCallEvent *ocall, int ret)
				: EnclaveEvent(ocall->get_eid()), ocall_event(ocall), ret(ret)
		{}

		void add_binds(sqlite3_stmt *stm) override
		{
			EnclaveEvent::add_binds(stm);
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":call_event"), static_cast<sqlite3_int64>(ocall_event->get_sql_id()));
			sqlite3_bind_int(stm, sqlite3_bind_parameter_index(stm, ":return_value"), ret);
		}

		void pre_insert(sqlite3_stmt *event_stm_p, sqlite3 *db) override
		{
			if (ocall_event->get_sql_id() == UINT64_MAX)
			{
				ocall_event->pre_insert(event_stm_p, db);

				// Insert inner event first
				ocall_event->sql_bind(event_stm_p);
				sqlite3_step(event_stm_p);
				ocall_event->set_sql_id(static_cast<uint64_t>(sqlite3_last_insert_rowid(db)));
			}
		}

		EventType get_type() override
		{
			return EventType::EnclaveOCallReturnEvent;
		}

		/**
		 * @brief Return the EnclaveOCallEvent that fired before this return event.
		 * @return
		 */
		EnclaveOCallEvent *get_ocall_event()
		{
			return ocall_event;
		}

	protected:
		EnclaveOCallEvent *ocall_event; ///< Corresponding EnclaveOCallEvent
		int ret; ///< Return value of the OCall
	};

	class EnclaveSyncWaitEvent : public EnclaveEvent
	{
	public:
		explicit EnclaveSyncWaitEvent(EnclaveOCallEvent *ocall) : EnclaveEvent(ocall->get_eid()), ocall_event(ocall)
		{}

		void add_binds(sqlite3_stmt *stm) override
		{
			EnclaveEvent::add_binds(stm);
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":call_event"), static_cast<sqlite3_int64>(ocall_event->get_sql_id()));
		}

		void pre_insert(sqlite3_stmt *event_stm_p, sqlite3 *db) override
		{
			if (ocall_event->get_sql_id() == UINT64_MAX)
			{
				ocall_event->pre_insert(event_stm_p, db);

				// Insert inner event first
				ocall_event->sql_bind(event_stm_p);
				sqlite3_step(event_stm_p);
				ocall_event->set_sql_id(static_cast<uint64_t>(sqlite3_last_insert_rowid(db)));
			}
		}

		EventType get_type() override
		{
			return EventType::EnclaveSyncWaitEvent;
		}

		EnclaveOCallEvent *get_ocall_event()
		{
			return ocall_event;
		}
	protected:
		EnclaveOCallEvent *ocall_event;
	};

	class EnclaveSyncSetEvent : public EnclaveEvent
	{
	public:
		explicit EnclaveSyncSetEvent(EnclaveOCallEvent *ocall, EnclaveSyncWaitEvent *wait) : EnclaveEvent(wait->get_eid()), ocall_event(ocall), wait_event(wait)
		{}

		void add_binds(sqlite3_stmt *stm) override
		{
			EnclaveEvent::add_binds(stm);
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":call_event"), static_cast<sqlite3_int64>(ocall_event->get_sql_id()));
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":arg"), static_cast<sqlite3_int64>(wait_event->get_sql_id()));
		}

		void pre_insert(sqlite3_stmt *event_stm_p, sqlite3 *db) override
		{
			if (ocall_event->get_sql_id() == UINT64_MAX)
			{
				ocall_event->pre_insert(event_stm_p, db);

				// Insert inner event first
				ocall_event->sql_bind(event_stm_p);
				sqlite3_step(event_stm_p);
				ocall_event->set_sql_id(static_cast<uint64_t>(sqlite3_last_insert_rowid(db)));
			}

			if (wait_event->get_sql_id() == UINT64_MAX)
			{
				wait_event->pre_insert(event_stm_p, db);

				// Insert inner event first
				wait_event->sql_bind(event_stm_p);
				sqlite3_step(event_stm_p);
				wait_event->set_sql_id(static_cast<uint64_t>(sqlite3_last_insert_rowid(db)));
			}
		}

		EventType get_type() override
		{
			return EventType::EnclaveSyncSetEvent;
		}

		EnclaveOCallEvent *get_ocall_event()
		{
			return ocall_event;
		}

		EnclaveSyncWaitEvent *get_wait_event()
		{
			return wait_event;
		}
	protected:
		EnclaveOCallEvent *ocall_event;
		EnclaveSyncWaitEvent *wait_event;
	};

	/**
	 * @brief Event representing an AEX.
	 */
	class EnclaveAEXEvent : public EnclaveEvent
	{
	public:
		explicit EnclaveAEXEvent(EnclaveECallEvent *ecall) : EnclaveEvent(ecall->get_eid()), ecall_event(ecall)
		{}

		void add_binds(sqlite3_stmt *stm) override
		{
			EnclaveEvent::add_binds(stm);
			sqlite3_bind_int64(stm, sqlite3_bind_parameter_index(stm, ":call_event"), static_cast<sqlite3_int64>(ecall_event->get_sql_id()));
		}

		void pre_insert(sqlite3_stmt *event_stm_p, sqlite3 *db) override
		{
			if (ecall_event->get_sql_id() == UINT64_MAX)
			{
				ecall_event->pre_insert(event_stm_p, db);

				// Insert inner event first
				ecall_event->sql_bind(event_stm_p);
				sqlite3_step(event_stm_p);
				ecall_event->set_sql_id(static_cast<uint64_t>(sqlite3_last_insert_rowid(db)));
			}
		}

		EventType get_type() override
		{
			return EventType::EnclaveAEXEvent;
		}

		EnclaveECallEvent *get_ecall_event()
		{
			return ecall_event;
		}
	protected:
		EnclaveECallEvent *ecall_event;
	};
}

#endif //SGX_PERF_EVENTS_H
