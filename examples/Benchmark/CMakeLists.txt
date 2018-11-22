set(LDS_NAME enclave.lds)

add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/enclave_u.c ${CMAKE_CURRENT_SOURCE_DIR}/src/enclave_u.h ${CMAKE_CURRENT_SOURCE_DIR}/src/enclave_t.c ${CMAKE_CURRENT_SOURCE_DIR}/src/enclave_t.h
        COMMAND ${SGXSDK_EDGER8R} --search-path misc --search-path ${SGXSDK_INCLUDE_DIRS} --untrusted-dir src --trusted-dir src enclave.edl
        DEPENDS misc/enclave.edl
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )

add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/misc/enclave.pem
        COMMAND openssl genrsa -out misc/enclave.pem -3 3072
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_custom_target(generate_key_bench DEPENDS misc/enclave.pem)

set(BENCH_APP_SOURCE_FILES
        src/app.cpp
        src/enclave_u.c)

set(BENCH_ENC_SOURCE_FILES
        src/enclave.cpp
        src/enclave_t.c)

set(ENCL_FLAGS "-Wl,--no-undefined -nostdlib -nodefaultlibs -nostartfiles -L${SGXSDK_ENCL_LIB_DIR} -Wl,--whole-archive \
        -l${SGXSDK_TRTS} -Wl,--no-whole-archive -Wl,--start-group -l${SGXSDK_TSTDC} -l${SGXSDK_TSTDCXX} -l${SGXSDK_TCRYPTO} \
        -l${SGXSDK_TSERVICE} -Wl,--end-group -Wl,-Bstatic -Wl,-Bsymbolic -Wl,--no-undefined -Wl,-pie,-eenclave_entry \
        -Wl,--export-dynamic -Wl,--defsym,__ImageBase=0 -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/misc/${LDS_NAME}")

set(ENCL_SIM_FLAGS "-Wl,--no-undefined -nostdlib -nodefaultlibs -nostartfiles -L${SGXSDK_ENCL_LIB_DIR} -Wl,--whole-archive \
        -l${SGXSDK_TRTS_SIM} -Wl,--no-whole-archive -Wl,--start-group -l${SGXSDK_TSTDC} -l${SGXSDK_TSTDCXX} -l${SGXSDK_TCRYPTO} \
        -l${SGXSDK_TSERVICE_SIM} -Wl,--end-group -Wl,-Bstatic -Wl,-Bsymbolic -Wl,--no-undefined -Wl,-pie,-eenclave_entry \
        -Wl,--export-dynamic -Wl,--defsym,__ImageBase=0 -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/misc/${LDS_NAME}")

add_library(benchenclave SHARED EXCLUDE_FROM_ALL ${BENCH_ENC_SOURCE_FILES})
target_include_directories(benchenclave PUBLIC ${SGXSDK_ENCL_CXX_INCLUDE_DIRS})
target_compile_options(benchenclave PUBLIC -nostdinc -nostdinc++ -fvisibility=hidden -fpie -fstack-protector -std=c++03)
target_link_libraries(benchenclave PUBLIC ${ENCL_FLAGS})
add_dependencies(benchenclave generate_key_bench)
#set_target_properties(enclave PROPERTIES LINK_FLAGS ${ENCL_FLAGS})

add_library(benchenclavesim SHARED EXCLUDE_FROM_ALL ${BENCH_ENC_SOURCE_FILES})
target_include_directories(benchenclavesim PUBLIC ${SGXSDK_ENCL_CXX_INCLUDE_DIRS})
target_compile_options(benchenclavesim PUBLIC -nostdinc -nostdinc++ -fvisibility=hidden -fpie -fstack-protector -std=c++03)
target_link_libraries(benchenclavesim PUBLIC ${ENCL_SIM_FLAGS})
add_dependencies(benchenclavesim generate_key_bench)

add_custom_command(TARGET benchenclave POST_BUILD
        COMMAND ${SGXSDK_SIGN} sign -key misc/enclave.pem -enclave ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libbenchenclave.so -out ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libbenchenclave.signed.so -config misc/enclave.config.xml
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_custom_command(TARGET benchenclavesim POST_BUILD
        COMMAND ${SGXSDK_SIGN} sign -key misc/enclave.pem -enclave ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libbenchenclavesim.so -out ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libbenchenclavesim.signed.so -config misc/enclave.config.xml
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(benchapp EXCLUDE_FROM_ALL ${BENCH_APP_SOURCE_FILES})
target_include_directories(benchapp PUBLIC ${SGXSDK_INCLUDE_DIRS})
target_link_libraries(benchapp PUBLIC ${SGXSDK_URTS} ${SGXSDK_UAE} pthread)

add_executable(benchappsim EXCLUDE_FROM_ALL ${BENCH_APP_SOURCE_FILES})
target_include_directories(benchappsim PUBLIC ${SGXSDK_INCLUDE_DIRS})
target_link_libraries(benchappsim PUBLIC ${SGXSDK_URTS_SIM} ${SGXSDK_UAE_SIM} pthread)
