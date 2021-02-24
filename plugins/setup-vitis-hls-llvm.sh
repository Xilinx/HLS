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

if [ -z "$XILINX_HLS" ]; then
  echo "ERROR: Must set XILINX_HLS or source Vitis_HLS settings64.sh file to set it"
  exit 1
fi

source ${XILINX_HLS}/settings64.sh

[ -n "$PATH" ] && PATH=:$PATH
VITIS_CMAKE="$XILINX_VITIS/tps/lnx64/cmake-3.3.2/bin:"; PATH=$(echo "$PATH" | sed -e "s|$VITIS_CMAKE||g")
export PATH=$XILINX_HLS/tps/lnx64/binutils-2.26/bin:$XILINX_HLS/tps/lnx64/gcc-6.2.0/bin$PATH

[ -n "$LD_LIBRARY_PATH" ] && LD_LIBRARY_PATH=:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$XILINX_HLS/tps/lnx64/binutils-2.26/bin:$XILINX_HLS/tps/lnx64/gcc-6.2.0/bin$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$XILINX_HLS/tps/lnx64/binutils-2.26/lib:$XILINX_HLS/tps/lnx64/gcc-6.2.0/lib:$XILINX_HLS/tps/lnx64/gcc-6.2.0/lib64:$LD_LIBRARY_PATH

[ -n "$LIBRARY_PATH" ] && LIBRARY_PATH=:$LIBRARY_PATH
export LIBRARY_PATH=$XILINX_HLS/tps/lnx64/binutils-2.26/lib:$XILINX_HLS/tps/lnx64/gcc-6.2.0/lib:$XILINX_HLS/tps/lnx64/gcc-6.2.0/lib64$LIBRARY_PATH

[ -n "$CPATH" ] && CPATH=:$CPATH
export CPATH=$XILINX_HLS/tps/lnx64/binutils-2.26/include:$XILINX_HLS/tps/lnx64/gcc-6.2.0/include$CPATH

[ -n "$MANPATH" ] && MANPATH=:$MANPATH
export MANPATH=$XILINX_HLS/tps/lnx64/binutils-2.26/share/man:$XILINX_HLS/tps/lnx64/gcc-6.2.0/share/man$MANPATH
