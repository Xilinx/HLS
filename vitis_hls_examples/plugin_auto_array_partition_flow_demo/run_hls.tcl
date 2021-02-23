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

if { ![info exists ::env(HLS_LLVM_PLUGIN_DIR)] } {
  # Use plugin example directory as default build directory
  set ::env(HLS_LLVM_PLUGIN_DIR) [file normalize ../../plugins/auto_array_partition]
}
if { ![file exists $::env(HLS_LLVM_PLUGIN_DIR)/LLVMMemoryPartition.so] } {
  error "Must build LLVMMemoryPartition.so before running this example"
}
### The following variable must be set before csynth_design
# Do not use global namespace (::) for variables used in LVM_CUSTOM_CMD
set ::LLVM_CUSTOM_CMD {$LLVM_CUSTOM_OPT -always-inline -mem2reg -gvn -reflow-simplify-type -instcombine -mem2reg -gvn -indvars  -load $::env(HLS_LLVM_PLUGIN_DIR)/LLVMMemoryPartition.so -auto-memory-partition $LLVM_CUSTOM_INPUT -o $LLVM_CUSTOM_OUTPUT}

# Open a project and remove any existing data
open_project -reset proj

# Add kernel
add_files hls_example.cpp

# Tell the top
set_top top

# Open a solution and remove any existing data
open_solution -reset solution1

# Set the target device
set_part "virtex7"

# Create a virtual clock for the current solution
create_clock -period "300MHz"

# Synthesize to RTL
csynth_design
