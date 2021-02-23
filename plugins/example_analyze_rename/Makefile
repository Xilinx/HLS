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

LLVM_CONFIG=llvm-config
GCC_TOOL_CHAIN_ROOT=$(XILINX_VIVADO)/tps/lnx64/gcc-6.2.0

CXX:=`$(LLVM_CONFIG) --bindir`/clang
CXXFLAGS:=`$(LLVM_CONFIG) --cppflags` --gcc-toolchain=$(GCC_TOOL_CHAIN_ROOT) -fPIC -fvisibility-inlines-hidden -Werror=date-time -Werror=unguarded-availability-new -std=c++11 -Wall -Wcast-qual -Wmissing-field-initializers -pedantic -Wno-long-long -Wcovered-switch-default -Wnon-virtual-dtor -Wdelete-non-virtual-dtor -Wstring-conversion -fcolor-diagnostics -ffunction-sections -fdata-sections -O3 -fno-exceptions -fno-rtti -D_GNU_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS
LDFLAGS:=`$(LLVM_CONFIG) --ldflags`

all: CXXFLAGS := $(CXXFLAGS) -D_DEBUG

all: LLVMCustomPasses.so

all-debug-off: CXXFLAGS := $(CXXFLAGS) -DNDEBUG

all-debug-off: LLVMCustomPasses.so

LLVMCustomPasses.so: Renamer.o Analyzer.o
	$(CXX) -shared $^ -o $@ -fPIC $(CXXFLAGS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) -c $^ -o $@ $(CXXFLAGS)

clean:
	rm -f *.o *.so

