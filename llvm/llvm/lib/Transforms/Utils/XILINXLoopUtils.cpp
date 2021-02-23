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

#include "llvm/Transforms/Utils/XILINXLoopUtils.h"
#include "llvm/IR/Constants.h"

using namespace llvm;

static LLVMContext &GetContext(const Loop *L) {
  return L->getHeader()->getContext();
}

static bool IsUnrollLoopMetadata(StringRef Attr) {
  return Attr.equals("llvm.loop.unroll.full") ||
         Attr.equals("llvm.loop.unroll.withoutcheck") ||
         Attr.equals("llvm.loop.unroll.count") ||
         Attr.equals("llvm.loop.unroll.disable");
}

/// NOTE: If there is already \p Attr existed for Loop \p L, drops it and
///       appends with the new one.
void llvm::addLoopMetadata(Loop *L, StringRef Attr,
                           ArrayRef<Metadata *> Options) {
  bool IsUnrollLoopMD = IsUnrollLoopMetadata(Attr);

  // Reserve first location for self reference to the LoopID metadata node.
  SmallVector<Metadata *, 4> MDs(1);

  // Check if there's already LoopID.
  if (MDNode *LoopID = L->getLoopID())
    for (unsigned i = 1, ie = LoopID->getNumOperands(); i < ie; ++i) {
      MDNode *MD = cast<MDNode>(LoopID->getOperand(i));

      // Already have given Attr? Drop it.
      if (const MDString *S = dyn_cast<MDString>(MD->getOperand(0)))
        if ((S->getString() == Attr) ||
            (IsUnrollLoopMD && IsUnrollLoopMetadata(S->getString())))
          continue;

      MDs.push_back(LoopID->getOperand(i));
    }

  // Appends the Attr + Options and creates new loop id.
  LLVMContext &Context = GetContext(L);
  SmallVector<Metadata *, 8> Vec;
  Vec.push_back(MDString::get(Context, Attr));
  if (!Options.empty())
    Vec.append(Options.begin(), Options.end());
  MDs.push_back(MDNode::get(Context, Vec));
  MDNode *NewLoopID = MDNode::get(Context, MDs);

  // Set operand 0 to refer to the loop id itself.
  NewLoopID->replaceOperandWith(0, NewLoopID);
  L->setLoopID(NewLoopID);
}

void llvm::addLoopTripCount(Loop *L, uint32_t Min, uint32_t Max, uint32_t Avg) {
  Type *Int32Ty = Type::getInt32Ty(GetContext(L));
  addLoopMetadata(L, "llvm.loop.tripcount",
                  {ConstantAsMetadata::get(ConstantInt::get(Int32Ty, Min)),
                   ConstantAsMetadata::get(ConstantInt::get(Int32Ty, Max)),
                   ConstantAsMetadata::get(ConstantInt::get(Int32Ty, Avg))});
}

void llvm::addDataFlow(Loop *L) {
  addLoopMetadata(L, "llvm.loop.dataflow.enable");
}

/// II = -1 : default "II" value
/// II = 0  : force no pipeline. Query with isPipelineOff instead.
/// II > 0  : customized "II" value
void llvm::addPipeline(Loop *L, int32_t II, bool IsRewind,
                       PipelineStyle Style) {
  LLVMContext &Context = GetContext(L);
  addLoopMetadata(L, "llvm.loop.pipeline.enable",
                  {ConstantAsMetadata::get(
                       ConstantInt::getSigned(Type::getInt32Ty(Context), II)),
                   ConstantAsMetadata::get(
                       ConstantInt::get(Type::getInt1Ty(Context), IsRewind)),
                   ConstantAsMetadata::get(ConstantInt::get(
                       Type::getInt8Ty(Context), static_cast<int>(Style)))});
}

void llvm::addPipelineOff(Loop *L) {
  addPipeline(L, /* No pipeline */ 0, false);
}

void llvm::addFullyUnroll(Loop *L) {
  addLoopMetadata(L, "llvm.loop.unroll.full");
}

void llvm::addPartialUnroll(Loop *L, uint32_t Factor, bool SkipExitCheck) {
  SmallVector<Metadata *, 1> MD(
      1, ConstantAsMetadata::get(
             ConstantInt::get(Type::getInt32Ty(GetContext(L)), Factor)));
  if (SkipExitCheck) {
    addLoopMetadata(L, "llvm.loop.unroll.withoutcheck", MD);
    return;
  }
  addLoopMetadata(L, "llvm.loop.unroll.count", MD);
}

void llvm::addUnrollOff(Loop *L) {
  addLoopMetadata(L, "llvm.loop.unroll.disable");
}

void llvm::addFlatten(Loop *L) {
  addLoopMetadata(L, "llvm.loop.flatten.enable",
                  {ConstantAsMetadata::get(
                      ConstantInt::get(Type::getInt1Ty(GetContext(L)), 1))});
}

void llvm::addFlattenOff(Loop *L) {
  addLoopMetadata(L, "llvm.loop.flatten.enable",
                  {ConstantAsMetadata::get(ConstantInt::get(
                      Type::getInt1Ty(GetContext(L)), /* No flatten */ 0))});
}
