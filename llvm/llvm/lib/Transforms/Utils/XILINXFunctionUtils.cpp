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

#include "llvm/Transforms/Utils/XILINXFunctionUtils.h"

using namespace llvm;

void llvm::addDataFlow(Function *F) {
  F->addFnAttr("fpga.dataflow.func", /* disable start propagation */ "0");
}

/// II = -1 : default "II" value
/// II = 0  : force no pipeline. Query with isPipelineOff instead.
/// II > 0  : customized "II" value
void llvm::addPipeline(Function *F, int32_t II, PipelineStyle Style) {
  auto StyleCode = static_cast<int>(Style);
  F->addFnAttr("fpga.static.pipeline",
               (std::to_string(II) + '.' + std::to_string(StyleCode)));
}

void llvm::addPipelineOff(Function *F) {
  addPipeline(F, /* No pipeline */ 0);
}
