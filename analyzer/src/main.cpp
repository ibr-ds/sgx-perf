#include "main.h"

#include <unistd.h>

void usage(char *exe)
{
	std::cout << exe << " [args] out-pid.db" << std::endl;
	std::cout << std::endl;
	std::cout << "Arguments, defaults in []:" << std::endl;
	std::cout << "-e num\t\t[num = 0] Discard all ecalls which have less than <num> calls" << std::endl;
	std::cout << "-o num\t\t[num = 0] Discard all ocalls which have less than <num> calls" << std::endl;
	std::cout << "-p phases\t[phases = csi] Execute the given analysis phases" << std::endl;
	std::cout << "\t\tc - Analyse ecalls/ocalls" << std::endl;
	std::cout << "\t\ts - Analyse synchronisation calls" << std::endl;
	std::cout << "\t\ti - Analyse enclave interface. Implies -p c" << std::endl;
	std::cout << "-g ids\t\t[ids = \"\"] Create DOT graph descriptions for the given ids" << std::endl;
	std::cout << "\t\tExample: e1,e19,e54, will create graphs for ecalls 1, 19 and 54" << std::endl;
	std::cout << "-f\t\tDOT graph file name. Implies \"-p c\". Disables \"-d\"." << std::endl;
	std::cout << "-d\t\tRaw call data folder name. Implies \"-p c\". Disables \"-f\"" << std::endl;
	std::cout << "-l\t\tPath to EDL for \"-p i\". Optional." << std::endl;
	std::cout << "-s\t\tEDL Search Path for EDL imports. Optional." << std::endl;
	std::cout << std::endl;
}

sqlite3 *db = nullptr;

config_t config;

/**
 * General queries
 */

uint64_t EnclaveOCallEventId = 0;
uint64_t EnclaveOCallReturnEventId = 0;

int event_callback(void *arg, int count, char **data, char **columns)
{
	(void)arg;
	(void)count;
	(void)columns;
	if (std::string(data[1]) == "EnclaveOCallEvent")
	{
		EnclaveOCallEventId = strtoul(data[0], nullptr, 10);
	}
	else if (std::string(data[1]) == "EnclaveOCallReturnEvent")
	{
		EnclaveOCallReturnEventId = strtoul(data[0], nullptr, 10);
	}

	return 0;
}


void get_event_ids()
{
	std::stringstream ss;
	ss << "select id, name from event_map;";

	sql_exec(ss, event_callback);
}

/**
 * Main
 */

int main(int argc, char** argv)
{
	if (argc == 1)
	{
		usage(argv[0]);
		exit(-1);
	}

	// Default config
	config.ecall_call_minimum = 0;
	config.ocall_call_minimum = 0;
	config.phases = {true, true, true};

	config.duplication_weights.alpha = 0.35;
	config.duplication_weights.beta = 0.50;
	config.duplication_weights.gamma = 0.65;

	config.reordering_weights.alpha = 1.00;
	config.reordering_weights.beta = 0.75;
	config.reordering_weights.gamma = 0.5;

	config.merging_weights.alpha = 1.00;
	config.merging_weights.beta = 0.75;
	config.merging_weights.gamma = 0.50;
	config.merging_weights.delta = 0.25;
	config.merging_weights.epsilon = 0.35;
	config.merging_weights.lambda = 0.35;

	config.batching_weights.alpha = 1.00;
	config.batching_weights.beta = 0.75;
	config.batching_weights.gamma = 0.50;
	config.batching_weights.delta = 0.25;
	config.batching_weights.epsilon = 0.35;
	config.batching_weights.lambda = 0.35;

	config.graph = "";
	config.call_data_filename = "";

	int ch;

	while ((ch = getopt(argc, argv, "e:o:p:g:f:d:il:")) != -1) {
		switch (ch) {
			case 'e':
			{
				long lim = strtol(optarg, nullptr, 10);
				if (lim < 0)
				{
					std::cout << "Limit must be positive!" << std::endl;
					exit(1);
				}
				config.ecall_call_minimum = (uint64_t)lim;
				break;
			}
			case 'o':
			{
				long lim = strtol(optarg, nullptr, 10);
				if (lim < 0)
				{
					std::cout << "Limit must be positive!" << std::endl;
					exit(1);
				}
				config.ocall_call_minimum = (uint64_t)lim;
				break;
			}
			case 'p':
			{
				config.phases = {false, false, false};
				auto s = std::string(optarg);
				if (s.find("c") != std::string::npos)
				{
					config.phases.calls = true;
				}
				else if (s.find("s") != std::string::npos)
				{
					config.phases.sync = true;
				}
				else if (s.find("i") != std::string::npos)
				{
					config.phases.calls = true;
					config.phases.sec = true;
				}
				break;
			}
			case 'g':
			{

				auto s = std::string(optarg);
				std::string delim = ",";
				size_t pos = 0;
				std::string token;
				while ((pos = s.find(delim)) != std::string::npos) {
					token = s.substr(0, pos);

					if (token[0] == 'e')
					{
						// ecall
						auto id = strtoul(token.c_str()+1, nullptr, 10);
						config.ecall_set.insert(id);
					}
					if (token[0] == 'o')
					{
						// ocall
						auto id = strtoul(token.c_str()+1, nullptr, 10);
						config.ocall_set.insert(id);
					}

					s.erase(0, pos + delim.length());
				}
				break;
			}
			case 'f':
			{
				config.call_data_filename = "";
				config.graph = optarg;
				config.phases.calls = true;
				break;
			}
			case 'd':
			{
				config.graph = "";
				config.call_data_filename = optarg;
				config.phases.calls = true;
				break;
			}
			case 'l':
			{
				config.edl_path = std::string(optarg);
				config.phases.calls = true;
				config.phases.sec = true;
				break;
			}
			case '?':
			default:
				break;
		}
	}
	argc -= optind;
	argv += optind;

	char *dbfile = argv[0];

	std::cout << "Opening database " << dbfile << std::endl;

	int rc = sqlite3_open_v2(dbfile, &db, SQLITE_OPEN_READONLY, nullptr);
	if (rc)
	{
		std::cout << "/!\\ Could not open database: " << sqlite3_errmsg(db) << std::endl;
		sqlite3_close(db);
		exit(-1);
	}

	std::cout << "(i) Opened database file " << dbfile << std::endl;

	get_event_ids();

	std::cout << "(i) Starting Analysis " << std::endl;

	if (config.phases.calls)
		analyze_calls();

	if (config.phases.sync)
		analyze_synchro();

	if (config.phases.sec)
		analyze_security();

	if (!config.graph.empty())
		draw_graphs();

	sqlite3_close(db);
}
