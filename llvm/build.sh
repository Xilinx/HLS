# (C) Copyright 2016-2021 Xilinx, Inc.
# All Rights Reserved.
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

#!/usr/bin/env bash

# Get absolute paths to output and src directories
outroot=`pwd`
cd `dirname -- $0`
cd ..
srcroot=`pwd`

builddir=$outroot/hls-build
llvmdir=$srcroot/llvm

# create build output directory
mkdir -p $builddir
cd $builddir

# build llvm, clang & clang-tools-extra
INSTALL_PREFIX="hls-install"
LLVM_SRC_DIR="$llvmdir/llvm"
CLANG_SRC_DIR="$llvmdir/clang"
EXTRA_SRC_DIR="$llvmdir/clang-tools-extra"
#NOTE: CMake 3.4.3 or higher is required
#NOTE: Can replace with 'ninja' if it's install with "cmake -G Ninja"
#NOTE: -DLLVM_ENABLE_DOXYGEN and -DLLVM_BUILD_DOCS are for doxygen documentation.
cmake "$LLVM_SRC_DIR" \
	-DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX \
	-DLLVM_EXTERNAL_CLANG_SOURCE_DIR=$CLANG_SRC_DIR \
	-DLLVM_EXTERNAL_CLANG_TOOLS_EXTRA_SOURCE_DIR=$EXTRA_SRC_DIR \
        -DLLVM_ENABLE_DOXYGEN=ON \
        -DLLVM_BUILD_DOCS=ON \
        -DCMAKE_BUILD_TYPE="Debug"

#NOTE: Use 'ninja' if you have it!:-)
make -j $(nproc)

#Build documentation
#make doxygen
