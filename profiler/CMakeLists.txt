#
# ompt-profiler.cpp -- OMPT based profiling tool
#
#
# //===----------------------------------------------------------------------===//
# //
# // Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# // See tools/archer/LICENSE.txt for details.
# // SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# //
# //===----------------------------------------------------------------------===//
  
project(ompt_profiler)  
cmake_minimum_required(VERSION 3.12)

include_directories(${LIBOMP_INCLUDE_DIR})

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})
find_package(PAPI)

find_package(Threads)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXE_LINKER_FLAGS "-Xlinker -v -v")

#add_compile_options(-fsanitize=address)

include_directories(../multiplex/)

set(OMPT_PAPI_SRC)
if(PAPI_FOUND)
  set(OMPT_PAPI_SRC ompt-papi.cpp)
endif()

if (NOT "${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_SOURCE_DIR}")
  set(SANITIZER_SYMBOLIZER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../compiler-rt/lib/sanitizer_common/)
endif ()

if (NOT DEFINED SANITIZER_SYMBOLIZER_SOURCE_DIR)
  message(FATAL_ERROR "SANITIZER_SYMBOLIZER_SOURCE_DIR is undefined but needed to point to a sanitizer_common directory.")
endif ()
if (NOT EXISTS ${SANITIZER_SYMBOLIZER_SOURCE_DIR})
  message(FATAL_ERROR "SANITIZER_SYMBOLIZER_SOURCE_DIR = ${SANITIZER_SYMBOLIZER_SOURCE_DIR} cannot be found.")
endif ()
if (NOT EXISTS ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_libc.cpp)
  message(FATAL_ERROR "SANITIZER_SYMBOLIZER_SOURCE_DIR = ${SANITIZER_SYMBOLIZER_SOURCE_DIR} doesn't seem to be a sanitizer_common directory.")
endif ()

include_directories(${SANITIZER_SYMBOLIZER_SOURCE_DIR}/..)
include_directories(${SANITIZER_SYMBOLIZER_SOURCE_DIR})


set(SANITIZER_SYMBOLIZER_SOURCES_PROFILER
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_libc.cpp
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_allocator.cpp
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_termination.cpp
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_common.cpp
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_common_libcdep.cpp
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_file.cpp
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_symbolizer_posix_libcdep.cpp
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_posix_libcdep.cpp
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_linux.cpp
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_printf.cpp
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_posix.cpp
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_flags.cpp
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_flag_parser.cpp
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_procmaps_common.cpp
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_procmaps_linux.cpp 
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_linux_libcdep.cpp
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_symbolizer_libbacktrace.cpp  
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_platform_limits_posix.cpp
  
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_symbolizer.cpp
  ${SANITIZER_SYMBOLIZER_SOURCE_DIR}/sanitizer_symbolizer_libcdep.cpp
  
  )

add_library(ompt_profiler SHARED ompt-profiler.cpp ${OMPT_PAPI_SRC} ${SANITIZER_SYMBOLIZER_SOURCES_PROFILER})


if(PAPI_FOUND)
  include_directories(${PAPI_INCLUDE_DIRS})
  target_link_libraries(ompt_profiler ${PAPI_LIBRARIES} ${CMAKE_DL_LIBS})
  target_compile_definitions(ompt_profiler PUBLIC USE_PAPI)
endif()

target_link_libraries(ompt_profiler ${CMAKE_THREAD_LIBS_INIT})

install(TARGETS ompt_profiler
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib)

install(FILES ompt-profiler-user.h DESTINATION include)
#add_subdirectory(tests)
