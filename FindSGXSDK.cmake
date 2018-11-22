# - Try to find the SGX SDK
# Once done this will define
#
#  SGXSDK_FOUND - system has SGX SDK
#  SGXSDK_INCLUDE_DIRS - the SGX SDK include directory
#  SGXSDK_ENCL_C_INCLUDE_DIRS - the enclave C include directory
#  SGXSDK_ENCL_CXX_INCLUDE_DIRS - the enclave C++ include directory
#  SGXSDK_URTS - the urts hardware library
#  SGXSDK_URTS_SIM - the urts simulation library
#
#  Copyright (c) 2017 Nico Weichbrodt <weichbr@ibr.cs.tu-bs.de>
#
#  Redistribution and use is allowed according to the terms of the New
#  BSD license.
#  For details see the accompanying COPYING-CMAKE-SCRIPTS file.
#

# Adapted from https://github.com/iovisor/bcc/blob/master/cmake/FindLibElf.cmake

if (SGXSDK_LIBRARIES AND SGXSDK_INCLUDE_DIRS)
    set (SGXSDK_FIND_QUIETLY TRUE)
endif (SGXSDK_LIBRARIES AND SGXSDK_INCLUDE_DIRS)

find_path (SGXSDK_INCLUDE_DIRS
        NAMES
        sgx.h
        PATHS
        /opt/intel/sgxsdk/include)

find_path (ENCL_C_INCLUDE_DIRS
        NAMES
        stdio.h
        PATHS
        /opt/intel/sgxsdk/include/tlibc
        NO_DEFAULT_PATH
        NO_SYSTEM_ENVIRONMENT_PATH)

find_path (ENCL_CXX_INCLUDE_DIRS
        NAMES
        cstdio
        PATHS
        /opt/intel/sgxsdk/include/libcxx
        NO_DEFAULT_PATH
        NO_SYSTEM_ENVIRONMENT_PATH)

set(SGXSDK_ENCL_C_INCLUDE_DIRS ${SGXSDK_INCLUDE_DIRS} ${ENCL_C_INCLUDE_DIRS})
set(SGXSDK_ENCL_CXX_INCLUDE_DIRS ${SGXSDK_ENCL_C_INCLUDE_DIRS} ${ENCL_CXX_INCLUDE_DIRS})

find_library (SGXSDK_URTS
        NAMES
        sgx_urts
        PATHS
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        /opt/intel/sgxsdk/lib64
        ENV LIBRARY_PATH
        ENV LD_LIBRARY_PATH)

find_library (SGXSDK_URTS_SIM
        NAMES
        sgx_urts_sim
        PATHS
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        /opt/intel/sgxsdk/lib64
        ENV LIBRARY_PATH
        ENV LD_LIBRARY_PATH)

find_library (SGXSDK_UAE
        NAMES
        sgx_uae_service
        PATHS
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        /opt/intel/sgxsdk/lib64
        ENV LIBRARY_PATH
        ENV LD_LIBRARY_PATH)

find_library (SGXSDK_UAE_SIM
        NAMES
        sgx_uae_service_sim
        PATHS
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        /opt/intel/sgxsdk/lib64
        ENV LIBRARY_PATH
        ENV LD_LIBRARY_PATH)

set(SGXSDK_TRTS sgx_trts)
set(SGXSDK_TRTS_SIM sgx_trts_sim)
set(SGXSDK_TSERVICE sgx_tservice)
set(SGXSDK_TSERVICE_SIM sgx_tservice_sim)

set(SGXSDK_TCRYPTO sgx_tcrypto)
set(SGXSDK_TSTDC sgx_tstdc)
set(SGXSDK_TSTDCXX sgx_tstdcxx)

find_path(SGXSDK_ENCL_LIB_DIR
        NAMES
        libsgx_trts.a
        PATHS
        /opt/intel/sgxsdk/lib64
)

find_program (SGXSDK_EDGER8R
        NAMES
        sgx_edger8r
        PATHS
        /opt/intel/sgxsdk/bin/x64)

find_program (SGXSDK_SIGN
        NAMES
        sgx_sign
        PATHS
        /opt/intel/sgxsdk/bin/x64)

include (FindPackageHandleStandardArgs)


# handle the QUIETLY and REQUIRED arguments and set SGXSDK_FOUND to TRUE if all listed variables are TRUE
FIND_PACKAGE_HANDLE_STANDARD_ARGS(SGXSDK DEFAULT_MSG
        SGXSDK_URTS
        SGXSDK_URTS_SIM
        SGXSDK_UAE
        SGXSDK_UAE_SIM
        SGXSDK_TRTS
        SGXSDK_TRTS_SIM
        SGXSDK_TSERVICE
        SGXSDK_TSERVICE_SIM
        SGXSDK_INCLUDE_DIRS
        ENCL_C_INCLUDE_DIRS
        ENCL_CXX_INCLUDE_DIRS
        SGXSDK_EDGER8R
        SGXSDK_SIGN)

SET(CMAKE_REQUIRED_LIBRARIES sgx_urts sgx_urts_sim sgx_uae_service sgx_uae_service_sim sgx_trts sgx_trts_sim)
#INCLUDE(CheckCXXSourceCompiles)
#CHECK_CXX_SOURCE_COMPILES("#include <libelf.h>
#int main() {
#  Elf *e = (Elf*)0;
#  size_t sz;
#  elf_getshdrstrndx(e, &sz);
#  return 0;
#}" ELF_GETSHDRSTRNDX)

mark_as_advanced(SGXSDK_INCLUDE_DIRS SGXSDK_ENCL_C_INCLUDE_DIRS SGXSDK_ENCL_CXX_INCLUDE_DIRS SGXSDK_URTS SGXSDK_URTS_SIM SGXSDK_UAE SGXSDK_UAE_SIM SGXSDK_TRTS SGXSDK_TRST_SIM)
