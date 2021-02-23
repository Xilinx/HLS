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

#ifndef LLVM_UNITTESTS_XILINXHLSIR_TESTUTILS_H
#define LLVM_UNITTESTS_XILINXHLSIR_TESTUTILS_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/Support/SourceMgr.h"

namespace llvm {

/// Make \p ModuleStr an Module under LLVMContext \p Context.
std::unique_ptr<Module> makeLLVMModule(LLVMContext &Context,
                                       const char *ModuleStr);

/// Get Function with name \p FuncName in Module \p M.
Function *getFunctionFrom(Module &M, StringRef FuncName);

} // end namespace llvm

#endif // LLVM_UNITTESTS_XILINXHLSIR_TESTUTILS_H
