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
// This file declares common functions useful for getting use-def/def-use
// information in XILINX HLS IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_XILINXHLSVALUETRACKINGUTILS_H
#define LLVM_ANALYSIS_XILINXHLSVALUETRACKINGUTILS_H

#include "llvm/ADT/SmallVector.h"

namespace llvm {

class Value;
class DataLayout;
class LoopInfo;
class GlobalValue;
class Type;

bool IsGlobalUseEmpty(const GlobalValue &GV);

bool IsHLSStream(const Value *V);

Type *StripPadding(Type *T, const DataLayout &DL);

Type *extractHLSStreamEltType(Type *T);

/// Same as llvm::GetUnderlyingObject but stops on ssa_copy
Value *GetUnderlyingSSACopyOrUnderlyingObject(Value *V, const DataLayout &DL,
                                              unsigned MaxLookup = 6);

/// Same as llvm::GetUnderlyingObjects but stops on ssa_copy
void GetUnderlyingSSACopiesOrUnderlyingObjects(
    Value *V, SmallVectorImpl<Value *> &Objects, const DataLayout &DL,
    LoopInfo *LI = nullptr, unsigned MaxLookup = 6);

/// Returns an ssa_copy or the final underlying object or nullptr if ambiguous
Value *GetUniqueSSACopyOrUnderlyingObject(Value *V, const DataLayout &DL,
                                          LoopInfo *LI, unsigned MaxLookup = 6);

/// Returns a chain of ssa_copy finished by the final underlying object, or
/// finished by nullptr if ambiguous at that level
SmallVector<Value *, 4> CollectSSACopyChain(Value *V, const DataLayout &DL,
                                            LoopInfo *LI,
                                            unsigned MaxLookup = 6);

} // end namespace llvm

#endif // LLVM_ANALYSIS_XILINXHLSVALUETRACKINGUTILS_H
