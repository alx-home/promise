cmake_minimum_required(VERSION 3.20)

project(promise LANGUAGES CXX)

option(PROMISE_MEMCHECK "Enable promise leak detection" OFF)

add_library(promise src/Promise.cpp)
add_library(alx-home::promise ALIAS promise)
# target_compile_definitions(promise PRIVATE PROMISE_MEMCHECK)

set_target_properties(promise PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(promise PROPERTIES CXX_STANDARD 26)
set_target_properties(promise PROPERTIES CMAKE_CXX_STANDARD_REQUIRED ON)
set_target_properties(promise PROPERTIES CMAKE_CXX_EXTENSIONS ON)

if(PROMISE_MEMCHECK)
    target_compile_definitions(promise PUBLIC PROMISE_MEMCHECK)
endif(PROMISE_MEMCHECK)

target_include_directories(promise INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(promise PRIVATE
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
    target_compile_options(promise PUBLIC /W4 ${COMPILE_OPTIONS})
else()
    target_compile_options(promise PUBLIC 
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