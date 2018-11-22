/**
 * @file elfparser.cpp
 * @author weichbr
 */

#include "elfparser.h"

#include <libelf.h>
#include <gelf.h>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <cstring>
#include <dlfcn.h>
#include <link.h>
#include <map>
#include <memory>
#include <tuple>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>
#include <iostream>


static std::map<std::string, int> *get_file_map()
{
	static std::map<std::string, int> file_map;
	return &file_map;
};

/*
 * Closes all open files opened by @c openOrGetFile
 */
void closeAllFiles()
{
	auto file_map = get_file_map();
	auto it = file_map->begin();
	while (it != file_map->end())
	{
		close(it->second);
		file_map->erase(it);
		it++;
	}
}

/*
 * Returns the file descriptor for the file @p path. If the file has not been opened before, it will open it and cache the descriptor.
 */
static int openOrGetFile(std::string &path)
{
	auto file_map = get_file_map();
	//printf("(i) Opening file %s\n", path.c_str());
	auto it = file_map->find(path);
	if (it == file_map->end())
	{
		//printf("(i) File not yet opened\n");
		int fd = open(path.c_str(), O_RDONLY);
		if (fd < 0)
		{
			//printf("!!! Could not open %s!\n", path.c_str());
			return fd;
		}
		it = file_map->insert(std::pair<std::string, int>(path, fd)).first;
		//printf("(i) Opened file: %d\n", it->second);
		return it->second;
	}
	//printf("(i) Returning cached file descriptor %d\n", it->second);
	return it->second;
}

std::tuple<GElf_Sym *, std::string, uint64_t> getSymbolInfo(std::string binary, Elf64_Addr address, std::string name)
{
	Elf *elf = NULL;
	Elf_Scn *scn = NULL;
	Elf_Data *edata = NULL;
	int fd = openOrGetFile(binary);

	if (fd < 0)
	{
		std::cout << "!!! Could not open file: " << binary << " (" << strerror(errno) << std::endl;
		return std::tuple<GElf_Sym *, std::string, uint64_t>(nullptr, std::string(""), 0);
	}

	/* Check libelf version first */
	if(elf_version(EV_CURRENT) == EV_NONE)
	{
		printf("WARNING Elf Library is out of date!\n");
	}

	// Iterate through section headers again this time well stop when we find symbols
	elf = elf_begin(fd, ELF_C_READ, NULL);

	uint64_t offset = 0;
	uint64_t symbol_count;
	uint64_t i;
	//printf("(i) Parsing ELF\n");
	while((scn = elf_nextscn(elf, scn)) != NULL)
	{
		GElf_Shdr shdr = {};
		gelf_getshdr(scn, &shdr);
		//printf("(i) ELF section\n");

		if(shdr.sh_type == SHT_PROGBITS && (shdr.sh_flags & (SHF_WRITE | SHF_ALLOC)))
		{
			offset = shdr.sh_addr - shdr.sh_offset;
		}
		// When we find a section header marked SHT_SYMTAB stop and get symbols
		if(shdr.sh_type == SHT_SYMTAB)
		{
			// edata points to our symbol table
			edata = elf_getdata(scn, edata);

			// how many symbols are there? this number comes from the size of
			// the section divided by the entry size
			symbol_count = shdr.sh_size / shdr.sh_entsize;
			//printf("(i) %lu symbols\n", symbol_count);

			// loop through to grab all symbols
			for(i = 0; i < symbol_count; i++)
			{
				// libelf grabs the symbol data using gelf_getsym()
				auto *sym = new GElf_Sym();
				gelf_getsym(edata, (uint32_t)i, sym);

				if (address != 0 && sym->st_value == address)
				{
					return std::tuple<GElf_Sym *, std::string, uint64_t>(sym, std::string(elf_strptr(elf, shdr.sh_link, sym->st_name)), offset);
				}

				if (!name.empty() && strcmp(name.c_str(), elf_strptr(elf, shdr.sh_link, sym->st_name)) == 0)
				{
					return std::tuple<GElf_Sym *, std::string, uint64_t>(sym, std::string(elf_strptr(elf, shdr.sh_link, sym->st_name)), offset);
				}

				delete sym;
			}

		}
	}
	return std::tuple<GElf_Sym *, std::string, uint64_t>(nullptr, std::string(""), 0);
}

void *get_address_for_symbol(std::string &binary, std::string &symbol_name)
{
	auto sym = std::get<0>(getSymbolInfo(binary, 0, symbol_name));
	if (sym == nullptr)
	{
		return nullptr;
	}
	auto address = sym->st_value;
	delete sym;
	return reinterpret_cast<void *>(address);
}

/**
 * @brief Tries to resolve an @p address to a symbol name in a given @p binary.
 * @param[in] binary The path to the binary.
 * @param[in] address The address to resolve the symbol name for.
 * @return The symbol name or empty string.
 */
std::string getSymbolForAddress(std::string &binary, uint64_t address)
{
	auto r = getSymbolInfo(binary, address, "");
	delete std::get<0>(r);
	return std::get<1>(r);
};

struct ecall_table *getECallTable(std::string &enclave)
{
	Elf64_Addr ecall_table_addr = 0;

	auto syminfo = getSymbolInfo(enclave, 0, "g_ecall_table");
	if (std::get<0>(syminfo)!= nullptr)
	{
		ecall_table_addr = std::get<0>(syminfo)->st_value;
		//printf("(i) ECall table @ %p\n", (void*)ecall_table_addr);
		delete std::get<0>(syminfo);
	}

	if (ecall_table_addr == 0)
	{
		return nullptr;
	}

	int fd = openOrGetFile(enclave);
	struct stat st = {};
	stat(enclave.c_str(), &st);
	size_t fsize = st.st_size;

	auto file = (uint8_t*)mmap(nullptr, fsize, PROT_READ, MAP_SHARED, fd, 0);
	auto orig_table = (struct ecall_table *)(file + (ecall_table_addr - std::get<2>(syminfo)));

	size_t size = sizeof(size_t) + orig_table->count*(sizeof(void *) + sizeof(uint64_t)); //  Because of padding, we allocate sizeof(uint64_t) instead of sizeof(uint8_t)
	//printf("(i) Table size: %lu\n", size);

	auto copy_table = (struct ecall_table *)malloc(size);
	memset(copy_table, 0, size);
	copy_table->count = orig_table->count;
	//printf("(i) Copying %lu ECalls\n", copy_table->count);
	for (size_t c = 0; c < copy_table->count; ++c)
	{
		copy_table->ecall_table[c].ecall_addr = ((uint8_t*)orig_table->ecall_table[c].ecall_addr);
		copy_table->ecall_table[c].is_priv = orig_table->ecall_table[c].is_priv;
	}
	munmap(file, fsize);
	return copy_table;
}

