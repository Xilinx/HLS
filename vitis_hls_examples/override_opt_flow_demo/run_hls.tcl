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

# Open a project and remove any existing data
open_project -reset proj

# Add kernel and testbench
add_files hls_example.cpp
add_files -tb hls_example.cpp

# Tell the top
set_top example

# Open a solution and remove any existing data
open_solution -reset solution1

# Set the target device
set_part "virtex7"

# Create a virtual clock for the current solution
create_clock -period "300MHz"

### The following variables must be set before csynth_design

# TODO: Change this location to your local opt build
set ::LLVM_CUSTOM_OPT [pwd]/../../llvm/hls-build/bin/opt

# This example assumes that the local opt binary has some customized behavior (no custom passes called here)
# Do not use global namespace (::) for variables used in LVM_CUSTOM_CMD
set ::LLVM_CUSTOM_CMD {$LLVM_CUSTOM_OPT -mem2reg -dce $LLVM_CUSTOM_INPUT -o $LLVM_CUSTOM_OUTPUT}

# Compile and runs pre-synthesis C simulation using the provided C test bench
csim_design

# Synthesize to RTL
csynth_design

# Execute post-synthesis co-simulation of the synthesized RTL with the original C/C++-based test bench
cosim_design
