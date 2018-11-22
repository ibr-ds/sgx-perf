/**
 * @file config.cpp
 * @author weichbr
 */

#include <cstdio>
#include <iostream>
#include "config.h"

#define INI_IMPLEMENTATION
#include "ini.h"

#define CONFIG_NAME ".sgxperf"

#define COUNT_AEX_NAME "CountAEX"
#define TRACE_AEX_NAME "TraceAEX"
#define TRACE_PAGING_NAME "TracePaging"
#define USE_SAMPLING_NAME "UseSampling"
#define BENCHMODE_NAME "Benchmode"

/**
 * @brief Initializes config system.
 */
void sgxperf::Config::init()
{
	FILE *fp = fopen(CONFIG_NAME, "r");
	if (!fp)
	{
		// No config file
		std::cout << "(i) No config file found, load defaults" << std::endl;
		return;
	}
	fseek( fp, 0, SEEK_END );
	size_t size = static_cast<size_t>(ftell(fp ));
	fseek( fp, 0, SEEK_SET );
	auto data = new char[size+1];
	fread( data, 1, size, fp );
	data[ size ] = '\0';
	fclose( fp );

	ini_t *ini = ini_load(data, nullptr);
	delete[] data;

	int count_aex_index = ini_find_property(ini, INI_GLOBAL_SECTION, COUNT_AEX_NAME, sizeof(COUNT_AEX_NAME));
	if (count_aex_index != INI_NOT_FOUND)
	{
		char const *count_aex_string = ini_property_value(ini, INI_GLOBAL_SECTION, count_aex_index);
		if (count_aex_string != nullptr)
		{
			if (strncmp("true", count_aex_string, 4) == 0)
			{
				count_aex = true;
				std::cout << "(i) Enabled AEX counting" << std::endl;
			}
		}
	}

	int trace_aex_index = ini_find_property(ini, INI_GLOBAL_SECTION, TRACE_AEX_NAME, sizeof(TRACE_AEX_NAME));
	if (trace_aex_index != INI_NOT_FOUND)
	{
		char const *trace_aex_string = ini_property_value(ini, INI_GLOBAL_SECTION, trace_aex_index);
		if (trace_aex_string != nullptr)
		{
			if (strncmp("true", trace_aex_string, 4) == 0)
			{
				count_aex = true;
				std::cout << "(i) Enabled AEX counting" << std::endl;
				trace_aex = true;
				std::cout << "(i) Enabled AEX tracing" << std::endl;
			}
		}
	}

	int trace_paging_index = ini_find_property(ini, INI_GLOBAL_SECTION, TRACE_PAGING_NAME, sizeof(TRACE_PAGING_NAME));
	if (trace_paging_index != INI_NOT_FOUND)
	{
		char const *trace_paging_string = ini_property_value(ini, INI_GLOBAL_SECTION, trace_paging_index);
		if (trace_paging_string != nullptr)
		{
			if (strncmp("true", trace_paging_string, 4) == 0)
			{
				trace_paging = true;
				std::cout << "(i) Enabled EPC page tracing, this needs root permissions" << std::endl;
			}
		}
	}

	int use_sampling_index = ini_find_property(ini, INI_GLOBAL_SECTION, USE_SAMPLING_NAME, sizeof(USE_SAMPLING_NAME));
	if (use_sampling_index != INI_NOT_FOUND)
	{
		char const *use_sampling_string = ini_property_value(ini, INI_GLOBAL_SECTION, use_sampling_index);
		if (use_sampling_string != nullptr)
		{
			if (strncmp("true", use_sampling_string, 4) == 0)
			{
				record_samples = true;
				std::cout << "(i) Enabled sample recording, this needs root permissions" << std::endl;
			}
		}
	}

	int benchmode_index = ini_find_property(ini, INI_GLOBAL_SECTION, BENCHMODE_NAME, sizeof(BENCHMODE_NAME));
	if (benchmode_index != INI_NOT_FOUND)
	{
		char const *benchmode_string = ini_property_value(ini, INI_GLOBAL_SECTION, benchmode_index);
		if (benchmode_string != nullptr)
		{
			if (strncmp("true", benchmode_string, 4) == 0)
			{
				benchmode = true;
				std::cout << "(i) Enabled benchmark mode, events will not be serialized" << std::endl;
			}
		}
	}
}
