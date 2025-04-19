cmake_minimum_required(VERSION 3.20)

project(alx-home_promise LANGUAGES CXX)

option(PROMISE_MEMCHECK_RELEASE "Enable promise leak detection in release mode" OFF)
option(PROMISE_MEMCHECK_DEBUG "Enable promise leak detection in debug mode" ON)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(PROMISE_MEMCHECK ${PROMISE_MEMCHECK_DEBUG})
else()
    set(PROMISE_MEMCHECK ${PROMISE_MEMCHECK_RELEASE})
endif()

add_library(alx-home_promise src/Promise.cpp)
add_library(alx-home::promise ALIAS alx-home_promise)
# target_compile_definitions(promise PRIVATE PROMISE_MEMCHECK)

set_target_properties(alx-home_promise PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(alx-home_promise PROPERTIES CXX_STANDARD 26)
set_target_properties(alx-home_promise PROPERTIES CMAKE_CXX_STANDARD_REQUIRED ON)
set_target_properties(alx-home_promise PROPERTIES CMAKE_CXX_EXTENSIONS ON)

if(PROMISE_MEMCHECK)
    target_compile_definitions(alx-home_promise PUBLIC PROMISE_MEMCHECK)
endif(PROMISE_MEMCHECK)

target_include_directories(alx-home_promise INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(alx-home_promise PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include/promise
)

set(COMPILE_OPTIONS
    -std=c++2c
    "$<$<CONFIG:DEBUG>:-DDEBUG>"
    -Wall -Wextra -Wpedantic -Wcast-align -Waddress-of-packed-member
    -ftemplate-backtrace-limit=0
    "$<$<CONFIG:Release>:-O3>"
    "$<$<CONFIG:Debug>:-O0>"
)

# set(SANITIZE "address")

if(DEFINED SANITIZE)
    list(APPEND COMPILE_OPTIONS
        -fsanitize=${SANITIZE}
    )
endif(DEFINED SANITIZE)

# if(DEFINED ADDRESS_SANITIZER)
#     list(APPEND COMPILE_OPTIONS 
#         "-DADDRESS_SANITIZER"
#         -fsanitize-recover=address
#     )
# endif(DEFINED ADDRESS_SANITIZER)

if(MSVC)
    list(TRANSFORM COMPILE_OPTIONS PREPEND "-clang:")
    target_compile_options(alx-home_promise PUBLIC /W4 ${COMPILE_OPTIONS})
else()
    target_compile_options(alx-home_promise PUBLIC 
        -export-dynamic
        -ggdb3 -pg -g
        ${COMPILE_OPTIONS}
        -D_GNU_SOURCE
        -Wno-psabi
    )
endif()


# set(MAJOR_VERSION 1)
# set(MINOR_VERSION 0)
# set(REVISION 0)

# set(VERSION ${MAJOR_VERSION}.${MINOR_VERSION}.${REVISION})

# set_target_properties(vfrnav PROPERTIES VERSION ${MAJOR_VERSION})
# set_target_properties(vfrnav PROPERTIES SOVERSION ${MAJOR_VERSION}.${MINOR_VERSION})