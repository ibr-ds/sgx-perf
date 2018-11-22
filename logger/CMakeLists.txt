set(LOGGER_SOURCE_FILES
        ../common/sqlite3.c
        ../common/rwlock.cpp
        ../common/elfparser.cpp
        src/main.cpp
        src/urts_calls.cpp
        src/libc_calls.cpp
        src/perf.cpp
        src/store.cpp
        src/config.cpp
        )

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,-e,libmain -Wl,--no-as-needed -O2")

find_package(LibElf REQUIRED)
find_package(SGXSDK REQUIRED)

add_library(loggersim SHARED ${LOGGER_SOURCE_FILES})
target_link_libraries(loggersim PUBLIC dl pthread ${LIBELF_LIBRARIES} ${SGXSDK_URTS_SIM})
target_include_directories(loggersim PUBLIC
        ../common
        ${SGXSDK_INCLUDE_DIRS}
        ${LIBELF_INCLUDE_DIRS})

add_library(logger SHARED ${LOGGER_SOURCE_FILES})
target_link_libraries(logger PUBLIC dl pthread ${LIBELF_LIBRARIES} ${SGXSDK_URTS})
target_include_directories(logger PUBLIC
        ../common
        ${SGXSDK_INCLUDE_DIRS}
        ${LIBELF_INCLUDE_DIRS})
target_compile_definitions(logger PUBLIC HWMODE)
