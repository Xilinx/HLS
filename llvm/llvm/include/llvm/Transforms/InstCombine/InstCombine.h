//===- InstCombine.h - InstCombine pass -------------------------*- C++ -*-===//
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
/// \file
///
/// This file provides the primary interface to the instcombine pass. This pass
/// is suitable for use in the new pass manager. For a pass that works with the
/// legacy pass manager, please look for \c createInstructionCombiningPass() in
/// Scalar.h.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTCOMBINE_INSTCOMBINE_H
#define LLVM_TRANSFORMS_INSTCOMBINE_INSTCOMBINE_H

#include "llvm/Analysis/TargetFolder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/InstCombine/InstCombineWorklist.h"

namespace llvm {

/// Custom combine callback:
///   return a None optional to use the existing inst combine;
///   return a non-none optional to suppress the existing inst combine
typedef std::function<Optional<Value *>(
    Instruction &, IRBuilder<TargetFolder, IRBuilderCallbackInserter> &,
    const DataLayout &)>
    CustomCombineCallback;

class InstCombinePass : public PassInfoMixin<InstCombinePass> {
  InstCombineWorklist Worklist;
  bool ExpensiveCombines;
  CustomCombineCallback CustomCombine;

public:
  static StringRef name() { return "InstCombinePass"; }

  explicit InstCombinePass(bool ExpensiveCombines = true)
      : ExpensiveCombines(ExpensiveCombines) {}
  InstCombinePass(CustomCombineCallback CustomCombine,
                  bool ExpensiveCombines = true)
      : ExpensiveCombines(ExpensiveCombines),
        CustomCombine(std::move(CustomCombine)) {}
  InstCombinePass(InstCombinePass &&Arg)
      : Worklist(std::move(Arg.Worklist)),
        ExpensiveCombines(Arg.ExpensiveCombines),
        CustomCombine(std::move(Arg.CustomCombine)) {}
  InstCombinePass &operator=(InstCombinePass &&RHS) {
    Worklist = std::move(RHS.Worklist);
    ExpensiveCombines = RHS.ExpensiveCombines;
    CustomCombine = std::move(RHS.CustomCombine);
    return *this;
  }

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// \brief The legacy pass manager's instcombine pass.
///
/// This is a basic whole-function wrapper around the instcombine utility. It
/// will try to combine all instructions in the function.
class InstructionCombiningPass : public FunctionPass {
  InstCombineWorklist Worklist;
  const bool ExpensiveCombines;
  CustomCombineCallback CustomCombine;

public:
  static char ID; // Pass identification, replacement for typeid

  InstructionCombiningPass(bool ExpensiveCombines = true)
      : FunctionPass(ID), ExpensiveCombines(ExpensiveCombines) {
    initializeInstructionCombiningPassPass(*PassRegistry::getPassRegistry());
  }

  InstructionCombiningPass(CustomCombineCallback CustomCombine,
                           bool ExpensiveCombines = true)
      : FunctionPass(ID), ExpensiveCombines(ExpensiveCombines),
        CustomCombine(std::move(CustomCombine)) {
    initializeInstructionCombiningPassPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnFunction(Function &F) override;
};
}

#endif
