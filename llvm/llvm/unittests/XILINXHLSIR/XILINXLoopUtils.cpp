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
// Test supported HLS pragma APIs on loop-simplify form loops.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/XILINXLoopUtils.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "TestUtils.h"
#include "gtest/gtest.h"

using namespace llvm;

/// Build the loop info for the function and run the Test.
static void runWithLoopInfo(
    Module &M, StringRef FuncName,
    function_ref<void(Function &F, LoopInfo &LI, ScalarEvolution &SE)> Test) {
  auto *F = M.getFunction(FuncName);
  ASSERT_NE(F, nullptr) << "Could not find " << FuncName;
  // Compute the dominator tree and the loop info for the function.
  DominatorTree DT(*F);
  LoopInfo LI(DT);
  Triple Trip(M.getTargetTriple());
  TargetLibraryInfoImpl TLII(Trip);
  TargetLibraryInfo TLI(TLII);
  AssumptionCache AC(*F);
  ScalarEvolution SE(*F, TLI, AC, DT, LI);
  Test(*F, LI, SE);
}

TEST(XILINXLoopUtilsTest, ForLoopForm) {
  const char *ModuleStr =
      "target datalayout = "
      "\"e-m:e-i64:64-i128:128-i256:256-i512:512-i1024:1024-i2048:2048-i4096:"
      "4096-n8:16:32:64-S128-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:"
      "256-v512:512-v1024:1024\"\n"
      "target triple = \"fpga64-xilinx-none\"\n"
      "declare void @g(i32*)\n"
      "define void @test1() {\n"
      "entry:\n"
      "  %array = alloca [20 x i32], align 16\n"
      "  br label %for.cond\n"
      "for.cond:\n"
      "  %i.0 = phi i32 [ 0, %entry ], [ %inc, %for.body ]\n"
      "  %cmp = icmp slt i32 %i.0, 100\n"
      "  %arrayidx = getelementptr inbounds [20 x i32], [20 x i32]* %array, "
      "i64 0, i64 0\n"
      "  br i1 %cmp, label %for.body, label %for.end\n"
      "for.body:\n"
      "  store i32 0, i32* %arrayidx, align 16\n"
      "  %inc = add nsw i32 %i.0, 1\n"
      "  br label %for.cond\n"
      "for.end:\n"
      "  %arrayidx.lcssa = phi i32* [ %arrayidx, %for.cond ]\n"
      "  call void @g(i32* %arrayidx.lcssa)\n"
      "  ret void\n"
      "}\n";

  // Parse the module.
  LLVMContext Context;
  std::unique_ptr<Module> M = makeLLVMModule(Context, ModuleStr);

  runWithLoopInfo(
      *M, "test1", [&](Function &F, LoopInfo &LI, ScalarEvolution &SE) {
        Function::iterator FI = F.begin();
        // First basic block is entry - skip it.
        BasicBlock *Header = &*(++FI);
        assert(Header->getName() == "for.cond");
        Loop *L = LI.getLoopFor(Header);

        // Test if Loop L is for loop.
        EXPECT_TRUE(L->isLoopSimplifyForm());
        EXPECT_TRUE(isForLoop(L));

        // Check if succeed to query and emit loop trip count information.
        Optional<LoopTripCountMDInfo> EmptyLTCInfo = getLoopTripCount(L);
        EXPECT_FALSE(EmptyLTCInfo.hasValue());
        addLoopTripCount(L, 20u, 20u, 20u);
        Optional<LoopTripCountMDInfo> LTCInfo = getLoopTripCount(L);
        EXPECT_EQ(LTCInfo.getValue().getMin(), 20u);
        EXPECT_EQ(LTCInfo.getValue().getMax(), 20u);
        EXPECT_EQ(LTCInfo.getValue().getAvg(), 20u);

        // Check if succeed to query and add dataflow metadata to Loop L.
        EXPECT_FALSE(isDataFlow(L));
        addDataFlow(L);
        EXPECT_TRUE(isDataFlow(L));

        // Check if succeed to query and add pipeline metadata to Loop L.
        EXPECT_FALSE(isPipeline(L));
        EXPECT_FALSE(isPipelineOff(L));
        addPipeline(L, 1, true);
        EXPECT_TRUE(isPipeline(L));
        EXPECT_TRUE(isPipelineRewind(L));
        EXPECT_FALSE(isPipelineOff(L));
        EXPECT_EQ(getPipelineIIInt64(L), 1);
        EXPECT_EQ(getPipelineStyle(L).getValue(), PipelineStyle::Default);
        addPipelineOff(L);
        EXPECT_FALSE(isPipeline(L));
        EXPECT_TRUE(isPipelineOff(L));

        // Check if succeed to query and add flatten metadata to Loop L.
        EXPECT_FALSE(isFlatten(L));
        EXPECT_FALSE(isFlattenOff(L));
        addFlatten(L);
        EXPECT_TRUE(isFlatten(L));
        addFlattenOff(L);
        EXPECT_FALSE(isFlatten(L));
        EXPECT_TRUE(isFlattenOff(L));

        // Check if succeed to query and add fully unroll metadata to Loop L.
        EXPECT_FALSE(hasUnrollEnableMetadata(L));
        EXPECT_FALSE(isFullyUnroll(L));
        addFullyUnroll(L);
        EXPECT_TRUE(isFullyUnroll(L));

        // Check the loop might be exposed into dataflow region.
        EXPECT_TRUE(mayExposeInDataFlowRegion(SE, L));

        // Check if succeed to query and add disable unroll metadata to Loop L.
        EXPECT_FALSE(isUnrollOff(L));
        addUnrollOff(L);
        EXPECT_TRUE(isUnrollOff(L));

        // Check if succeed to query and add partial unroll with exit check
        // metadata to Loop L.
        EXPECT_FALSE(hasUnrollEnableMetadata(L));
        addPartialUnroll(L, 4u, true);
        EXPECT_FALSE(isUnrollOff(L));
        EXPECT_TRUE(isWithoutExitCheckUnroll(L));
        EXPECT_EQ(getUnrollFactorUInt64(L), 4u);

        // Check if succeed to query and add partial unroll without exit check
        // metadata to Loop L.
        addPartialUnroll(L, 5u, false);
        EXPECT_FALSE(isWithoutExitCheckUnroll(L));
        EXPECT_EQ(getUnrollFactorUInt64(L), 5u);
      });
}

TEST(XILINXLoopUtilsTest, LoopRotateForm) {
  const char *ModuleStr =
      "target datalayout = "
      "\"e-m:e-i64:64-i128:128-i256:256-i512:512-i1024:1024-i2048:2048-i4096:"
      "4096-n8:16:32:64-S128-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:"
      "256-v512:512-v1024:1024\"\n"
      "target triple = \"fpga64-xilinx-none\"\n"
      "declare void @g(i32*)\n"
      "define void @test2() {\n"
      "entry:\n"
      "  %array = alloca [20 x i32], align 16\n"
      "  %arrayidx = getelementptr inbounds [20 x i32], [20 x i32]* %array, "
      "i64 0, i64 0\n"
      "  br label %for.body\n"
      "for.body:\n"
      "  %i.01 = phi i32 [ 0, %entry ], [ %inc, %for.body ]\n"
      "  store i32 0, i32* %arrayidx, align 16\n"
      "  %inc = add nsw i32 %i.01, 1\n"
      "  %cmp = icmp slt i32 %inc, 100\n"
      "  br i1 %cmp, label %for.body, label %for.end\n"
      "for.end:\n"
      "  %arrayidx.lcssa = phi i32* [ %arrayidx, %for.body ]\n"
      "  call void @g(i32* %arrayidx.lcssa)\n"
      "  ret void\n"
      "}\n";

  // Parse the module.
  LLVMContext Context;
  std::unique_ptr<Module> M = makeLLVMModule(Context, ModuleStr);

  runWithLoopInfo(
      *M, "test2", [&](Function &F, LoopInfo &LI, ScalarEvolution &SE) {
        Function::iterator FI = F.begin();
        // First basic block is entry - skip it.
        BasicBlock *Header = &*(++FI);
        assert(Header->getName() == "for.body");
        Loop *L = LI.getLoopFor(Header);

        // Test if Loop L is do-while form.
        EXPECT_TRUE(L->isLoopSimplifyForm());
        // TODO: Need to import from upstream LLVM
        // EXPECT_TRUE(L->isRotatedForm());
        EXPECT_FALSE(isForLoop(L));

        // Check if succeed to query and emit loop trip count information.
        Optional<LoopTripCountMDInfo> EmptyLTCInfo = getLoopTripCount(L);
        EXPECT_FALSE(EmptyLTCInfo.hasValue());
        addLoopTripCount(L, 20u, 20u, 20u);
        Optional<LoopTripCountMDInfo> LTCInfo = getLoopTripCount(L);
        EXPECT_EQ(LTCInfo.getValue().getMin(), 20u);
        EXPECT_EQ(LTCInfo.getValue().getMax(), 20u);
        EXPECT_EQ(LTCInfo.getValue().getAvg(), 20u);

        // Check if succeed to query and add dataflow metadata to Loop L.
        EXPECT_FALSE(isDataFlow(L));
        addDataFlow(L);
        EXPECT_TRUE(isDataFlow(L));

        // Check if succeed to query and add pipeline metadata to Loop L.
        EXPECT_FALSE(isPipeline(L));
        EXPECT_FALSE(isPipelineOff(L));
        addPipeline(L, 1, true);
        EXPECT_TRUE(isPipeline(L));
        EXPECT_TRUE(isPipelineRewind(L));
        EXPECT_FALSE(isPipelineOff(L));
        EXPECT_EQ(getPipelineIIInt64(L), 1);
        EXPECT_EQ(getPipelineStyle(L).getValue(), PipelineStyle::Default);
        addPipelineOff(L);
        EXPECT_FALSE(isPipeline(L));
        EXPECT_TRUE(isPipelineOff(L));

        // Check if succeed to query and add flatten metadata to Loop L.
        EXPECT_FALSE(isFlatten(L));
        EXPECT_FALSE(isFlattenOff(L));
        addFlatten(L);
        EXPECT_TRUE(isFlatten(L));
        addFlattenOff(L);
        EXPECT_FALSE(isFlatten(L));
        EXPECT_TRUE(isFlattenOff(L));

        // Check if succeed to query and add fully unroll metadata to Loop L.
        EXPECT_FALSE(hasUnrollEnableMetadata(L));
        EXPECT_FALSE(isFullyUnroll(L));
        addFullyUnroll(L);
        EXPECT_TRUE(isFullyUnroll(L));

        // Check the loop might be exposed into dataflow region.
        EXPECT_TRUE(mayExposeInDataFlowRegion(SE, L));

        // Check if succeed to query and add disable unroll metadata to Loop L.
        EXPECT_FALSE(isUnrollOff(L));
        addUnrollOff(L);
        EXPECT_TRUE(isUnrollOff(L));

        // Check if succeed to query and add partial unroll with exit check
        // metadata to Loop L.
        EXPECT_FALSE(hasUnrollEnableMetadata(L));
        addPartialUnroll(L, 4u, true);
        EXPECT_FALSE(isUnrollOff(L));
        EXPECT_TRUE(isWithoutExitCheckUnroll(L));
        EXPECT_EQ(getUnrollFactorUInt64(L), 4u);

        // Check if succeed to query and add partial unroll without exit check
        // metadata to Loop L.
        addPartialUnroll(L, 5u, false);
        EXPECT_FALSE(isWithoutExitCheckUnroll(L));
        EXPECT_EQ(getUnrollFactorUInt64(L), 5u);
      });
}
