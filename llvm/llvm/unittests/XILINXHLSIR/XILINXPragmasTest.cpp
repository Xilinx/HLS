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
// Test supported HLS pragma APIs on variables.
//
// Not yet support HLS pragma APIs on top function argument.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/XILINXFPGAIntrinsicInst.h"
#include "llvm/IR/XILINXHLSIRBuilder.h"
#include "gtest/gtest.h"

using namespace llvm;

/// \brief Basic routine for launching HLSIRBuilderTest.
class HLSIRBuilderTest : public testing::Test {
protected:
  void SetUp() override {
    M.reset(new Module("MyModule", Ctx));
    FunctionType *FTy = FunctionType::get(Type::getVoidTy(Ctx),
                                          /*isVarArg=*/false);
    F = Function::Create(FTy, Function::ExternalLinkage, "", M.get());
    BB = BasicBlock::Create(Ctx, "", F);
    Type *Int8Ty = Type::getInt8Ty(Ctx);
    SmallVector<Constant *, 2> Initializers{ConstantInt::get(Int8Ty, 1),
                                            ConstantInt::get(Int8Ty, 2)};
    ArrayType *ArrayOfInt8Ty = ArrayType::get(Int8Ty, Initializers.size());
    GV = new GlobalVariable(*M, ArrayOfInt8Ty, true,
                            GlobalValue::InternalLinkage,
                            ConstantArray::get(ArrayOfInt8Ty, Initializers));
    AI = new AllocaInst(Int8Ty, 0, ConstantInt::get(Int8Ty, 10), "lv", BB);
  }

  void TearDown() override {
    BB = nullptr;
    M.reset();
  }

  LLVMContext Ctx;
  std::unique_ptr<Module> M;
  Function *F;
  BasicBlock *BB;
  GlobalVariable *GV;
  AllocaInst *AI;
};

TEST_F(HLSIRBuilderTest, ArrayPartition) {
  HLSIRBuilder IB(BB);

  // Check if succeed to create array partition PragmaInst on local alloca.
  Value *APV = IB.CreateArrayPartitionInst(
      AI, ArrayXFormInst<ArrayPartitionInst>::XFormMode::Block, 1, 2);
  EXPECT_TRUE(isa<ArrayPartitionInst>(APV));

  // Check if succeed to get the exact same PragmInst from local alloca.
  ArrayPartitionInst *API = ArrayPartitionInst::get(AI);
  EXPECT_EQ(API, APV);

  // Check if succeed to generate correct information in array partition
  // PragmInst.
  EXPECT_EQ(API->getMode(), "block");
  EXPECT_EQ(API->getDim(), 1);
  EXPECT_EQ(API->getFactor(), 2);

  // Check if succeed to create array partition PragmaInst on global variable.
  Value *APGV = IB.CreateArrayPartitionInst(
      GV, ArrayXFormInst<ArrayPartitionInst>::XFormMode::Complete, 1);
  EXPECT_TRUE(isa<ArrayPartitionInst>(APGV));

  // Check if succeed to get the exact same PragmInst from global variable.
  ArrayPartitionInst *APGI = ArrayPartitionInst::get(GV);
  EXPECT_EQ(APGI, APGV);

  // Check if succeed to generate correct information in array partition
  // PragmInst.
  EXPECT_EQ(APGI->getMode(), "complete");
  EXPECT_EQ(APGI->getDim(), 1);
  EXPECT_EQ(APGI->getFactor(), 0);
}

TEST_F(HLSIRBuilderTest, ArrayReshape) {
  HLSIRBuilder IB(BB);

  // Check if succeed to create array reshape PragmaInst on local alloca.
  Value *APV = IB.CreateArrayReshapeInst(
      AI, ArrayXFormInst<ArrayReshapeInst>::XFormMode::Cyclic, 1, 5);
  EXPECT_TRUE(isa<ArrayReshapeInst>(APV));

  // Check if succeed to get the exact same PragmInst from local alloca.
  ArrayReshapeInst *API = ArrayReshapeInst::get(AI);
  EXPECT_EQ(API, APV);

  // Check if succeed to generate correct information in array reshape
  // PragmInst.
  EXPECT_EQ(API->getMode(), "cyclic");
  EXPECT_EQ(API->getDim(), 1);
  EXPECT_EQ(API->getFactor(), 5);

  // Check if succeed to create array reshape PragmaInst on global variable.
  Value *APGV = IB.CreateArrayReshapeInst(
      GV, ArrayXFormInst<ArrayReshapeInst>::XFormMode::Complete, 1);
  EXPECT_TRUE(isa<ArrayReshapeInst>(APGV));

  // Check if succeed to get the exact same PragmInst from global variable.
  ArrayReshapeInst *APGI = ArrayReshapeInst::get(GV);
  EXPECT_EQ(APGI, APGV);

  // Check if succeed to generate correct information in array reshape
  // PragmInst.
  EXPECT_EQ(APGI->getMode(), "complete");
  EXPECT_EQ(APGI->getDim(), 1);
  EXPECT_EQ(APGI->getFactor(), 0);
}

TEST_F(HLSIRBuilderTest, Dependence) {
  HLSIRBuilder IB(BB);

  // Check if succeed to create dependence PragmaInst on local alloca.
  Value *LDV = IB.CreateDependenceInst(AI, true, DependenceInst::DepType::INTER,
                                       DependenceInst::Direction::WAR, 1);
  EXPECT_TRUE(isa<DependenceInst>(LDV));

  // Check if succeed to get the exact same PragmInst from local alloca.
  DependenceInst *LDI = DependenceInst::get(AI);
  EXPECT_EQ(LDI, LDV);

  // Check if succeed to generate correct information in dependence
  // PragmInst.
  EXPECT_TRUE(LDI->isEnforced());
  EXPECT_EQ(LDI->getType(), DependenceInst::DepType::INTER);
  EXPECT_EQ(LDI->getDirection(), DependenceInst::Direction::WAR);
  EXPECT_EQ(LDI->getDistance(), 1);

  // Check if succeed to create dependence PragmaInst on global variable.
  Value *GDV = IB.CreateDependenceInst(GV, false,
                                       DependenceInst::DepType::INTRA);
  EXPECT_TRUE(isa<DependenceInst>(GDV));

  // Check if succeed to get the exact same PragmInst from global variable.
  DependenceInst *GDI = DependenceInst::get(GV);
  EXPECT_EQ(GDI, GDV);

  // Check if succeed to generate correct information in dependence
  // PragmInst.
  EXPECT_FALSE(GDI->isEnforced());
  EXPECT_EQ(GDI->getType(), DependenceInst::DepType::INTRA);
  EXPECT_EQ(GDI->getDirection(), DependenceInst::Direction::NODIR);
  EXPECT_EQ(GDI->getDistance(), 0);
}

TEST_F(HLSIRBuilderTest, Aggregate) {
  HLSIRBuilder IB(BB);

  // Check if succeed to create aggregate PragmaInst on local alloca.
  Value *LAV = IB.CreateAggregateInst(AI);
  EXPECT_TRUE(isa<AggregateInst>(LAV));

  // Check if succeed to get the exact same PragmInst from local alloca.
  AggregateInst *LAI = AggregateInst::get(AI);
  EXPECT_EQ(LAI, LAV);

  // Check if succeed to create aggregate PragmaInst on global variable.
  Value *GAV = IB.CreateAggregateInst(GV);
  EXPECT_TRUE(isa<AggregateInst>(GAV));

  // Check if succeed to get the exact same PragmInst from global variable.
  AggregateInst *GAI = AggregateInst::get(GV);
  EXPECT_EQ(GAI, GAV);
}

TEST_F(HLSIRBuilderTest, DisAggregate) {
  HLSIRBuilder IB(BB);

  // Check if succeed to create dis-aggregate PragmaInst on local alloca.
  Value *LDAV = IB.CreateDisaggregateInst(AI);
  EXPECT_TRUE(isa<DisaggrInst>(LDAV));

  // Check if succeed to get the exact same PragmInst from local alloca.
  DisaggrInst *LDAI = DisaggrInst::get(AI);
  EXPECT_EQ(LDAI, LDAV);

  // Check if succeed to create dis-aggregate PragmaInst on global variable.
  Value *GDAV = IB.CreateDisaggregateInst(GV);
  EXPECT_TRUE(isa<DisaggrInst>(GDAV));

  // Check if succeed to get the exact same PragmInst from global variable.
  DisaggrInst *GDAI = DisaggrInst::get(GV);
  EXPECT_EQ(GDAI, GDAV);
}
