/**
 * @author weichbr
 */

#include <fstream>
#include "security.h"

extern std::map<uint64_t, enclave_data_t> encls;

static inline void ltrim(std::string &s) {
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
		return !std::isspace(ch);
	}));
}

static inline void rtrim(std::string &s) {
	s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
		return !std::isspace(ch);
	}).base(), s.end());
}

static bool in_block_comment = false;

static inline void remove_comments(std::string &s)
{
	if (!in_block_comment)
	{
		if (s.find("//") != std::string::npos)
			s.erase(s.begin() + s.find("//"), s.end());
		if (s.find("/*") != std::string::npos)
		{
			s.erase(s.begin() + s.find("/*"), s.end());
			in_block_comment = true;
		}
	}
	if (in_block_comment)
	{
		if (s.find("*/") != std::string::npos)
		{
			s.erase(s.begin(), s.begin() + s.find("*/") + 2);
			in_block_comment = false;
		}
		else
		{
			s.erase(s.begin(), s.end());
		}
	}
}

void analyze_security()
{
	std::cout << "=== OCall interface security hints" << std::endl;
	if (config.edl_path.empty())
	{
		std::cout << "(i) No EDL specified, printing narrowest interface for each OCall." << std::endl;

		std::for_each(encls.begin(), encls.end(), [](std::pair<const uint64_t, enclave_data_t> &p) {
			auto eid = p.first;
			std::stringstream ss;
			std::for_each(encls[eid].ocalls.begin(), encls[eid].ocalls.end(), [eid, &ss](call_data_t *ocd) {
				ss << *ocd->name;

				bool first = true;
				std::for_each(encls[eid].ecalls.begin(), encls[eid].ecalls.end(), [ocd, &first, &ss](call_data_t *ecd) {
					auto &od = ecd->direct_parents_data->at(ocd->call_id);
					if (od.count == 0)
						return;
					if (first)
					{
						ss << " allow (" << *ecd->name;
						first = false;
						return;
					}
					ss << ", " << *ecd->name;
				});

				if (!first)
					ss << ")";
				ss << ";";

				auto s = ss.str();
				ss.str("");

				if (s[s.length()-2] == ')')
				{
					std::cout << s << std::endl;
				}

			});
		});

		return;
	}

	std::ifstream edl(config.edl_path);
	if (!edl.is_open())
	{
		std::cout << "/!\\ Failed to open EDL " << config.edl_path << std::endl;
		return;
	}
	std::cout << "(i) Reading EDL..." << std::endl;

	std::map<std::string, std::set<std::string> > ocalls;
	// TODO: handle EDL imports

	std::string line;
	uint8_t state = 0;
	while (std::getline(edl, line))
	{
		ltrim(line);
		rtrim(line);
		// remove comments

		remove_comments(line);

		switch (state)
		{
			case 0:
			{
				if (line.find("untrusted") != std::string::npos)
				{
					state = 1;
				}
				break;
			}
			case 1:
			{
				if (line.empty())
					break;

				if (line == "};")
					state = 0;

				// first, look for allow
				auto allow = line.find("allow");
				std::set<std::string> allowed;
				size_t start = line.find('(', allow) + 1;
				size_t end = 0;
				bool loop = true;
				if (allow != std::string::npos)
				{
					while (loop)
					{
						if (end != 0)
							start = end + 1;
						end = line.find(',', start);
						if (end == std::string::npos)
						{
							end = line.find(')', start);
							loop = false;
						}

						auto sym = line.substr(start, end-start);
						//std::cout << ">" << sym << "<" << std::endl;
						allowed.emplace(sym);
					}

					line = line.substr(0, allow - 1);
				}

				int op = 0, cp = 0;
				start = 0, end = 0;
				// Backtrack through the string to find symbol name
				for (size_t i = line.length(); i > 0; --i)
				{
					if (line[i] == ')')
						cp++;
					if (line[i] == '(')
						op++;

					if (op > 0 && op == cp)
					{
						// at the end of the arguments
						end = i;
						while(i > 0 && line[i--] != ' ');
						start = i+2;
						break;
					}
				}
				auto sym = line.substr(start, end-start);
				//std::cout << ">" << sym << "<" << std::endl;
				ocalls[sym] = allowed;
			}
		}
	}

	std::for_each(encls.begin(), encls.end(), [&ocalls](std::pair<const uint64_t, enclave_data_t> &p) {
		auto eid = p.first;
		std::for_each(encls[eid].ocalls.begin(), encls[eid].ocalls.end(), [eid, &ocalls](call_data_t *ocd) {
			std::set<std::string> edlallowed;

			bool first = true;
			std::for_each(encls[eid].ecalls.begin(), encls[eid].ecalls.end(), [ocd, &first, &edlallowed](call_data_t *ecd) {
				auto &od = ecd->direct_parents_data->at(ocd->call_id);
				if (od.count == 0)
					return;
				if (first)
				{
					edlallowed.emplace(*ecd->name);
					first = false;
					return;
				}
				edlallowed.emplace(*ecd->name);
			});

			auto narallowed = ocalls[*ocd->name];
			std::set<std::string> in_edl;

			std::set_difference(edlallowed.begin(), edlallowed.end(), narallowed.begin(), narallowed.end(), std::inserter(in_edl, in_edl.begin()));

			if (!in_edl.empty())
			{
				// there were calls inside the edl that don't need to be there
				std::cout << "Interface for " << *ocd->name << " can be narrowed. Remove functions" << std::endl;
				auto it = in_edl.begin();
				while(it != in_edl.end())
				{
					std::cout << "\t" << *it << std::endl;
					it++;
				}
			}
		});
	});
}
