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
// This file declares common functions useful for getting information of a
// XILINX HLS Function.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_XILINXFUNCTIONINFOUTILS_H
#define LLVM_ANALYSIS_XILINXFUNCTIONINFOUTILS_H

#include "llvm/ADT/Optional.h"
#include "llvm/Analysis/XILINXLoopInfoUtils.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Function.h"

namespace llvm {

/// Returns true if Function \p F is a dataflow function.
bool isDataFlow(const Function *F);

/// Returns true if Function \p F is a pipeline function.
bool isPipeline(const Function *F);

/// Returns true if Function \p F is a function that must not be pipelined.
bool isPipelineOff(const Function *F);

/// Get target II for pipeline Function \p F. Retrun None if Function \p F is
/// not a pipeline function.
Optional<long long> getPipelineII(const Function *F);

/// Returns pipeline style of pipeline Function \p F.
Optional<PipelineStyle> getPipelineStyle(const Function *F);

/// Returns true if Function \p F is marked as always inline.
bool isAlwaysInline(const Function *F);

/// Returns true if CallSite \p CS is marked as always inline.
bool isAlwaysInline(const CallSite CS);

/// Returns true if Function \p F is marked as no inline.
bool isNoInline(const Function *F);

/// Returns true if CallSite \p CS is marked as no inline.
bool isNoInline(const CallSite CS);

/// Returns true if Function \p F is top function
bool isTop(const Function *F);

/// Returns Function \p Top 's name if it's a top function.
Optional<const std::string> getTopFunctionName(const Function *Top);

} // end namespace llvm

#endif // LLVM_ANALYSIS_XILINXFUNCTIONINFOUTILS_H
