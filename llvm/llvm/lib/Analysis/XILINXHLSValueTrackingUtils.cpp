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

#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/XILINXHLSValueTrackingUtils.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/XILINXFPGAIntrinsicInst.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "xilinx-hls-value-tracking-utils"

using namespace llvm;

// NOTE: From llvm/Transforms/Utils/Local.h
// Keep the building system clean for Analysis/
static void FindDbgValues(SmallVectorImpl<DbgValueInst *> &DbgValues, Value *V) {
  if (auto *L = LocalAsMetadata::getIfExists(V))
    if (auto *MDV = MetadataAsValue::getIfExists(V->getContext(), L))
      for (User *U : MDV->users())
        if (DbgValueInst *DVI = dyn_cast<DbgValueInst>(U))
          DbgValues.push_back(DVI);
}

bool llvm::IsGlobalUseEmpty(const GlobalValue &GV) {
  if (GV.use_empty())
    return true;
  GV.removeDeadConstantUsers();
  return GV.use_empty();
}

bool llvm::IsHLSStream(const Value *V) {
  SmallVector<DbgValueInst *, 4> DVIs;
  if (Argument *A = dyn_cast<Argument>(const_cast<Value *>(V)))
    FindDbgValues(DVIs, A);

  return any_of(DVIs, [](const DbgValueInst *DI) {
    DIType *Ty = DI->getVariable()->getType().resolve();
    auto *DerivedTy = dyn_cast<DIDerivedType>(Ty);
    if (!DerivedTy)
      return false;

    if (DerivedTy->getBaseType()) {
      auto *RefTy = dyn_cast<DIDerivedType>(DerivedTy->getBaseType());
      if (RefTy)
        DerivedTy = RefTy;
    }

    auto tmpTy = DerivedTy->getBaseType();
    if (!tmpTy)
      return false;
    auto *CompositeTy = dyn_cast<DICompositeType>(tmpTy);

    if (!CompositeTy)
      return false;
    // Name of the class must be stream.
    if (!CompositeTy->getName().startswith("stream<"))
      return false;
    // Stream namespace has to be "hls".
    return CompositeTy->getScope().resolve()->getName().equals("hls");
  });
}

Type *llvm::StripPadding(Type *T, const DataLayout &DL) {
  if (auto *ST = dyn_cast<StructType>(T)) {
    if (ST->isOpaque())
      return Type::getInt8Ty(T->getContext());

    if (ST->getNumElements() == 1) {
      auto *EltT = ST->getElementType(0);
      if (DL.getTypeAllocSize(ST) == DL.getTypeAllocSize(EltT))
        return StripPadding(EltT, DL);
    }
  }

  return T;
}

Type *llvm::extractHLSStreamEltType(Type *T) {
  if (auto *ST = dyn_cast<StructType>(T))
    if (ST->hasName() && !ST->isOpaque() &&
        ST->getName().startswith("class.hls::stream"))
      return ST->getStructElementType(0);

  return nullptr;
}

static bool isSameInLoop(const PHINode *PN, const LoopInfo *LI) {
  // Find the loop-defined value.
  Loop *L = LI->getLoopFor(PN->getParent());
  if (PN->getNumIncomingValues() != 2)
    return true;

  // Find the value from previous iteration.
  auto *PrevValue = dyn_cast<Instruction>(PN->getIncomingValue(0));
  if (!PrevValue || LI->getLoopFor(PrevValue->getParent()) != L)
    PrevValue = dyn_cast<Instruction>(PN->getIncomingValue(1));
  if (!PrevValue || LI->getLoopFor(PrevValue->getParent()) != L)
    return true;

  // If a new pointer is loaded in the loop, the pointer references a different
  // object in every iteration.  E.g.:
  //    for (i)
  //       int *p = a[i];
  //       ...
  if (auto *Load = dyn_cast<LoadInst>(PrevValue))
    if (!L->isLoopInvariant(Load->getPointerOperand()))
      return false;
  return true;
}

Value *llvm::GetUnderlyingSSACopyOrUnderlyingObject(Value *V,
                                                    const DataLayout &DL,
                                                    unsigned MaxLookup) {
  assert(V && "Value is nullptr!");
  if (!V->getType()->isPointerTy())
    return V;
  for (unsigned Count = 0; MaxLookup == 0 || Count < MaxLookup; ++Count) {
    if (isa<SSACopyInst>(V)) {
      // Found an ssa_copy.
      return V;
    } else if (auto *GEP = dyn_cast<GEPOperator>(V)) {
      V = GEP->getPointerOperand();
    } else if (Operator::getOpcode(V) == Instruction::BitCast ||
               Operator::getOpcode(V) == Instruction::AddrSpaceCast) {
      V = cast<Operator>(V)->getOperand(0);
    } else if (auto *GA = dyn_cast<GlobalAlias>(V)) {
      if (GA->isInterposable())
        return V;
      V = GA->getAliasee();
    } else if (isa<AllocaInst>(V)) {
      // An alloca can't be further simplified.
      return V;
    } else {
      if (auto CS = CallSite(V))
        if (Value *RV = CS.getReturnedArgOperand()) {
          V = RV;
          continue;
        }

      // See if InstructionSimplify knows any relevant tricks.
      if (auto *I = dyn_cast<Instruction>(V))
        // TODO: Acquire a DominatorTree and AssumptionCache and use them.
        if (Value *Simplified = SimplifyInstruction(I, {DL, I})) {
          V = Simplified;
          continue;
        }

      return V;
    }
    assert(V->getType()->isPointerTy() && "Unexpected operand type!");
  }
  return V;
}

void llvm::GetUnderlyingSSACopiesOrUnderlyingObjects(
    Value *V, SmallVectorImpl<Value *> &Objects, const DataLayout &DL,
    LoopInfo *LI, unsigned MaxLookup) {
  assert(V && "Value is nullptr!");
  SmallPtrSet<Value *, 4> Visited;
  SmallVector<Value *, 4> Worklist;
  Worklist.push_back(V);
  do {
    Value *P = Worklist.pop_back_val();
    P = GetUnderlyingSSACopyOrUnderlyingObject(P, DL, MaxLookup);

    if (!Visited.insert(P).second)
      continue;

    if (SelectInst *SI = dyn_cast<SelectInst>(P)) {
      Worklist.push_back(SI->getTrueValue());
      Worklist.push_back(SI->getFalseValue());
      continue;
    }

    if (PHINode *PN = dyn_cast<PHINode>(P)) {
      // If this PHI changes the underlying object in every iteration of the
      // loop, don't look through it.  Consider:
      //   int **A;
      //   for (i) {
      //     Prev = Curr;     // Prev = PHI (Prev_0, Curr)
      //     Curr = A[i];
      //     *Prev, *Curr;
      //
      // Prev is tracking Curr one iteration behind so they refer to different
      // underlying objects.
      if (!LI || !LI->isLoopHeader(PN->getParent()) || isSameInLoop(PN, LI))
        for (Value *IncValue : PN->incoming_values())
          Worklist.push_back(IncValue);
      continue;
    }

    Objects.push_back(P);
  } while (!Worklist.empty());
}

Value *llvm::GetUniqueSSACopyOrUnderlyingObject(Value *V, const DataLayout &DL,
                                                LoopInfo *LI,
                                                unsigned MaxLookup) {
  SmallVector<Value *, 4> O;
  GetUnderlyingSSACopiesOrUnderlyingObjects(V, O, DL, LI, MaxLookup);

  if (O.size() != 1) {
    DEBUG(dbgs() << "Found multiple underlying ssa_copy:\n");
    DEBUG(llvm::for_each(O, [](const Value *V) { dbgs() << *V << "\n"; }));
    return nullptr;
  }

  return O.front();
}

SmallVector<Value *, 4> llvm::CollectSSACopyChain(Value *V,
                                                  const DataLayout &DL,
                                                  LoopInfo *LI,
                                                  unsigned MaxLookup) {
  assert(V && "Value is nullptr!");
  SmallVector<Value *, 4> Collected;
  DEBUG(dbgs() << "CollectSSACopyChain:\n" << *V << "\n");
  V = GetUniqueSSACopyOrUnderlyingObject(V, DL, LI, MaxLookup);
  DEBUG(dbgs() << "Collected:\n");
  while (auto *I = dyn_cast_or_null<SSACopyInst>(V)) {
    DEBUG(dbgs() << *V << "\n");
    Collected.emplace_back(I);
    V = GetUniqueSSACopyOrUnderlyingObject(I->getValue(), DL, LI, MaxLookup);
  }
  DEBUG((V ? dbgs() << *V : dbgs() << "  ***nullptr***") << "\n");
  Collected.emplace_back(V);
  return Collected;
}
