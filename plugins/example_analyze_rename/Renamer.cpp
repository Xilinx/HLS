// (C) Copyright 2016-2021 Xilinx, Inc.
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
//

//===----------------------------------------------------------------------===//
//
// Rename all the function arguments.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Function.h"
#include "llvm/Pass.h"

using namespace llvm;

struct Renamer : public FunctionPass {
  static char ID;
  Renamer() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};

char Renamer::ID = 0;
static RegisterPass<Renamer> X("renamer", "Rename Function arguments",
                               false /* Only looks at CFG */,
                               true /* Transformation Pass */);

bool Renamer::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  for (auto &A : F.args()) {
    std::string Name = A.getName();
    A.setName(Name + "boba");
  }

  return true;
}
