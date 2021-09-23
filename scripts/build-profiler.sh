#!/bin/bash

# Part of the Data Race Performance Benchmark, under the Apache License v2.0.
# See LICENSE for license information.
# SPDX-License-Identifier: Apache-2.0

set +e
set -x

module use $BASE_DIR/modules
module load clang cmake DEV-TOOLS papi

mkdir -p $BASE_DIR/llvm/build-profiler && cd $BASE_DIR/llvm/build-profiler
CC=clang CXX=clang++ cmake ../../profiler/ -DSANITIZER_SYMBOLIZER_SOURCE_DIR=$BASE_DIR/llvm/llvm-project/compiler-rt/lib/sanitizer_common/
make
