/**
 * @file elfparser.h
 * @author weichbr
 */

#include <string>
#include <cstdint>

#ifndef SGX_PERF_ELFPARSER_H
#define SGX_PERF_ELFPARSER_H

/**
 * @brief struct describing the ECall table
 */
extern "C" struct ecall_table {
	size_t count; ///< Number of ECalls managed by this table
	struct {
		void *ecall_addr; ///< Function pointer of the ECall
		uint8_t is_priv; ///< Flag showing whether this ECall is private and can only be called from certain OCalls or not.
	} ecall_table[]; ///< Array mapping ECall IDs to functions pointers
};

std::string getSymbolForAddress(std::string &binary, uint64_t address);
void *get_address_for_symbol(std::string &binary, std::string &symbol_name);
struct ecall_table *getECallTable(std::string &enclave);
void closeAllFiles();

#endif //SGX_PERF_ELFPARSER_H
