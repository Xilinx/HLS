//===- llvm/Analysis/DivergenceAnalysis.h - Divergence Analysis -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// And has the following additional copyright:
//
// (C) Copyright 2016-2020 Xilinx, Inc.
// All Rights Reserved.
//
//===----------------------------------------------------------------------===//
//
// The divergence analysis is an LLVM pass which can be used to find out
// if a branch instruction in a GPU program is divergent or not. It can help
// branch optimizations such as jump threading and loop unswitching to make
// better decisions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DIVERGENCEANALYSIS_H
#define LLVM_ANALYSIS_DIVERGENCEANALYSIS_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"

namespace llvm {
class Value;
class BasicBlock;
class DivergenceAnalysis : public FunctionPass {
public:
  static char ID;

  DivergenceAnalysis() : FunctionPass(ID) {
    initializeDivergenceAnalysisPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool runOnFunction(Function &F) override;

  Function *getCurrentFunction() const { return CurrentFunction; }

  // Print all divergent branches in the function.
  void print(raw_ostream &OS, const Module *) const override;

  // Returns true if V is divergent.
  bool isDivergent(const Value *V) const { return DivergentValues.count(V); }

  // Returns true if V is uniform/non-divergent.
  bool isUniform(const Value *V) const { return !isDivergent(V); }

  // Returns true if BB is control-divergent.
  bool isControlDivergent(const BasicBlock *BB) const {
    return DivergentBasicBlocks.count(BB);
  }

  // Returns true if BB is control-uniform/non-divergent.
  bool isControlUniform(const BasicBlock *BB) const {
    return !isControlDivergent(BB);
  }

  // Update the set of divergent values
  void addDivergentValue(const Value *V) { DivergentValues.insert(V); }

  // Update the set of divergent basic blocks
  void addDivergentBasicBlock(const BasicBlock *BB) {
    DivergentBasicBlocks.insert(BB);
  }

private:
  // Remember the Function
  Function *CurrentFunction;
  // Stores all divergent values and basic blocks.
  DenseSet<const Value *> DivergentValues;
  DenseSet<const BasicBlock *> DivergentBasicBlocks;
};
} // End llvm namespace

#endif
