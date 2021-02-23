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
// Test supported HLS pragma APIs on functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/XILINXFunctionUtils.h"
#include "TestUtils.h"
#include "gtest/gtest.h"

using namespace llvm;

TEST(XILINXFunctionUtilsTest, Dataflow) {
  const char *ModuleStr =
      "target datalayout = "
      "\"e-m:e-i64:64-i128:128-i256:256-i512:512-i1024:1024-i2048:2048-i4096:"
      "4096-n8:16:32:64-S128-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:"
      "256-v512:512-v1024:1024\"\n"
      "target triple = \"fpga64-xilinx-none\"\n"
      "declare void @g(i32*)\n"
      "define void @t() {\n"
      "entry:\n"
      "  ret void\n"
      "}\n";

  // Parse the module.
  LLVMContext Context;
  std::unique_ptr<Module> M = makeLLVMModule(Context, ModuleStr);

  // Check if succeed to query and add dataflow attribute on Function g.
  auto *Fg = getFunctionFrom(*M, "g");
  EXPECT_FALSE(isDataFlow(Fg));
  addDataFlow(Fg);
  EXPECT_TRUE(isDataFlow(Fg));

  // Check if succeed to query and add dataflow attribute on Function t.
  auto *Ft = getFunctionFrom(*M, "t");
  EXPECT_FALSE(isDataFlow(Ft));
  addDataFlow(Ft);
  EXPECT_TRUE(isDataFlow(Ft));
}

TEST(XILINXFunctionUtilsTest, Pipeline) {
  const char *ModuleStr =
      "target datalayout = "
      "\"e-m:e-i64:64-i128:128-i256:256-i512:512-i1024:1024-i2048:2048-i4096:"
      "4096-n8:16:32:64-S128-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:"
      "256-v512:512-v1024:1024\"\n"
      "target triple = \"fpga64-xilinx-none\"\n"
      "declare void @g(i32*)\n"
      "define void @t() {\n"
      "entry:\n"
      "  ret void\n"
      "}\n";

  // Parse the module.
  LLVMContext Context;
  std::unique_ptr<Module> M = makeLLVMModule(Context, ModuleStr);

  // Check if succeed to query and add pipeline attribute on Function g.
  auto *Fg = getFunctionFrom(*M, "g");
  EXPECT_FALSE(isPipeline(Fg));
  addPipeline(Fg, 1, PipelineStyle::FLP);
  EXPECT_TRUE(isPipeline(Fg));
  EXPECT_FALSE(isPipelineOff(Fg));

  // Check if succeed to generate correct information for the pipelined Function g.
  Optional<long long> II = getPipelineII(Fg);
  EXPECT_TRUE(II.hasValue());
  EXPECT_EQ(II.getValue(), 1);
  Optional<PipelineStyle> Style = getPipelineStyle(Fg);
  EXPECT_TRUE(Style.hasValue());
  EXPECT_EQ(Style.getValue(), PipelineStyle::FLP);

  // Check if succeed to query and add pipeline off attribute on Function g.
  addPipelineOff(Fg);
  EXPECT_FALSE(isPipeline(Fg));
  EXPECT_TRUE(isPipelineOff(Fg));

  // Check if succeed to query and add pipeline attribute on Function t.
  auto *Ft = getFunctionFrom(*M, "t");
  EXPECT_FALSE(isPipeline(Ft));
  addPipeline(Ft, 1, PipelineStyle::FLP);
  EXPECT_TRUE(isPipeline(Ft));
  EXPECT_FALSE(isPipelineOff(Ft));

  // Check if succeed to generate correct information for the pipelined Function t.
  II = getPipelineII(Ft);
  EXPECT_TRUE(II.hasValue());
  EXPECT_EQ(II.getValue(), 1);
  Style = getPipelineStyle(Ft);
  EXPECT_TRUE(Style.hasValue());
  EXPECT_EQ(Style.getValue(), PipelineStyle::FLP);

  // Check if succeed to query and add pipeline off attribute on Function t.
  addPipelineOff(Ft);
  EXPECT_FALSE(isPipeline(Ft));
  EXPECT_TRUE(isPipelineOff(Ft));
}
