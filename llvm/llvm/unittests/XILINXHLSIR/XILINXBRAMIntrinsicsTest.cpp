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
// Test building FPGA intrinsics: fpga_bram_load, fpga_bram_store.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "gtest/gtest.h"

using namespace llvm;

TEST(BRAMIntrinsicTest, IRBuilder) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M;
  Function *F = nullptr;
  BasicBlock *BB = nullptr;

  M.reset(new Module("MyModule", Ctx));

  auto *EltTy = IntegerType::get(Ctx, 32);
  auto *MaskTy = IntegerType::get(Ctx, 4);
  auto *PtrTy = PointerType::getUnqual(EltTy);

  FunctionType *FTy =
      FunctionType::get(Type::getVoidTy(Ctx), {EltTy, PtrTy, MaskTy},
                        /*isVarArg=*/false);
  F = Function::Create(FTy, Function::ExternalLinkage, "", M.get());
  BB = BasicBlock::Create(Ctx, "", F);

  IRBuilder<> Builder(BB);

  Function *BramLoad = Intrinsic::getDeclaration(
      M.get(), Intrinsic::fpga_bram_load, {EltTy, PtrTy});
  Function *BramStore = Intrinsic::getDeclaration(
      M.get(), Intrinsic::fpga_bram_store, {EltTy, PtrTy, MaskTy});

  auto *Ld = Builder.CreateCall(BramLoad, {std::next(F->arg_begin())});
  auto *St =
      Builder.CreateCall(BramStore, {F->arg_begin(), std::next(F->arg_begin()),
                                     std::next(F->arg_begin(), 2)});

  EXPECT_EQ(cast<IntrinsicInst>(Ld)->getIntrinsicID(),
            Intrinsic::fpga_bram_load);
  EXPECT_EQ(cast<IntrinsicInst>(St)->getIntrinsicID(),
            Intrinsic::fpga_bram_store);
}
