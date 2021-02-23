// (c) Copyright 2016-2020 Xilinx, Inc.
// All Rights Reserved.
//
// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//===----------------------------------------------------------------------===//
//
// This file define some handy debug helper functions that you can use in
// gdb or lldb.
//
// To add a new helper:
//  - Declare it in 'llvm/include/llvm/DebugHelpers.h'
//  - Implement it in 'llvm/lib/DebugHelpers.cpp' (here)
//  - Call it with dummy arguments in 'llvm/include/llvm/LinkAllDebugHelpers.h'
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugHelpers.h"
#include "llvm/IR/Module.h"

#ifndef NDEBUG

void dumpFuncs(llvm::Module *M) {
  for (auto &G : M->globals())
    G.dump();

  for (auto &F : M->functions())
    F.dump();
}

#endif
