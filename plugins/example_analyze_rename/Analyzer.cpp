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
// Analyze pipelined loops memory accesses patterns using Scalar Evolution.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/XILINXLoopInfoUtils.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

struct Analyzer : public FunctionPass {
  static char ID;
  Analyzer() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.setPreservesAll();
  }
};

char Analyzer::ID = 0;
static RegisterPass<Analyzer> X("analyzer", "Analyze access patterns",
                                false /* Only looks at CFG */,
                                false /* Transformation Pass */);

bool Analyzer::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  auto &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  auto &SE = getAnalysis<ScalarEvolutionWrapperPass>().getSE();

  // Iterate over top level loops
  for (auto L : LI) {
    if (isPipeline(L)) {

      // Print the predicted loop trip count
      L->print(dbgs());
      dbgs() << "\tBackedge Taken Count: " << *SE.getBackedgeTakenCount(L)
             << "\n";

      if (auto II = getPipelineIIInt64(L))
        dbgs() << "II:" << II << '\n';

      for (auto *BB : L->blocks())
        for (auto &I : *BB) {
          if (auto *Ld = dyn_cast<LoadInst>(&I)) {
            // Print the load access pattern
            Ld->print(dbgs());
            auto S = SE.getSCEV(Ld->getPointerOperand());
            dbgs() << "\n\tAccess Pattern: ";
#ifndef NDEBUG
            S->dump();
#else
           dbgs() << *S;
#endif
          }

          if (auto *St = dyn_cast<StoreInst>(&I)) {
            // Print the store access pattern
            St->print(dbgs());
            auto S = SE.getSCEV(St->getPointerOperand());
            dbgs() << "\n\tAccess Pattern: ";
#ifndef NDEBUG
            S->dump();
#else
           dbgs() << *S;
#endif
          }
        }
    }
  }

  // We didn't modify the IR (analysis only)
  return false;
}
