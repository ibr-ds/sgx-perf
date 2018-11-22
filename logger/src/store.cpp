/**
 * @file store.cpp
 * @author weichbr
 */

#include <unistd.h>
#include <cstdio>
#include <utility>
#include <fstream>
#include <dlfcn.h>
#include <cstring>
#include <set>

#include "store.h"
#include "elfparser.h"
#include "events.h"

/**
 * @brief Mapping event type IDs to string names
 */
std::string __event_type_names[] = {
                                    "Event",
                                    "SignalEvent",
                                    "ThreadEvent",
                                    "ThreadCreationEvent",
                                    "ThreadCreatorEvent",
                                    "ThreadDestructionEvent",
                                    "ThreadSetNameEvent",
                                    "EnclaveEvent",
                                    "EnclaveCreationEvent",
                                    "EnclaveDestructionEvent",
                                    "EnclavePagingEvent",
                                    "EnclavePageOutEvent",
                                    "EnclavePageInEvent",
                                    "EnclaveCallEvent",
                                    "EnclaveECallEvent",
                                    "EnclaveECallReturnEvent",
                                    "EnclaveOCallEvent",
                                    "EnclaveOCallReturnEvent",
                                    "EnclaveSyncWaitEvent",
                                    "EnclaveSyncSetEvent",
                                    "EnclaveAEXEvent",
                                    ""};

extern sgxperf::Config *config;

thread_local sgxperf::Thread *current_thread = nullptr;

/**
 * @brief Executes the SQL query inside the given string
 * @param sql
 */
void sgxperf::EventStore::sql_exec(const char *sql)
{
	char *errmsg = nullptr;
	int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
	if (rc != SQLITE_OK)
	{
		printf("/!\\ Could not execute statement: %s\n", errmsg);
		printf("Statement was: %s\n", sql);
		sqlite3_free(errmsg);
		sqlite3_close(db);
		exit(-1);
	}
}

/**
 * @brief Executes the SQL query inside the given string.
 * @param sql
 */
void sgxperf::EventStore::sql_exec(std::string const &sql)
{
	sql_exec((char *)sql.c_str());
}

/**
 * @brief Executes the SQL query inside the given string stream.
 * @param ss
 */
void sgxperf::EventStore::sql_exec(std::stringstream &ss)
{
	sql_exec(ss.str());
	ss.str(std::string());
}


sgxperf::EventStore::EventStore() : enclave_map_lock({}), enclave_map(), tcs_map(), thread_id(0), db(nullptr), thread_events_lock({}), thread_events(), finalized(false), main_thread(nullptr)
{
	timespec t = {};
	clock_gettime(CLOCK_MONOTONIC_RAW, &t);
	start_time = static_cast<uint64_t>(t.tv_nsec + t.tv_sec * 1000000000);
}

sgxperf::EventStore::~EventStore()
{
	// TODO: write everything out
}

/**
 * @brief Creates a new SQLite database and adds the tables
 * @return 0 on success, non-zero otherwise
 */
int sgxperf::EventStore::create_database()
{
	std::stringstream ss;
	ss << "out-" << getpid() << ".db";
	int rc = sqlite3_open(":memory:", &db);
	if (rc)
	{
		printf("/!\\ Could not open database: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		return -1;
	}

	char *errmsg = nullptr;

	// Adding tables
	const char *tables = ""
	                     "CREATE TABLE `event_map` ( `id` INTEGER NOT NULL UNIQUE, `name` TEXT NOT NULL, PRIMARY KEY(`id`) );"
	                     "CREATE TABLE `general` ( `key` TEXT NOT NULL, `value` INTEGER NOT NULL );"
	                     "CREATE TABLE `threads` ( `id` INTEGER NOT NULL UNIQUE, `pthread_id` INTEGER NOT NULL, `name` TEXT NOT NULL, `start_address` INTEGER NOT NULL, `start_symbol` TEXT, `start_symbol_file_name` TEXT, `start_address_normalized` INTEGER, PRIMARY KEY(`id`) );"
	                     "CREATE TABLE `events` ( `id` INTEGER PRIMARY KEY AUTOINCREMENT UNIQUE, `type` INTEGER NOT NULL, `time` INTEGER NOT NULL, `involved_thread` INTEGER NOT NULL, `core` INTEGER NOT NULL, `other_thread` INTEGER, `arg` INTEGER, `start_function` INTEGER, `return_value` INTEGER, `name` TEXT, `eid` INTEGER, `file_name` TEXT, `enclave_start` INTEGER, `enclave_end` INTEGER, `call_id` INTEGER, `call_event` INTEGER, `aex_count` INTEGER);"
	                     "CREATE TABLE `ocalls` ( `id` INTEGER NOT NULL, `eid` INTEGER NOT NULL, `symbol_name` TEXT, `symbol_file_name` TEXT, `symbol_address` INTEGER, `symbol_address_normalized` INTEGER, PRIMARY KEY(`id`,`eid`) );"
	                     "CREATE TABLE `ecalls` ( `id` INTEGER NOT NULL, `eid` INTEGER NOT NULL, `symbol_address` INTEGER NOT NULL, `symbol_name` TEXT, `is_private` INTEGER, PRIMARY KEY(`id`,`eid`) )"
	                     "";

	rc = sqlite3_exec(db, tables, nullptr, nullptr, &errmsg);

	if (rc != SQLITE_OK)
	{
		printf("/!\\ Could not create tables:\n");
		printf("%s\n", errmsg);
		sqlite3_free(errmsg);
		sqlite3_close(db);
		return -1;
	}

	return 0;
}

/**
 * @brief Insert an Event into the EventStore for saving.
 * @param[in] involved_thread The thread ID for which this Event should be inserted
 * @param[in] event The Event to be inserted
 */
void sgxperf::EventStore::insert_event(pthread_t involved_thread, Event *event)
{
	// First, check if we already wrote out everything
	if (finalized)
	{
		return;
	}

	// Find the corresponding thread and add the event to its queue
	// But check thread local storage first
	if (current_thread == nullptr)
	{
		read_lock(&thread_events_lock);
		unlock_func uf = read_unlock;
		auto it = thread_events.find(involved_thread);
		if (it == thread_events.end())
		{
			read_unlock(&thread_events_lock);
			write_lock(&thread_events_lock);
			auto thread_pair = std::pair<pthread_t, Thread *>(involved_thread, (new Thread(involved_thread, __sync_fetch_and_add(&thread_id, 1))));
			auto itt = thread_events.insert(thread_pair);
			it = itt.first;
			uf = write_unlock;
			if (main_thread == nullptr)
				main_thread = thread_pair.second;
		}
		uf(&thread_events_lock);
		current_thread = it->second;
	}

	// Add event to thread's event queue
	current_thread->events.push(event);

	// Set the internal thread id of the event to the one of the thread
	event->set_thread_id(current_thread->sql_id);

	// If the event created another thread, we need to create that thread's object
	if (auto tcevent = dynamic_cast<ThreadCreatorEvent *>(event))
	{
		// A new thread has been created, so we need to add that one to the list
		pthread_t other_thread = tcevent->get_other_thread_pthread_id();
		read_lock(&thread_events_lock);
		unlock_func uf = read_unlock;
		auto oit = thread_events.find(other_thread);
		if (oit == thread_events.end())
		{
			read_unlock(&thread_events_lock);
			write_lock(&thread_events_lock);
			auto thread_pair = std::pair<pthread_t, Thread *>(other_thread, (new Thread(other_thread, __sync_fetch_and_add(&thread_id, 1))));
			auto itt = thread_events.insert(thread_pair);
			oit = itt.first;
			uf = write_unlock;
		}
		uf(&thread_events_lock);
	}

	// If the event involves another thread, we need to find out the other's thread internal ID
	// Also, some ThreadEvents need special handling
	if (auto tevent = dynamic_cast<ThreadEvent *>(event))
	{
		// Need to replace id of other thread with this one.
		read_lock(&thread_events_lock);
		auto oit = thread_events.find(tevent->get_other_thread_pthread_id());
		if (oit == thread_events.end())
		{
			// Potential race: t1 created t2, so t1 fired a ThreadCreatorEvent and then killed itself (-> ThreadDestructionEvent).
			// t2 is now running and fires a ThreadCreationEvent which tries to look up its now killed creator
			// This fails, as the creator thread is now in the finished threads list, so we need to look there...
			// FIXME: implement this case
			printf("/!\\ Got an event with an other_thread id that was not in our map of threads!\n");
			throw std::exception();
		}
		read_unlock(&thread_events_lock);
		auto othread = oit->second;

		tevent->setOtherThreadID(othread->sql_id);

		switch (event->get_type())
		{
			case EventType::ThreadDestructionEvent:
			{
				write_lock(&thread_events_lock);
				thread_events.erase(involved_thread);
				finished_thread_events.push_back(current_thread);
				write_unlock(&thread_events_lock);
				break;
			}
			case EventType::ThreadSetNameEvent:
			{
				auto tsnevent = dynamic_cast<ThreadSetNameEvent *>(event);
				othread->name = tsnevent->get_name();
				break;
			}
			default:
			{
				// Do no special handling
			}
		}
	}
}

/**
 * @brief Gets the current @c Thread object
 * @return Pointer to the current @c Thread object
 */
sgxperf::Thread *sgxperf::EventStore::get_thread()
{
	if (current_thread == nullptr)
	{
		read_lock(&thread_events_lock);
		pthread_t id = pthread_self();
		auto it = thread_events.find(id);
		if (it == thread_events.end())
		{
			read_unlock(&thread_events_lock);
			return nullptr;
		}
		read_unlock(&thread_events_lock);
		current_thread = it->second;
	}
	return current_thread;
}

/**
 * @brief Inserts the Event @p event into this @c EventStore
 * @param event The Event to be inserted
 */
void sgxperf::EventStore::insert_event(Event *event)
{
	insert_event(pthread_self(), event);
}

/**
 * @brief Create a JSON sumamry of everything stored in this EventStore
 * @return A JSON representation of the contents of this EventStore
 */
void sgxperf::EventStore::create_summary()
{
	if (config->is_benchmark_mode_enabled())
	{
		std::cout << "(i) Benchmark mode, will not serialize events" << std::endl;
		return;
	}

	std::cout << "(i) Starting serialization" << std::endl;
	std::stringstream stm;
	std::map<sgx_enclave_id_t, std::string> enclave_files;

	if (!finalized)
	{
		printf("!!! EventStore has to be finalized before serialization!\n");
		throw std::exception();
		//throw std::exception("EventStore has to be finalized before outputting summary");
	}

	stm << "INSERT INTO `general` (`key`,`value`) VALUES ('version',1);";
	stm << "INSERT INTO `general` (`key`,`value`) VALUES ('start_time'," << start_time << ");";
	stm << "INSERT INTO `general` (`key`,`value`) VALUES ('end_time'," << end_time << ");";
	stm << "INSERT INTO `general` (`key`,`value`) VALUES ('main_thread'," << std::dec << main_thread->sql_id << ");";

	sql_exec(stm);

	std::cout << "(i) Mapping event IDs to names" << std::endl;
	// event id to event name mapping
	for (__event_type event_id = __event_type::First; event_id != __event_type::Last; ++event_id)
	{
		stm << "INSERT INTO `event_map` (`id`, `name`) VALUES (" << (int)event_id << ", '" << __event_type_names[(int)event_id] << "');";
	}
	// Also add last entry
	stm << "INSERT INTO `event_map` (`id`, `name`) VALUES (" << (int)__event_type::Last << ", '" << __event_type_names[(int)__event_type::Last] << "');";

	sql_exec(stm);

	std::cout << "(i) Serializing events (" << thread_events.size() << " + " << finished_thread_events.size() << " threads)" << std::endl;

	auto tit = finished_thread_events.begin();
	std::set<void *> thread_addresses;
	while (tit != finished_thread_events.end())
	{
		stm << "INSERT INTO `threads` (`id`, `pthread_id`, `name`, `start_address`) VALUES (" << (*tit)->sql_id << ", " << (*tit)->id << ", '" << (*tit)->name << "', 0);";
		sql_exec(stm);
		tit++;
	}

	stm << "INSERT INTO `events` (`type`,`time`,`involved_thread`,`core`,`other_thread`,"
	       "`arg`,`start_function`,`return_value`,`name`,`eid`,"
	       "`file_name`,`enclave_start`,`enclave_end`,`call_id`,`call_event`,"
	       "`aex_count`) "
	       "VALUES (:type, :time, :involved_thread, :core, :other_thread, "
	       ":arg, :start_function, :return_value, :name, :eid, "
	       ":file_name, :enclave_start, :enclave_end, :call_id, :call_event, "
	       ":aex_count);";
	auto event_stm = stm.str();
	sqlite3_stmt *event_stm_p = nullptr;
	auto ret = sqlite3_prepare_v2(db, event_stm.c_str(), static_cast<int>(event_stm.length()), &event_stm_p, nullptr);
	if (ret != SQLITE_OK)
	{
		const char *errmsg = sqlite3_errmsg(db);
		printf("/!\\ Could not prepare statement: %s\n", errmsg);
		printf("Statement was: %s\n", event_stm.c_str());
		sqlite3_close(db);
		exit(-1);
	}
	stm.str(std::string());

	tit = finished_thread_events.begin();
	while (tit != finished_thread_events.end())
	{
		auto queue = (*tit)->events;
		while (!queue.empty())
		{
			auto e = queue.front();
			queue.pop();

			// Make all pre insert things happen
			e->pre_insert(event_stm_p, db);

			// If this is a paging event, we need to find the enclave it belongs to.
			if (e->get_type() == EventType::EnclavePageInEvent || e->get_type() == EventType::EnclavePageOutEvent)
			{
				auto epfe = dynamic_cast<EnclavePagingEvent *>(e);
				auto encl_it = enclave_map.begin();
				while (encl_it != enclave_map.end())
				{
					auto encl = encl_it->second;
					// Check if this event happened in this enclaves memory range and lifetime
					if (encl->is_within_enclave(reinterpret_cast<void *>(epfe->get_address()))
					    && encl->is_within_lifetime(epfe->get_time()))
					{
						epfe->set_eid(encl->eid);
						break;
					}
					encl_it++;
				}
				if (encl_it == enclave_map.end())
				{
					// No enclave found, ignore this pagefault
					goto nextevent;
				}
			}

			// Check if event has already been inserted and only insert if not
			if (e->get_sql_id() == UINT64_MAX)
			{
				e->sql_bind(event_stm_p);
				sqlite3_step(event_stm_p);
				e->set_sql_id(static_cast<uint64_t>(sqlite3_last_insert_rowid(db)));
			}

			// In case of EnclaveCreationEvent we need to add the enclave file to the enclave_files map
			if (e->get_type() == EventType::EnclaveCreationEvent)
			{
				auto enclevent = dynamic_cast<EnclaveCreationEvent *>(e);
				if (enclave_files.find(enclevent->get_eid()) == enclave_files.end())
				{
					enclave_files[enclevent->get_eid()] = std::string(enclevent->get_file_name());
				}
			}

			// In case of ThreadCreatorEvent we need to set the start address of the created thread inside the database
			// Also we need to insert the address to the thread_address map for later symbol resolution
			if (e->get_type() == EventType::ThreadCreatorEvent)
			{
				auto tcevent = dynamic_cast<ThreadCreatorEvent *>(e);
				thread_addresses.insert(tcevent->get_start_function());
				stm << "UPDATE `threads` SET `start_address` = " << (uint64_t) tcevent->get_start_function() << " WHERE `id` == " << tcevent->get_other_thread_id() << ";";
				sql_exec(stm);
			}
			nextevent:;
			//delete e;
		}
		printf(".");
		fflush(stdout);
		tit++;
	}
	printf("\n");

	std::cout << "(i) Mapping thread start addresses to symbols" << std::endl;
	auto tsit = thread_addresses.begin();
	while (tsit != thread_addresses.end())
	{
		Dl_info dlinfo = {};
		int ret = dladdr(*tsit, &dlinfo);
		if (ret == 0)
		{
			tsit++;
			continue;
		}
		auto binary = std::string(dlinfo.dli_fname);
		auto info = getSymbolForAddress(binary, ((uint64_t)*tsit - (uint64_t)dlinfo.dli_fbase));
		if (info.empty())
			info = getSymbolForAddress(binary, ((uint64_t)*tsit));
		if (info.empty())
		{
			stm << "UPDATE `threads` SET `start_symbol_file_name` = '" << dlinfo.dli_fname << "',"
			       "`start_address_normalized` = " << ((uint64_t)*tsit - (uint64_t)dlinfo.dli_fbase) << ""
			       " WHERE `start_address` == " << (uint64_t)*tsit << ";";
		}
		else
		{
			stm << "UPDATE `threads` SET `start_symbol_file_name` = '" << dlinfo.dli_fname << "',"
			       "`start_address_normalized` = " << ((uint64_t)*tsit - (uint64_t)dlinfo.dli_fbase) << ","
			       "`start_symbol` = '" << info << "' WHERE `start_address` == " << (uint64_t)*tsit << ";";
		}
		sql_exec(stm);
		tsit++;
	}

	std::cout << "(i) Mapping OCall IDs to symbols" << std::endl;
	// Print OCall ID <-> Symbol name map
	auto enclit = enclave_map.begin();
	while (enclit != enclave_map.end())
	{
		auto map = enclit->second->orig_table;
		if (map == nullptr)
		{
			// Enclave has never been called, so we don't know ocalls, just skip it
			goto next;
		}
		for (uint32_t i = 0; i < map->count; ++i)
		{
			Dl_info dlinfo = {};
			int ret = dladdr(map->table[i], &dlinfo);
			if (ret == 0)
			{
				stm << "INSERT INTO `ocalls` (`id`, `eid`) VALUES ("
				       "" << i << ","
				       "" << (uint64_t) enclit->first << ""
				       ");";
				sql_exec(stm);
				goto next;
			}
			auto binary = std::string(dlinfo.dli_fname);
			auto info = getSymbolForAddress(binary, ((uint64_t)map->table[i] - (uint64_t)dlinfo.dli_fbase));
			if (info.empty())
				info = getSymbolForAddress(binary, ((uint64_t)map->table[i]));
			stm << "INSERT INTO `ocalls` (`id`, `eid`, `symbol_name`, `symbol_file_name`, `symbol_address`, `symbol_address_normalized`) VALUES ("
			       "" << i << ","
			       "" << (uint64_t) enclit->first << ","
			       "'" << info << "',"
			       "'" << dlinfo.dli_fname << "',"
			       "" << (uint64_t) map->table[i] << ","
			       "" << ((uint64_t)map->table[i] - (uint64_t)dlinfo.dli_fbase) << ""
                   ");";
			sql_exec(stm);
		}
		next:
		enclit++;
	}

	std::cout << "(i) Mapping ECall IDs to symbols (" << enclave_files.size() << " enclaves)" << std::endl;
	// Print ECall ID <-> Symbol name map
	auto enclfit = enclave_files.begin();
	while (enclfit != enclave_files.end())
	{
		std::cout << "(i) Enclave " << enclfit->first << "(" << enclfit->second.c_str() << ")" << std::endl;
		struct ecall_table *ecalltable = getECallTable(enclfit->second);
		if (ecalltable == nullptr)
		{
			std::cout << "(i) Could not get g_ecall_table" << std::endl;
			enclfit++;
			continue;
		}
		for (size_t i = 0; i < ecalltable->count; ++i)
		{
			auto info = getSymbolForAddress(enclfit->second, (uint64_t)ecalltable->ecall_table[i].ecall_addr);

			stm << "INSERT INTO `ecalls` (`id`, `eid`, `symbol_address`, `symbol_name`, `is_private`) VALUES ("
			       "" << i << ","
			       "" << (uint64_t) enclfit->first << ","
			       "" << (uint64_t) ecalltable->ecall_table[i].ecall_addr << ","
			       "'" << info << "',"
			       "" <<  (ecalltable->ecall_table[i].is_priv ? "1" : "0") << ""
			       ");";
			sql_exec(stm);
		}
		enclfit++;
		free(ecalltable);
	}

	std::cout << "(i) Close all binary files" << std::endl;
	// Close all opened binary files
	closeAllFiles();

	std::cout << "(i) Creating DB indices" << std::endl;
	const char *indices = ""
	                      "CREATE UNIQUE INDEX idx_events_id ON events (id);"
	                      "CREATE INDEX idx_events_call_id ON events (call_id);"
	                      "";
	char *errmsg = nullptr;
	int rc = sqlite3_exec(db, indices, nullptr, nullptr, &errmsg);

	if (rc != SQLITE_OK)
	{
		printf("/!\\ Could not create indices:\n");
		printf("%s\n", errmsg);
	}

	std::cout << "(i) Serialization done" << std::endl;
}

/**
 * @brief Writes the summary to a file
 * @param filename Name of the file that the summary shall be written to
 */
void sgxperf::EventStore::write_summary(std::string &filename)
{
	if (config->is_benchmark_mode_enabled())
	{
		std::cout << "(i) Benchmark mode, will not write to file" << std::endl;
		//std::cerr << "," << bench_aex_count << std::endl;
		auto tit = finished_thread_events.begin();
		auto thread = *tit;
		while (thread->sql_id != 0)
		{
			thread = *(tit++);
		}
		bool first = true;
		while (!thread->events.empty())
		{
			auto e = thread->events.front();
			thread->events.pop();
			if (e->get_type() != sgxperf::EventType::EnclaveECallEvent)
				continue;
			auto ee = (sgxperf::EnclaveECallEvent *)(e);
			if (!first)
			{
				std::cerr << ",";
			}
			else
			{
				first = false;
			}
			std::cerr << ee->aex_counter;
		}
		std::cerr << std::endl;
		return;
	}

	std::ofstream file;
	try
	{
		create_summary();
		printf("(i) Writing out file\n");
		sqlite3 *fdb;
		int rc = sqlite3_open(filename.c_str(), &fdb);
		if (rc == SQLITE_OK)
		{
			sqlite3_backup *p = sqlite3_backup_init(fdb, "main", db, "main");
			if (p)
			{
				sqlite3_backup_step(p, -1);
				sqlite3_backup_finish(p);
			}
			rc = sqlite3_errcode(fdb);
		}
		else
		{
			std::cout << "!!! Could not open file for writing!" << std::endl;
		}
		sqlite3_close(fdb);
	}
	catch (std::exception &e)
	{
		printf("!!! Error opening file, creating summary or closing file\n");
	}

	sqlite3_close(db);
}

/**
 * @brief Finalize the EventStore to stop accepting events
 */
void sgxperf::EventStore::finalize()
{
	finalized = true;

	timespec t = {};
	clock_gettime(CLOCK_MONOTONIC_RAW, &t);
	end_time = static_cast<uint64_t>(t.tv_nsec + t.tv_sec * 1000000000);
	auto it =  thread_events.begin();
	while (it != thread_events.end())
	{
		auto thread = it->second;

		// Find open calls and finalize them
		while (thread->current_call != nullptr)
		{
			auto ec = dynamic_cast<EnclaveECallEvent *>(thread->current_call);
			if (ec != nullptr)
			{
				// is an ECallEvent
				auto ecr = new sgxperf::EnclaveECallReturnEvent(ec, SGX_SUCCESS, ec->aex_counter);
				ecr->set_thread_id(ec->get_thread_id());
				ecr->set_time(end_time);
				thread->events.push(ecr);

			}
			else
			{
				auto oc = dynamic_cast<EnclaveOCallEvent *>(thread->current_call);
				if (oc != nullptr)
				{
					// is an ECallEvent
					auto ocr = new sgxperf::EnclaveOCallReturnEvent(oc, SGX_SUCCESS);
					ocr->set_thread_id(oc->get_thread_id());
					ocr->set_time(end_time);
					thread->events.push(ocr);

				}
			}

			thread->current_call = thread->current_call->get_previous_call();
		}

		finished_thread_events.push_back(thread);
		it++;
	}
	thread_events.clear();

}
