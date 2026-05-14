cmake_minimum_required(VERSION 3.20)

project(alx-home_promise LANGUAGES CXX)

include(CMakeParseArguments)
include(FetchContent)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  set(PROMISE_BUILD_TESTS_DEFAULT ON)
else()
  set(PROMISE_BUILD_TESTS_DEFAULT OFF)
endif()

option(PROMISE_BUILD_TESTS "Build the promise test executable" ${PROMISE_BUILD_TESTS_DEFAULT})
option(PROMISE_FETCH_BUILD_TOOLS "Fetch build_tools when win32 helper commands are unavailable" ON)
option(PROMISE_FETCH_CPP_UTILS "Fetch cpp_utils when alx-home::cpp_utils is unavailable" ON)

set(PROMISE_BUILD_TOOLS_GIT_REPOSITORY "git@github.com:alx-home/build_tools.git" CACHE STRING "Git repository used to fetch build_tools")
set(PROMISE_BUILD_TOOLS_GIT_TAG "release" CACHE STRING "Git tag or branch used to fetch build_tools")
set(PROMISE_CPP_UTILS_GIT_REPOSITORY "https://github.com/alx-home/cpp_utils.git" CACHE STRING "Git repository used to fetch cpp_utils")
set(PROMISE_CPP_UTILS_GIT_TAG "release" CACHE STRING "Git tag or branch used to fetch cpp_utils")

if(NOT COMMAND win32_library OR NOT COMMAND win32_executable)
  if(NOT PROMISE_FETCH_BUILD_TOOLS)
    message(FATAL_ERROR "win32 helper commands not found and PROMISE_FETCH_BUILD_TOOLS is OFF")
  endif()

  FetchContent_Declare(
    alx_home_build_tools
    GIT_REPOSITORY ${PROMISE_BUILD_TOOLS_GIT_REPOSITORY}
    GIT_TAG ${PROMISE_BUILD_TOOLS_GIT_TAG}
  )
  FetchContent_GetProperties(alx_home_build_tools)

  if(NOT alx_home_build_tools_POPULATED)
    FetchContent_Populate(alx_home_build_tools)
  endif()

  include(${alx_home_build_tools_SOURCE_DIR}/cmake/win32_library.cmake)
  include(${alx_home_build_tools_SOURCE_DIR}/cmake/win32_executable.cmake)
endif()

if(NOT TARGET alx-home::cpp_utils)
  if(NOT PROMISE_FETCH_CPP_UTILS)
    message(FATAL_ERROR "alx-home::cpp_utils target not found and PROMISE_FETCH_CPP_UTILS is OFF")
  endif()

  FetchContent_Declare(
    alx_home_cpp_utils
    GIT_REPOSITORY ${PROMISE_CPP_UTILS_GIT_REPOSITORY}
    GIT_TAG ${PROMISE_CPP_UTILS_GIT_TAG}
  )
  FetchContent_MakeAvailable(alx_home_cpp_utils)
endif()

option(PROMISE_MEMCHECK_RELEASE "Enable promise leak detection in release mode" OFF)
option(PROMISE_MEMCHECK_DEBUG "Enable promise leak detection in debug mode" ON)
option(PROMISE_MEMCHECK_FULL "Dump leaked promises" ON)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(PROMISE_MEMCHECK ${PROMISE_MEMCHECK_DEBUG})
else()
  set(PROMISE_MEMCHECK ${PROMISE_MEMCHECK_RELEASE})
endif()

win32_library(TARGET_NAME alx-home_promise
    FILES
  src/Memcheck.cpp
  src/Reject.cpp
  src/Resolve.cpp
  src/Promise.cpp
  src/CVPromise.cpp
  src/StatePromise.cpp
  src/MessageQueue.cpp
)
add_library(alx-home::promise ALIAS alx-home_promise)

target_compile_features(alx-home_promise PUBLIC cxx_std_23)

target_include_directories(alx-home_promise INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(alx-home_promise PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include/promise
)

target_link_libraries(alx-home_promise PUBLIC alx-home::cpp_utils)

if(PROMISE_BUILD_TESTS)
  add_subdirectory(test)
endif()

if(PROMISE_MEMCHECK)
  target_compile_definitions(alx-home_promise PUBLIC PROMISE_MEMCHECK)

  if(PROMISE_BUILD_TESTS)
    target_compile_definitions(promise_test PUBLIC PROMISE_MEMCHECK)
  endif()

  if(PROMISE_MEMCHECK_FULL)
    target_compile_definitions(alx-home_promise PUBLIC PROMISE_MEMCHECK_FULL)

    if(PROMISE_BUILD_TESTS)
      target_compile_definitions(promise_test PUBLIC PROMISE_MEMCHECK_FULL)
    endif()
  endif(PROMISE_MEMCHECK_FULL)
endif(PROMISE_MEMCHECK)