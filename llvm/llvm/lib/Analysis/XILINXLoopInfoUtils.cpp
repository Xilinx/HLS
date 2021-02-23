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

#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/XILINXLoopInfoUtils.h"

using namespace llvm;

bool llvm::isForLoop(Loop *L) {
  BasicBlock *Latch = L->getLoopLatch();
  return Latch && !L->isLoopExiting(Latch) && L->isLoopExiting(L->getHeader());
}

// The loop index must be an induction variable with constant step and loop
// invariant upper bound.
bool llvm::getLoopIndexInfo(Loop *L, bool ExitFromHeader,
                            LoopIndexInfoTy &Info) {
  BasicBlock *H = L->getHeader();

  BasicBlock *Incoming = nullptr, *Backedge = nullptr;
  pred_iterator PI = pred_begin(H);
  assert(PI != pred_end(H) && "Loop must have at least one backedge!");
  Backedge = *PI++;
  if (PI == pred_end(H))
    return false; // dead loop
  Incoming = *PI++;
  if (PI != pred_end(H))
    return false; // multiple backedges?

  if (L->contains(Incoming)) {
    if (L->contains(Backedge))
      return false;
    std::swap(Incoming, Backedge);
  } else if (!L->contains(Backedge))
    return false;

  BasicBlock *E = ExitFromHeader ? H : L->getLoopLatch();
  if (!E || !L->isLoopExiting(E))
    return false;

  BranchInst *BR = dyn_cast<BranchInst>(E->getTerminator());
  if (!BR || !BR->isConditional())
    return false;

  ICmpInst *Cmp = dyn_cast<ICmpInst>(BR->getCondition());
  if (!Cmp)
    return false;

  CmpInst::Predicate Pred = Cmp->getPredicate();

  Value *Idx = Cmp->getOperand(0);
  Value *Upper = Cmp->getOperand(1);

  if (!L->isLoopInvariant(stripIntegerCast(Upper))) {
    std::swap(Idx, Upper);
    Pred = CmpInst::getSwappedPredicate(Pred);
    if (!L->isLoopInvariant(stripIntegerCast(Upper)))
      return false;
  }

  bool IsZExt = isa<ZExtInst>(Idx);
  Idx = stripIntegerCast(Idx);

  if (!L->contains(BR->getSuccessor(0)))
    Pred = CmpInst::getInversePredicate(Pred);

  // Loop over all of the PHI nodes, looking for a canonical indvar.
  for (BasicBlock::iterator I = H->begin(); isa<PHINode>(I); ++I) {
    PHINode *PN = cast<PHINode>(I);
    if (!PN->getType()->isIntegerTy())
      continue;

    Instruction *Inc =
        dyn_cast<Instruction>(PN->getIncomingValueForBlock(Backedge));

    if (!Inc || (Idx != Inc && Idx != PN))
      continue;

    int64_t Step;
    if (Inc->getOpcode() == Instruction::Add) {
      Value *Op1 = stripIntegerCast(Inc->getOperand(0));
      Value *Op2 = stripIntegerCast(Inc->getOperand(1));
      if (Op2 == PN)
        std::swap(Op1, Op2);
      if (Op1 != PN || !isa<ConstantInt>(Op2))
        continue;
      Step = cast<ConstantInt>(Op2)->getSExtValue();
    } else if (Inc->getOpcode() == Instruction::Sub) {
      Value *Op1 = stripIntegerCast(Inc->getOperand(0));
      Value *Op2 = stripIntegerCast(Inc->getOperand(1));
      if (Op1 != PN || !isa<ConstantInt>(Op2))
        continue;
      Step = -cast<ConstantInt>(Op2)->getSExtValue();
    } else {
      continue;
    }

    if (Step == 0)
      continue;

    switch (Pred) {
    case CmpInst::ICMP_ULT:
    case CmpInst::ICMP_ULE:
    case CmpInst::ICMP_SLT:
    case CmpInst::ICMP_SLE:
      if (Step < 0)
        continue;
      break;
    case CmpInst::ICMP_UGT:
    case CmpInst::ICMP_UGE:
    case CmpInst::ICMP_SGT:
    case CmpInst::ICMP_SGE:
      if (Step > 0)
        continue;
      break;
    case CmpInst::ICMP_NE:
      if (Step != 1 && Step != -1)
        continue;
      break;
    default:
      continue;
    }

    Info.PN = PN;
    Info.Init = PN->getIncomingValueForBlock(Incoming);
    Info.Upper = Upper;
    Info.Pred = Pred;
    Info.Step = Step;
    Info.ExitIdx = Idx;
    Info.IdxZExt = IsZExt;
    return true;
  }
  return false;
}

MDNode *llvm::getLoopMetadata(const Loop *L, StringRef Attr) {
  MDNode *LoopID = L->getLoopID();
  if (!LoopID)
    return nullptr;

  assert(LoopID->getNumOperands() > 0 && "requires at least one operand");
  assert(LoopID->getOperand(0) == LoopID && "invalid loop id");

  for (unsigned i = 1, e = LoopID->getNumOperands(); i < e; ++i) {
    MDNode *MD = dyn_cast<MDNode>(LoopID->getOperand(i));
    if (!MD)
      continue;

    MDString *S = dyn_cast<MDString>(MD->getOperand(0));
    if (!S)
      continue;

    if (Attr.equals(S->getString()))
      return MD;
  }
  return nullptr;
}

bool llvm::hasLoopMetadata(const Loop *L, StringRef Attr) {
  return getLoopMetadata(L, Attr) != nullptr;
}

Optional<LoopTripCountMDInfo> llvm::getLoopTripCount(const Loop *L) {
  if (MDNode *MD = getLoopMetadata(L, "llvm.loop.tripcount")) {
    return LoopTripCountMDInfo(
        mdconst::extract<ConstantInt>(MD->getOperand(1))->getZExtValue(),
        mdconst::extract<ConstantInt>(MD->getOperand(2))->getZExtValue(),
        mdconst::extract<ConstantInt>(MD->getOperand(3))->getZExtValue());
  }
  return None;
}

bool llvm::isDataFlow(const Loop *L) {
  return hasLoopMetadata(L, "llvm.loop.dataflow.enable");
}

bool llvm::isParallel(const Loop *L) {
  return hasLoopMetadata(L, "reflow.parallel.loop");
}

bool llvm::isPipeline(const Loop *L) {
  if (MDNode *MD = getLoopMetadata(L, "llvm.loop.pipeline.enable"))
    return mdconst::extract<ConstantInt>(MD->getOperand(1))->getZExtValue();
  return false;
}

bool llvm::isPipelineRewind(const Loop *L) {
  if (MDNode *MD = getLoopMetadata(L, "llvm.loop.pipeline.enable"))
    return mdconst::extract<ConstantInt>(MD->getOperand(1))->getZExtValue() &&
           mdconst::extract<ConstantInt>(MD->getOperand(2))->getZExtValue();
  return false;
}

bool llvm::isPipelineOff(const Loop *L) {
  if (MDNode *MD = getLoopMetadata(L, "llvm.loop.pipeline.enable"))
    return mdconst::extract<ConstantInt>(MD->getOperand(1))->getZExtValue() ==
           0;
  return false;
}

/// II = -1 : default "II" value
/// II = 0  : force no pipeline. Query with isPipelineOff instead.
/// II > 0  : customized "II" value
/// \returns II when it's not zero.
Optional<ConstantInt *> llvm::getPipelineII(const Loop *L) {
  if (MDNode *LMD = getLoopMetadata(L, "llvm.loop.pipeline.enable")) {
    assert((LMD->getNumOperands() == 4) &&
           "Expect 4 operands in Loop Pipeline hint!");
    ConstantInt *II = mdconst::extract<ConstantInt>(LMD->getOperand(1));
    if (II->getZExtValue() == 0)
      return None;
    return II;
  }

  return None;
}

int64_t llvm::getPipelineIIInt64(const Loop *L) {
  Optional<ConstantInt *> II = getPipelineII(L);
  if (!II.hasValue())
    return 0;
  return II.getValue()->getSExtValue();
}

Optional<PipelineStyle> llvm::getPipelineStyle(const Loop *L) {
  if (MDNode *LMD = getLoopMetadata(L, "llvm.loop.pipeline.enable")) {
    assert((LMD->getNumOperands() == 4) &&
           "Expect 4 operands in Loop Pipeline hint!");
    ConstantInt *Style = mdconst::extract<ConstantInt>(LMD->getOperand(3));
    int64_t StyleCode = Style->getSExtValue();
    assert((StyleCode >= -1) && (StyleCode <= 2) &&
           "unexpected pipeline style!");
    return static_cast<PipelineStyle>(StyleCode);
  }

  return None;
}

bool llvm::isFlatten(const Loop *L) {
  if (MDNode *MD = getLoopMetadata(L, "llvm.loop.flatten.enable"))
    return mdconst::extract<ConstantInt>(MD->getOperand(1))->getZExtValue();
  return false;
}

bool llvm::isFlattenOff(const Loop *L) {
  if (MDNode *MD = getLoopMetadata(L, "llvm.loop.flatten.enable"))
    return mdconst::extract<ConstantInt>(MD->getOperand(1))->getZExtValue() ==
           0;
  return false;
}

bool llvm::hasUnrollEnableMetadata(const Loop *L) {
  return hasLoopMetadata(L, "llvm.loop.unroll.count") ||
         hasLoopMetadata(L, "llvm.loop.unroll.withoutcheck") ||
         hasLoopMetadata(L, "llvm.loop.unroll.full");
}

bool llvm::isFullyUnroll(const Loop *L) {
  return hasLoopMetadata(L, "llvm.loop.unroll.full");
}

bool llvm::isWithoutExitCheckUnroll(const Loop *L) {
  return hasLoopMetadata(L, "llvm.loop.unroll.withoutcheck");
}

bool llvm::isUnrollOff(const Loop *L) {
  return hasLoopMetadata(L, "llvm.loop.unroll.disable");
}

Optional<ConstantInt *> llvm::getUnrollFactor(const Loop *L) {
  if (MDNode *LMD = getLoopMetadata(L, "llvm.loop.unroll.count")) {
    assert((LMD->getNumOperands() == 2) &&
           "Expect 2 operands in Loop Unroll hint!");
    return mdconst::extract<ConstantInt>(LMD->getOperand(1));
  }

  if (MDNode *LMD = getLoopMetadata(L, "llvm.loop.unroll.withoutcheck")) {
    assert((LMD->getNumOperands() == 2) &&
           "Expect 2 operands in Loop Unroll hint!");
    return mdconst::extract<ConstantInt>(LMD->getOperand(1));
  }

  return None;
}

uint64_t llvm::getUnrollFactorUInt64(const Loop *L) {
  Optional<ConstantInt *> Factor = getUnrollFactor(L);
  if (!Factor.hasValue())
    return 0;
  return Factor.getValue()->getZExtValue();
}

bool llvm::mayFullyUnroll(const Loop *L, const SCEV *LTC) {
  if (isFullyUnroll(L))
    return true;

  if (isUnrollOff(L))
    return false;

  // Get constant loop trip count.
  auto LTCC = dyn_cast<SCEVConstant>(LTC);
  if (!LTCC)
    return false;

  Optional<ConstantInt *> Factor = getUnrollFactor(L);
  return (Factor.hasValue() && (Factor.getValue()->getSExtValue() ==
                                LTCC->getValue()->getSExtValue()));
}

bool llvm::mayExposeInDataFlowRegion(ScalarEvolution &SE, const Loop *L) {
  if (isDataFlow(L))
    return true;

  // Top-level non dataflow loop(which contains dataflow pragma) in non dataflow
  // function will not be exposed into dataflow region.
  // NOTE: This doesn't consider inlining.
  bool IsDataFlowFunction =
      L->getHeader()->getParent()->hasFnAttribute("fpga.dataflow.func");
  if (!IsDataFlowFunction && (L->getParentLoop() == nullptr))
    return false;

  // When there is at least one parent loop that will not disapear and is not
  // constructing a dataflow region, the Loop L is not exposed in the dataflow
  // region.
  for (Loop *PL = L->getParentLoop(); PL != nullptr; PL = PL->getParentLoop()) {
    PredicatedScalarEvolution PSEPL(SE, *PL);
    const SCEV *PBTC = PSEPL.getBackedgeTakenCount();
    if (isa<SCEVCouldNotCompute>(PBTC))
      return false;

    // Compute the PL's loop trip count
    const SCEV *PLTC = SE.getAddExpr(PBTC, SE.getOne(PBTC->getType()));
    if (!isDataFlow(PL) && !(PLTC->isOne()) && !mayFullyUnroll(PL, PLTC))
      return false;
  }
  return true;
}

// Find the "llvm.loop.name" MDNode to get the loop name
// We store loop name in !llvm.loop. Here the loop name means the label the user
// specified in the source C/C++ or OpenCL program. Like below. loop_name: for
// (int i = 0; ...) Note: The name is different from LLVM getName() for loop.
Optional<const std::string> llvm::getLoopName(const Loop *L) {
  MDNode *LoopID = L->getLoopID();
  // Return none if LoopID is false.
  if (!LoopID)
    return None;

  // First operand should refer to the loop id itself.
  assert(LoopID->getNumOperands() > 0 && "requires at least one operand");
  assert(LoopID->getOperand(0) == LoopID && "invalid loop id");

  // Iterate over LoopID operands and look for MDString Metadata
  for (const MDOperand &Op : LoopID->operands()) {
    MDNode *MD = dyn_cast<MDNode>(Op);
    if (!MD)
      continue;
    auto *S = dyn_cast<MDString>(MD->getOperand(0));
    if (!S)
      continue;
    StringRef MDLoopName = "llvm.loop.name";
    if (MDLoopName.equals(S->getString()))
      switch (MD->getNumOperands()) {
      case 1:
        return {};
      case 2:
        if (auto *Name = dyn_cast<MDString>(MD->getOperand(1)))
          return (Name->getString()).str();
        return {};
      default:
        llvm_unreachable("loop metadata has 0 or 1 operand");
      }
  }
  return None;
}
