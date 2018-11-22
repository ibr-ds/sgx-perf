set(AL_SOURCE_FILES
        ../common/elfparser.cpp
        ../common/rwlock.cpp
        ../common/sqlite3.c
        src/main.cpp
        src/synchro.cpp
        src/util.cpp
        src/calls.cpp
        src/graph.cpp
        src/security.cpp)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -g -O0")

find_package(LibElf REQUIRED)

add_executable(analyzer ${AL_SOURCE_FILES})
target_link_libraries(analyzer PUBLIC dl pthread ${LIBELF_LIBRARIES})
target_include_directories(analyzer PUBLIC
        ../common
        ${LIBELF_INCLUDE_DIRS})
