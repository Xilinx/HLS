// (C) Copyright 2016-2020 Xilinx, Inc.
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
// The file declares some comman functions for testing XILINX HLS APIs.
//
//===----------------------------------------------------------------------===//

#include "TestUtils.h"

using namespace llvm;

std::unique_ptr<Module> llvm::makeLLVMModule(LLVMContext &Context,
                                              const char *ModuleStr) {
  SMDiagnostic Err;
  return parseAssemblyString(ModuleStr, Err, Context);
}

Function *llvm::getFunctionFrom(Module &M, StringRef FuncName) {
  auto *F = M.getFunction(FuncName);
  assert(F && "Could not find function!");
  return F;
}
