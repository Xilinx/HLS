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
// This file declares common functions for manipulating XILINX HLS Function.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_XILINXFUNCTIONINFOUTILS_H
#define LLVM_TRANSFORMS_UTILS_XILINXFUNCTIONINFOUTILS_H

#include "llvm/Analysis/XILINXFunctionInfoUtils.h"
#include "llvm/IR/Function.h"

namespace llvm {

/// Add dataflow attribute to Function \p F.
void addDataFlow(Function *F);

/// Add pipeline attribute to Function \p F.
void addPipeline(Function *F, int32_t II = -1,
                 PipelineStyle Style = PipelineStyle::Default);

/// Add pipeline off(force not to pipeline) attribute to Function \p F.
void addPipelineOff(Function *F);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_XILINXFUNCTIONINFOUTILS_H
