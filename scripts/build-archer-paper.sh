#!/bin/bash

# Part of the Data Race Performance Benchmark, under the Apache License v2.0.
# See LICENSE for license information.
# SPDX-License-Identifier: Apache-2.0

set +e
set -x

WDIR=${WDIR:-$HOME/llvm/}
BUILDDIR=$WDIR/build
INSTALLDIR=${2:-$WDIR/install-$1}
SOURCEDIR=$WDIR/llvm-project
LLVM_SOURCE=$SOURCEDIR/llvm
PROJECTS="clang;compiler-rt;libcxxabi;libcxx;libunwind;clang-tools-extra"
RUNTIMES="openmp"
case "$1" in
  *noreads) BRANCH="tsan-noreads" ;;
  *sampling) BRANCH="tsan-sampling" ;;
  master) BRANCH="main" ;;
  main) BRANCH="main" ;;
  base) BRANCH="5a645b7313aac877254870c0b3464c6f89553dbe" ;;
  release) BRANCH="release/12.x" ;;
  *) echo "Usage: $0 [*noreads|*sampling|base|master|main|release]"; exit -1 ;;
esac

module purge
module load DEVELOP clang/11 cuda/11.2 ninja-build cmake/3.16.4 ccache

mkdir -p $BUILDDIR
if [ ! -e $SOURCEDIR/.git ]; then
  git clone https://github.com/RWTH-HPC/llvm-project $SOURCEDIR
fi

cd $SOURCEDIR
if [ "$CHECKOUT" != "no" ]; then
  git checkout $BRANCH
fi

export CXXFLAGS="-Wno-unused-command-line-argument $CXXFLAGS" 
export CFLAGS="-Wno-unused-command-line-argument $CFLAGS" 

cd $BUILDDIR
cmake -GNinja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=$INSTALLDIR \
  -DLLVM_ENABLE_LIBCXX=ON \
  -DLLVM_LIT_ARGS="-sv -j12" \
  -DPAPI_PREFIX=${PAPI_ROOT} \
  -DCLANG_DEFAULT_CXX_STDLIB=libc++ \
  -DCLANG_OPENMP_NVPTX_DEFAULT_ARCH=sm_70 \
  -DLIBOMPTARGET_ENABLE_DEBUG=on \
  -DLIBOMPTARGET_NVPTX_ENABLE_BCLIB=true \
  -DLIBOMPTARGET_NVPTX_AUTODETECT_COMPUTE_CAPABILITY=OFF \
  -DLIBOMPTARGET_NVPTX_COMPUTE_CAPABILITIES="35;60;70" \
  -DLLVM_ENABLE_PROJECTS=$PROJECTS \
  -DLLVM_ENABLE_RUNTIMES=$RUNTIMES \
  -DLLVM_INSTALL_UTILS=ON \
  -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
  $LLVM_SOURCE
time nice -n 10 ninja -j$(nproc --all) -l$(nproc --all) > >(tee $WDIR/build-archer.log) 2> >(tee $WDIR/build-archer.err >&2) || exit -1
time nice -n 10 ninja -j$(nproc --all) -l$(nproc --all) install
