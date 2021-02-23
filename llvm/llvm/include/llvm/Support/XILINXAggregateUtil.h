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

#ifndef AGGREGATE_INFO_H
#define AGGREGATE_INFO_H

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Type.h"
#include <cstdint>

namespace llvm {

enum class AggregateType {
  NoSpec = 0,   // no aggregate pragma at all
  Default,      // aggregate pragma without compact option
  Bit,          // -compact bit, field bit alignment
  Byte,         // -compact byte, field byte alignment
  NoCompact     // -compact none, no padding removal
};

enum class HwType {
  None,
  FIFO,              // temp type (stream pragma or ap_fifo interface)
  HLS_STREAM,        // temp type (hls::stream)
  AXIS,              // temp type (axis pragma)
  Scalar,            // final type
  BRAM,              // final type
  FIFO_ARRAY,        // final type (array-to-stream: stream pragma on array)
  FIFO_HLS_STREAM,   // final type (stream pragma on hls::stream)
  AXIS_ARRAY,        // final type (array-to-axis: axis pragma on array)
  AXIS_HLS_STREAM,   // final type (axis pragma on hls::stream)
  AXIS_SIDE_CHANNEL, // final type (from axis-with-side-channel intrinsics)
  MAXI               // final type (maxi pragma)
};

struct AggregateInfo {
  AggregateType AggrTy;
  uint64_t WordSize; // In bits

  AggregateInfo(AggregateType AggrTy = AggregateType::NoSpec,
                uint64_t WordSize = 0 /*0 means no spec*/)
      : AggrTy(AggrTy), WordSize(WordSize){};
  AggregateInfo(uint64_t WordSize /*0 means no spec*/,
                AggregateType AggrTy = AggregateType::NoSpec)
      : AggrTy(AggrTy), WordSize(WordSize){};
  AggregateType getAggregateType() { return AggrTy; }
  uint64_t getWordSize() { return WordSize; }
};

uint64_t getAggregatedBitwidthInBitLevel(const DataLayout &DL, Type *Ty);

uint64_t getAggregatedBitwidthInByteLevel(const DataLayout &DL,
                                          Type *Ty);

uint64_t getAggregatedBitwidth(const DataLayout &DL, Type *Ty,
                               AggregateType AggrTy);

void getStructFieldOffsetSizePairsInBits(
    const DataLayout &DL, Type *Ty, uint64_t Start,
    SmallVectorImpl<std::pair<uint64_t, uint64_t>> &PairVec);

std::string getAggregateTypeStr(AggregateType AggrTy);

std::string getHwTypeStr(HwType HwTy);

} // namespace llvm
#endif // !AGGREGATE_INFO_H
