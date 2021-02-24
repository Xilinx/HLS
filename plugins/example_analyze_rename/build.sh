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
source `dirname -- $0`/../setup-vitis-hls-llvm.sh

clang_path=$XILINX_HLS/lnx64/tools/clang-3.9-csynth
echo "Using Vitis HLS clang/opt: $clang_path/bin"

[ -n "$PATH" ] && PATH=:$PATH
export PATH=$clang_path/bin$PATH

[ -n "$LD_LIBRARY_PATH" ] && LD_LIBRARY_PATH=:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$clang_path/lib$LD_LIBRARY_PATH

[ -n "$LIBRARY_PATH" ] && LIBRARY_PATH=:$LIBRARY_PATH
export LIBRARY_PATH=$clang_path/lib$LIBRARY_PATH

[ -n "$CPATH" ] && CPATH=:$CPATH
export CPATH=$clang_path/include$CPATH

# Build against release build
make all-debug-off

# Build against debug build
#make all
