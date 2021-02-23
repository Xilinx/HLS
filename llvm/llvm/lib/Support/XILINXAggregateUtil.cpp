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
// This file defines aggregate related APIs
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Type.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/XILINXAggregateUtil.h"
#include "llvm/Support/MathExtras.h"

namespace llvm {

uint64_t getAggregatedBitwidthInBitLevel(const DataLayout &DL,
                                                       Type *Ty) {
  if (Ty->isArrayTy())
    return Ty->getArrayNumElements() *
           getAggregatedBitwidthInBitLevel(DL, Ty->getArrayElementType());
  else if (Ty->isStructTy()) {
    uint64_t BW = 0;
    for (unsigned i = 0; i < Ty->getStructNumElements(); i++) {
      BW += getAggregatedBitwidthInBitLevel(DL, Ty->getStructElementType(i));
    }
    return BW;
  // Maybe VectorTy?
  } else
    return DL.getTypeSizeInBits(Ty);
}

uint64_t getAggregatedBitwidthInByteLevel(const DataLayout &DL,
                                                       Type *Ty) {
  if (Ty->isArrayTy())
    return Ty->getArrayNumElements() *
           getAggregatedBitwidthInByteLevel(DL, Ty->getArrayElementType());
  else if (Ty->isStructTy()) {
    uint64_t BW = 0;
    for (unsigned i = 0; i < Ty->getStructNumElements(); i++) {
      BW += getAggregatedBitwidthInByteLevel(DL, Ty->getStructElementType(i));
    }
    return BW;
  // Maybe VectorTy?
  } else
    return alignTo(DL.getTypeSizeInBits(Ty), 8);
}

uint64_t getAggregatedBitwidth(const DataLayout &DL, Type *Ty,
                               AggregateType AggrTy) {
  switch (AggrTy) {
  case AggregateType::Bit:
    return getAggregatedBitwidthInBitLevel(DL, Ty);
  case AggregateType::Byte:
    return getAggregatedBitwidthInByteLevel(DL, Ty);
  case AggregateType::NoCompact:
    return DL.getTypeAllocSizeInBits(Ty);
  default:
    llvm_unreachable("Get aggregate bitwidth for NoSpec?!");
  }
  return 0;
}

void getStructFieldOffsetSizePairsInBits(
    const DataLayout &DL, Type *Ty, uint64_t Start,
    SmallVectorImpl<std::pair<uint64_t, uint64_t>> &PairVec) {
  if (Ty->isStructTy()) {
    auto *SL = DL.getStructLayout(cast<StructType>(Ty));
    for (unsigned i = 0; i < Ty->getStructNumElements(); i++)
      getStructFieldOffsetSizePairsInBits(DL, Ty->getStructElementType(i),
                                          Start + SL->getElementOffsetInBits(i),
                                          PairVec);
  } else if (Ty->isArrayTy()) {
    auto *EleTy = Ty->getArrayElementType();
    auto EleSize = DL.getTypeSizeInBits(EleTy);
    for (unsigned i = 0; i < Ty->getArrayNumElements(); i++)
      getStructFieldOffsetSizePairsInBits(DL, EleTy, Start + EleSize * i,
                                          PairVec);
  } else { // arrive at leaf
    PairVec.push_back(std::make_pair(Start, DL.getTypeSizeInBits(Ty)));
  }
}

std::string getAggregateTypeStr(AggregateType AggrTy) {
  switch(AggrTy) {
  case AggregateType::Default:
    return "default";
  case AggregateType::Bit:
    return "field bit alignment";
  case AggregateType::Byte:
    return "field byte alignment";
  case AggregateType::NoSpec:
    return "non-compact";
  default:
    return "default";
  }
  return "default";
}

std::string getHwTypeStr(HwType HwTy) {
  switch (HwTy) {
  case HwType::Scalar:
    return "scalar";
  case HwType::BRAM:
    return "bram";
  case HwType::FIFO_ARRAY:
    return "fifo (array-to-stream)";
  case HwType::FIFO_HLS_STREAM:
    return "fifo (hls::stream)";
  case HwType::AXIS_ARRAY:
    return "axis (array-to-stream)";
  case HwType::AXIS_HLS_STREAM:
    return "axis (hls::stream)";
  case HwType::AXIS_SIDE_CHANNEL:
    return "axis (with side-channel)";
  case HwType::MAXI:
    return "maxi";
  default:
    llvm_unreachable("other HW types?!");
  }
  return "";
}

} // namespace llvm
