/**
 * @author $author$
 */

#include <set>
#include <fstream>
#include "graph.h"

extern std::map<uint64_t, enclave_data_t> encls;

void draw_graphs()
{
	std::cout << "=== DOT graph descriptions" << std::endl;
	std::ofstream dotfile(config.graph);
	std::for_each(encls.begin(), encls.end(), [&dotfile](std::pair<const uint64_t, enclave_data_t> &p) {
		auto eid = p.first;
		/*
		std::for_each(config.ecall_graphs.begin(), config.ecall_graphs.end(), [eid](uint64_t cid) {
			std::cout << "/--" << std::endl;
			std::cout << dot_ecall_graph(eid, cid) << std::endl;
			std::cout << "\\--" << std::endl;
		});
		*/

//		std::cout << "/--" << std::endl;
		dotfile << dot_graph(eid) << std::endl;
//		std::cout << "\\--" << std::endl;
	});
	dotfile.close();
}

std::string dot_graph(uint64_t eid)
{
	std::stringstream ss;

	ss << "digraph Enclave_" << eid << " {" << std::endl;

	std::for_each(encls[eid].ecalls.begin(), encls[eid].ecalls.end(), [&ss] (call_data_t *cd) {
		if (skip_call(cd, config.ecall_set))
		{
			return;
		}

		ss << "\t" << *cd->name << " [shape=box,label=\"[" << cd->call_id << "] " << *cd->name << "\"];" << std::endl;


		for (uint64_t i = 0; i < cd->indirect_parents_data->size(); ++i)
		{
			auto &ipcd = cd->indirect_parents_data->at(i);
			if (skip_call(ipcd.call_data, config.ecall_set))
			{
				continue;
			}
			ss << "\t" << *ipcd.call_data->name << " -> " << *cd->name << " [label=\"" << ipcd.count << "\",style=dashed];" << std::endl;
		}

		for (uint64_t i = 0; i < cd->direct_parents_data->size(); ++i)
		{
			auto &dpcd = cd->direct_parents_data->at(i);
			if (skip_call(dpcd.call_data, config.ocall_set))
			{
				continue;
			}
			ss << "\t" << *dpcd.call_data->name << " -> " << *cd->name << " [label=\"" << dpcd.count  << "\"];" << std::endl;
		}
	});

	std::for_each(encls[eid].ocalls.begin(), encls[eid].ocalls.end(), [&ss] (call_data_t *cd) {
		if (skip_call(cd, config.ocall_set))
		{
			return;
		}

		ss << "\t" << *cd->name << " [label=\"[" << cd->call_id << "] " << *cd->name << "\"];" << std::endl;

		for (uint64_t i = 0; i < cd->indirect_parents_data->size(); ++i)
		{
			auto &ipcd = cd->indirect_parents_data->at(i);
			if (skip_call(ipcd.call_data, config.ocall_set))
			{
				continue;
			}
			ss << "\t" << *ipcd.call_data->name << " -> " << *cd->name << " [label=\"" << ipcd.count << "\",style=dashed];" << std::endl;
		}

		for (uint64_t i = 0; i < cd->direct_parents_data->size(); ++i)
		{
			auto &dpcd = cd->direct_parents_data->at(i);
			if (skip_call(dpcd.call_data, config.ecall_set))
			{
				continue;
			}
			ss << "\t" << *dpcd.call_data->name << " -> " << *cd->name << " [label=\"" << dpcd.count << "\"];" << std::endl;
		}
	});

	ss << "}";

	return ss.str();
}

std::string dot_ecall_graph(uint64_t eid, uint64_t start_id)
{
	std::stringstream ss;
	std::set<uint64_t> done_ecalls;
	std::set<uint64_t> done_ocalls;
	std::set<uint64_t> todo_ecalls;
	std::set<uint64_t> todo_ocalls;

	ss << "digraph " << *encls[eid].ecalls[start_id]->name << " {" << std::endl;

	todo_ecalls.insert(start_id);

	while (!todo_ecalls.empty())
	{
		auto id = *todo_ecalls.begin();
		auto cd = encls[eid].ecalls[id];

		if (!cd->has_indirect_parents && !cd->has_direct_parents)
		{
			done_ecalls.insert(id);
			todo_ecalls.erase(todo_ecalls.find(id));
			continue;
		}

		ss << "\t" << *cd->name << " [shape=box];";

		for (uint64_t i = 0; i < cd->indirect_parents_data->size(); ++i)
		{
			auto &ipcd = cd->indirect_parents_data->at(i);
			if (ipcd.count == 0)
			{
				continue;
			}
			ss << "\t" << *ipcd.call_data->name << " -> " << *cd->name << " [label=\"" << countformat(ipcd.count, cd->all_stats.calls) << "\",style=dashed];" << std::endl;
			auto d = done_ecalls.find(i);
			if (d == done_ecalls.end())
			{
				todo_ecalls.insert(i);
			}
		}

		for (uint64_t i = 0; i < cd->direct_parents_data->size(); ++i)
		{
			auto &dpcd = cd->direct_parents_data->at(i);
			if (dpcd.count == 0)
			{
				continue;
			}
			ss << "\t" << *dpcd.call_data->name << " -> " << *cd->name << " [label=\"" << countformat(dpcd.count, cd->all_stats.calls) << "\"];" << std::endl;
			auto d = done_ocalls.find(i);
			if (d == done_ocalls.end())
			{
				todo_ocalls.insert(i);
			}
		}

		done_ecalls.insert(id);
		todo_ecalls.erase(todo_ecalls.find(id));

		while (!todo_ocalls.empty())
		{
			auto oid = *todo_ocalls.begin();
			auto ocd = encls[eid].ocalls[oid];

			if (!ocd->has_indirect_parents && !ocd->has_direct_parents)
			{
				done_ocalls.insert(oid);
				todo_ocalls.erase(todo_ocalls.find(oid));
				continue;
			}

			for (uint64_t i = 0; i < ocd->indirect_parents_data->size(); ++i)
			{
				auto &ipcd = ocd->indirect_parents_data->at(i);
				if (ipcd.count == 0)
				{
					continue;
				}
				ss << "\t" << *ipcd.call_data->name << " -> " << *ocd->name << " [label=\"" << countformat(ipcd.count, ocd->all_stats.calls) << "\",style=dashed];" << std::endl;
				auto d = done_ocalls.find(i);
				if (d == done_ocalls.end())
				{
					todo_ocalls.insert(i);
				}
			}

			for (uint64_t i = 0; i < ocd->direct_parents_data->size(); ++i)
			{
				auto &dpcd = ocd->direct_parents_data->at(i);
				if (dpcd.count == 0)
				{
					continue;
				}
				ss << "\t" << *dpcd.call_data->name << " -> " << *ocd->name << " [label=\"" << countformat(dpcd.count, cd->all_stats.calls) << "\"];" << std::endl;
				auto d = done_ecalls.find(i);
				if (d == done_ecalls.end())
				{
					todo_ecalls.insert(i);
				}
			}

			done_ocalls.insert(oid);
			todo_ocalls.erase(todo_ocalls.find(oid));
		}
	}

	ss << "}";
	return ss.str();
}
