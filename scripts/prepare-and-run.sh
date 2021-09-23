#!/bin/bash

# Part of the Data Race Performance Benchmark, under the Apache License v2.0.
# See LICENSE for license information.
# SPDX-License-Identifier: Apache-2.0

export SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
export BASE_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )"/.. &> /dev/null && pwd )"

bash $SCRIPT_DIR/build-spec-drd.sh

WDIR=$BASE_DIR/llvm bash $SCRIPT_DIR/build-archer-paper.sh base
WDIR=$BASE_DIR/llvm bash $SCRIPT_DIR/build-archer-paper.sh noreads
WDIR=$BASE_DIR/llvm bash $SCRIPT_DIR/build-archer-paper.sh sampling

bash $SCRIPT_DIR/build-profiler.sh

module load DEV-TOOLS jube

export JUBE_RES_DIR=$BASE_DIR/jube-out

cd $BASE_DIR/jube
jube run --tag profile papi_branches flat_profile -- spec_master_jube.xml
