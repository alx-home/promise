cmake_minimum_required(VERSION 3.20)

project(alx-home_promise LANGUAGES CXX)

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
  src/Promise.cpp
)
add_library(alx-home::promise ALIAS alx-home_promise)

target_include_directories(alx-home_promise INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(alx-home_promise PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include/promise
)

win32_executable(TARGET_NAME promise_test
    FILES
  src/promise_test.cpp
)
target_link_libraries(promise_test alx-home::promise)
target_include_directories(promise_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include/promise
)

if(PROMISE_MEMCHECK)
  target_compile_definitions(alx-home_promise PUBLIC PROMISE_MEMCHECK)
  target_compile_definitions(promise_test PUBLIC PROMISE_MEMCHECK)

  if(PROMISE_MEMCHECK_FULL)
    target_compile_definitions(alx-home_promise PUBLIC PROMISE_MEMCHECK_FULL)
    target_compile_definitions(promise_test PUBLIC PROMISE_MEMCHECK_FULL)
  endif(PROMISE_MEMCHECK_FULL)
endif(PROMISE_MEMCHECK)