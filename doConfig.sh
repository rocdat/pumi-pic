#!/bin/bash
[ $# -ne 2 ] && echo "Usage: $0 <path to source> <path to particle structures
install >" && return 1
src=$1
ps=$2

export CMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH:$ps

cmake -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON \
      -DCUDA_NVCC_FLAGS:STRING="-G -Xcompiler -rdynamic" \
      -DCMAKE_CXX_COMPILER=mpicxx \
      -DCMAKE_BUILD_TYPE=Debug \
      -DIS_TESTING=ON \
      -DTEST_DATA_DIR=$src/pumipic-data \
      $src

#      -DCMAKE_BUILD_TYPE=DEBUG \
