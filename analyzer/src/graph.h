/**
 * @author $author$
 */

#ifndef SGX_PERF_GRAPH_H
#define SGX_PERF_GRAPH_H

#include <string>
#include <map>
#include "main.h"

void draw_graphs();
std::string dot_graph(uint64_t eid);
std::string dot_ecall_graph(uint64_t eid, uint64_t start_id);

#endif //SGX_PERF_GRAPH_H
