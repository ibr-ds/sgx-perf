set(WS_SOURCE_FILES
        ../common/elfparser.cpp
        ../common/rwlock.cpp
        src/main.cpp)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,-e,libmain -Wl,--no-as-needed -O2")

find_package(LibElf REQUIRED)
find_package(SGXSDK REQUIRED)

add_library(enclwssim SHARED ${WS_SOURCE_FILES})
target_link_libraries(enclwssim PUBLIC dl ${LIBELF_LIBRARIES} ${SGXSDK_URTS_SIM})
target_include_directories(enclwssim PUBLIC
        ../common
        ${SGXSDK_INCLUDE_DIRS}
        ${LIBELF_INCLUDE_DIRS})

add_library(enclws SHARED ${WS_SOURCE_FILES})
target_link_libraries(enclws PUBLIC dl ${LIBELF_LIBRARIES} ${SGXSDK_URTS})
target_include_directories(enclws PUBLIC
        ../common
        ${SGXSDK_INCLUDE_DIRS}
        ${LIBELF_INCLUDE_DIRS})
target_compile_definitions(enclws PUBLIC HWMODE)
