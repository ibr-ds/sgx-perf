/**
 * @author weichbr
 */

#include "main.h"
#include <iostream>
#include <iomanip>

void sql_exec(const char *sql, int (*callback)(void*,int,char**,char**), void *arg)
{
	char *errmsg = nullptr;
	//std::cout << "--- Executing " << sql << std::endl;
	int rc = sqlite3_exec(db, sql, callback, arg, &errmsg);
	if (rc != SQLITE_OK)
	{
		printf("/!\\ Could not execute statement: %s\n", errmsg);
		printf("Statement was: %s\n", sql);
		sqlite3_free(errmsg);
		sqlite3_close(db);
		exit(-1);
	}
}

std::string timeformat(uint64_t ns, bool print_ns)
{
	std::stringstream ss;
	uint64_t us = 0, ms = 0, s = 0;
	if (ns < 1000)
	{
		ss << ns << " ns";
		goto end;
	}
	us = ns / 1000;
	if (us < 1000)
	{
		ss << us << " µs";
		goto end;
	}
	ms = us / 1000;
	if (ms < 1000)
	{
		ss << ms << " ms";
		goto end;
	}
	s = ms / 1000;
	ss << s << " s";
	end:
	if (print_ns)
	{
		ss << " (" << ns << " ns)";
	}
	return ss.str();
}


std::string countformat(uint64_t c, uint64_t m, bool color)
{
	std::stringstream ss;
	if (c == 0)
	{
		ss << "0 (0%)";
		return ss.str();
	}
	double p = (c/(double)m)*100.0;
	if (color)
	{
		if (p >= 75)
		{
			ss << RED();
		}
		else if (p >= 30)
		{
			ss << YELLOW();
		}
		else
		{
			ss << GREEN();
		}
	}
	ss << c;
	ss << " (" << std::setprecision(5) << p << "%)";
	if (color)
		ss << NORMAL();
	return ss.str();
}

/**
 * @brief Executes the SQL query inside the given string.
 * @param sql
 */
void sql_exec(std::string const &sql, int (*callback)(void*,int,char**,char**), void *arg)
{
	sql_exec((char *)sql.c_str(), callback, arg);
}

/**
 * @brief Executes the SQL query inside the given string stream.
 * @param ss
 */
void sql_exec(std::stringstream &ss, int (*callback)(void*,int,char**,char**), void *arg)
{
	sql_exec(ss.str(), callback, arg);
	ss.str(std::string());
}

bool hasEnding (std::string const &fullString, std::string const &ending) {
	if (fullString.length() >= ending.length()) {
		return (0 == fullString.compare (fullString.length() - ending.length(), ending.length(), ending));
	} else {
		return false;
	}
}

bool skip_call(call_data_t *cd, std::set<uint64_t> &set)
{
	if (cd == nullptr)
	{
		return true;
	}

	if (set.empty())
	{
		if (cd->all_stats.calls == 0)
		{
			return true;
		}
	}
	else
	{
		if (set.find(cd->call_id) == set.end())
		{
			return true;
		}
	}

	return false;
}
