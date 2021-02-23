//===- ValueTrackingTest.cpp - ValueTracking tests ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// And has the following additional copyright:
//
// (C) Copyright 2016-2020 Xilinx, Inc.
// All Rights Reserved.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/KnownBits.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace {

static Instruction &findInstructionByName(Function *F, StringRef Name) {
  for (Instruction &I : instructions(F))
    if (I.getName() == Name)
      return I;

  llvm_unreachable("Expected value not found");
}

class ValueTrackingTest : public testing::Test {
protected:
  std::unique_ptr<Module> parseModule(StringRef Assembly) {
    SMDiagnostic Error;
    std::unique_ptr<Module> M = parseAssemblyString(Assembly, Error, Context);

    std::string errMsg;
    raw_string_ostream os(errMsg);
    Error.print("", os);
    EXPECT_TRUE(M) << os.str();

    return M;
  }

  void parseAssembly(StringRef Assembly) {
    M = parseModule(Assembly);
    ASSERT_TRUE(M);

    F = M->getFunction("test");
    ASSERT_TRUE(F) << "Test must have a function @test";
    if (!F)
      return;

    A = &findInstructionByName(F, "A");
    ASSERT_TRUE(A) << "@test must have an instruction %A";
  }

  LLVMContext Context;
  std::unique_ptr<Module> M;
  Function *F = nullptr;
  Instruction *A = nullptr;
};

class MatchSelectPatternTest : public ValueTrackingTest {
protected:
  void expectPattern(const SelectPatternResult &P) {
    Value *LHS, *RHS;
    Instruction::CastOps CastOp;
    SelectPatternResult R = matchSelectPattern(A, LHS, RHS, &CastOp);
    EXPECT_EQ(P.Flavor, R.Flavor);
    EXPECT_EQ(P.NaNBehavior, R.NaNBehavior);
    EXPECT_EQ(P.Ordered, R.Ordered);
  }
};

}

TEST_F(MatchSelectPatternTest, SimpleFMin) {
  parseAssembly(
      "define float @test(float %a) {\n"
      "  %1 = fcmp ult float %a, 5.0\n"
      "  %A = select i1 %1, float %a, float 5.0\n"
      "  ret float %A\n"
      "}\n");
  expectPattern({SPF_FMINNUM, SPNB_RETURNS_NAN, false});
}

TEST_F(MatchSelectPatternTest, SimpleFMax) {
  parseAssembly(
      "define float @test(float %a) {\n"
      "  %1 = fcmp ogt float %a, 5.0\n"
      "  %A = select i1 %1, float %a, float 5.0\n"
      "  ret float %A\n"
      "}\n");
  expectPattern({SPF_FMAXNUM, SPNB_RETURNS_OTHER, true});
}

TEST_F(MatchSelectPatternTest, SwappedFMax) {
  parseAssembly(
      "define float @test(float %a) {\n"
      "  %1 = fcmp olt float 5.0, %a\n"
      "  %A = select i1 %1, float %a, float 5.0\n"
      "  ret float %A\n"
      "}\n");
  expectPattern({SPF_FMAXNUM, SPNB_RETURNS_OTHER, false});
}

TEST_F(MatchSelectPatternTest, SwappedFMax2) {
  parseAssembly(
      "define float @test(float %a) {\n"
      "  %1 = fcmp olt float %a, 5.0\n"
      "  %A = select i1 %1, float 5.0, float %a\n"
      "  ret float %A\n"
      "}\n");
  expectPattern({SPF_FMAXNUM, SPNB_RETURNS_NAN, false});
}

TEST_F(MatchSelectPatternTest, SwappedFMax3) {
  parseAssembly(
      "define float @test(float %a) {\n"
      "  %1 = fcmp ult float %a, 5.0\n"
      "  %A = select i1 %1, float 5.0, float %a\n"
      "  ret float %A\n"
      "}\n");
  expectPattern({SPF_FMAXNUM, SPNB_RETURNS_OTHER, true});
}

TEST_F(MatchSelectPatternTest, FastFMin) {
  parseAssembly(
      "define float @test(float %a) {\n"
      "  %1 = fcmp nnan olt float %a, 5.0\n"
      "  %A = select i1 %1, float %a, float 5.0\n"
      "  ret float %A\n"
      "}\n");
  expectPattern({SPF_FMINNUM, SPNB_RETURNS_ANY, false});
}

TEST_F(MatchSelectPatternTest, FMinConstantZero) {
  parseAssembly(
      "define float @test(float %a) {\n"
      "  %1 = fcmp ole float %a, 0.0\n"
      "  %A = select i1 %1, float %a, float 0.0\n"
      "  ret float %A\n"
      "}\n");
  // This shouldn't be matched, as %a could be -0.0.
  expectPattern({SPF_UNKNOWN, SPNB_NA, false});
}

TEST_F(MatchSelectPatternTest, FMinConstantZeroNsz) {
  parseAssembly(
      "define float @test(float %a) {\n"
      "  %1 = fcmp nsz ole float %a, 0.0\n"
      "  %A = select i1 %1, float %a, float 0.0\n"
      "  ret float %A\n"
      "}\n");
  // But this should be, because we've ignored signed zeroes.
  expectPattern({SPF_FMINNUM, SPNB_RETURNS_OTHER, true});
}

TEST_F(MatchSelectPatternTest, DoubleCastU) {
  parseAssembly(
      "define i32 @test(i8 %a, i8 %b) {\n"
      "  %1 = icmp ult i8 %a, %b\n"
      "  %2 = zext i8 %a to i32\n"
      "  %3 = zext i8 %b to i32\n"
      "  %A = select i1 %1, i32 %2, i32 %3\n"
      "  ret i32 %A\n"
      "}\n");
  // We should be able to look through the situation where we cast both operands
  // to the select.
  expectPattern({SPF_UMIN, SPNB_NA, false});
}

TEST_F(MatchSelectPatternTest, DoubleCastS) {
  parseAssembly(
      "define i32 @test(i8 %a, i8 %b) {\n"
      "  %1 = icmp slt i8 %a, %b\n"
      "  %2 = sext i8 %a to i32\n"
      "  %3 = sext i8 %b to i32\n"
      "  %A = select i1 %1, i32 %2, i32 %3\n"
      "  ret i32 %A\n"
      "}\n");
  // We should be able to look through the situation where we cast both operands
  // to the select.
  expectPattern({SPF_SMIN, SPNB_NA, false});
}

TEST_F(MatchSelectPatternTest, DoubleCastBad) {
  parseAssembly(
      "define i32 @test(i8 %a, i8 %b) {\n"
      "  %1 = icmp ult i8 %a, %b\n"
      "  %2 = zext i8 %a to i32\n"
      "  %3 = sext i8 %b to i32\n"
      "  %A = select i1 %1, i32 %2, i32 %3\n"
      "  ret i32 %A\n"
      "}\n");
  // The cast types here aren't the same, so we cannot match an UMIN.
  expectPattern({SPF_UNKNOWN, SPNB_NA, false});
}

TEST(ValueTracking, GuaranteedToTransferExecutionToSuccessor) {
  StringRef Assembly =
      "declare void @nounwind_readonly(i32*) nounwind readonly "
      "declare void @nounwind_argmemonly(i32*) nounwind argmemonly "
      "declare void @throws_but_readonly(i32*) readonly "
      "declare void @throws_but_argmemonly(i32*) argmemonly "
      " "
      "declare void @unknown(i32*) "
      " "
      "define void @f(i32* %p) { "
      "  call void @nounwind_readonly(i32* %p) "
      "  call void @nounwind_argmemonly(i32* %p) "
      "  call void @throws_but_readonly(i32* %p) "
      "  call void @throws_but_argmemonly(i32* %p) "
      "  call void @unknown(i32* %p) nounwind readonly "
      "  call void @unknown(i32* %p) nounwind argmemonly "
      "  call void @unknown(i32* %p) readonly "
      "  call void @unknown(i32* %p) argmemonly "
      "  ret void "
      "} ";

  LLVMContext Context;
  SMDiagnostic Error;
  auto M = parseAssemblyString(Assembly, Error, Context);
  assert(M && "Bad assembly?");

  auto *F = M->getFunction("f");
  assert(F && "Bad assembly?");

  auto &BB = F->getEntryBlock();
  bool ExpectedAnswers[] = {
      true,  // call void @nounwind_readonly(i32* %p)
      true,  // call void @nounwind_argmemonly(i32* %p)
      false, // call void @throws_but_readonly(i32* %p)
      false, // call void @throws_but_argmemonly(i32* %p)
      true,  // call void @unknown(i32* %p) nounwind readonly
      true,  // call void @unknown(i32* %p) nounwind argmemonly
      false, // call void @unknown(i32* %p) readonly
      false, // call void @unknown(i32* %p) argmemonly
      false, // ret void
  };

  int Index = 0;
  for (auto &I : BB) {
    EXPECT_EQ(isGuaranteedToTransferExecutionToSuccessor(&I),
              ExpectedAnswers[Index])
        << "Incorrect answer at instruction " << Index << " = " << I;
    Index++;
  }
}

TEST(ValueTracking, ComputeNumSignBits_PR32045) {
  StringRef Assembly = "define i32 @f(i32 %a) { "
                       "  %val = ashr i32 %a, -1 "
                       "  ret i32 %val "
                       "} ";

  LLVMContext Context;
  SMDiagnostic Error;
  auto M = parseAssemblyString(Assembly, Error, Context);
  assert(M && "Bad assembly?");

  auto *F = M->getFunction("f");
  assert(F && "Bad assembly?");

  auto *RVal =
      cast<ReturnInst>(F->getEntryBlock().getTerminator())->getOperand(0);
  EXPECT_EQ(ComputeNumSignBits(RVal, M->getDataLayout()), 1u);
}

TEST(ValueTracking, ComputeKnownBits) {
  StringRef Assembly = "define i32 @f(i32 %a, i32 %b) { "
                       "  %ash = mul i32 %a, 8 "
                       "  %aad = add i32 %ash, 7 "
                       "  %aan = and i32 %aad, 4095 "
                       "  %bsh = shl i32 %b, 4 "
                       "  %bad = or i32 %bsh, 6 "
                       "  %ban = and i32 %bad, 4095 "
                       "  %mul = mul i32 %aan, %ban "
                       "  ret i32 %mul "
                       "} ";

  LLVMContext Context;
  SMDiagnostic Error;
  auto M = parseAssemblyString(Assembly, Error, Context);
  assert(M && "Bad assembly?");

  auto *F = M->getFunction("f");
  assert(F && "Bad assembly?");

  auto *RVal =
      cast<ReturnInst>(F->getEntryBlock().getTerminator())->getOperand(0);
  auto Known = computeKnownBits(RVal, M->getDataLayout());
  ASSERT_FALSE(Known.hasConflict());
  EXPECT_EQ(Known.One.getZExtValue(), 10u);
  EXPECT_EQ(Known.Zero.getZExtValue(), 4278190085u);
}

TEST(ValueTracking, ComputeKnownMulBits) {
  StringRef Assembly = "define i32 @f(i32 %a, i32 %b) { "
                       "  %aa = shl i32 %a, 5 "
                       "  %bb = shl i32 %b, 5 "
                       "  %aaa = or i32 %aa, 24 "
                       "  %bbb = or i32 %bb, 28 "
                       "  %mul = mul i32 %aaa, %bbb "
                       "  ret i32 %mul "
                       "} ";

  LLVMContext Context;
  SMDiagnostic Error;
  auto M = parseAssemblyString(Assembly, Error, Context);
  assert(M && "Bad assembly?");

  auto *F = M->getFunction("f");
  assert(F && "Bad assembly?");

  auto *RVal =
      cast<ReturnInst>(F->getEntryBlock().getTerminator())->getOperand(0);
  auto Known = computeKnownBits(RVal, M->getDataLayout());
  ASSERT_FALSE(Known.hasConflict());
  EXPECT_EQ(Known.One.getZExtValue(), 32u);
  EXPECT_EQ(Known.Zero.getZExtValue(), 95u);
}

TEST_F(ValueTrackingTest, ComputeKnownBitsPtrToIntTrunc) {
  // ptrtoint truncates the pointer type.
  parseAssembly(
      "define void @test(i8** %p) {\n"
      "  %A = load i8*, i8** %p\n"
      "  %i = ptrtoint i8* %A to i32\n"
      "  %m = and i32 %i, 31\n"
      "  %c = icmp eq i32 %m, 0\n"
      "  call void @llvm.assume(i1 %c)\n"
      "  ret void\n"
      "}\n"
      "declare void @llvm.assume(i1)\n");
  AssumptionCache AC(*F);
  KnownBits Known = computeKnownBits(
      A, M->getDataLayout(), /* Depth */ 0, &AC, F->front().getTerminator());
  EXPECT_EQ(Known.Zero.getZExtValue(), 31u);
  EXPECT_EQ(Known.One.getZExtValue(), 0u);
}

TEST_F(ValueTrackingTest, ComputeKnownBitsPtrToIntZext) {
  // ptrtoint zero extends the pointer type.
  parseAssembly(
      "define void @test(i8** %p) {\n"
      "  %A = load i8*, i8** %p\n"
      "  %i = ptrtoint i8* %A to i128\n"
      "  %m = and i128 %i, 31\n"
      "  %c = icmp eq i128 %m, 0\n"
      "  call void @llvm.assume(i1 %c)\n"
      "  ret void\n"
      "}\n"
      "declare void @llvm.assume(i1)\n");
  AssumptionCache AC(*F);
  KnownBits Known = computeKnownBits(
      A, M->getDataLayout(), /* Depth */ 0, &AC, F->front().getTerminator());
  EXPECT_EQ(Known.Zero.getZExtValue(), 31u);
  EXPECT_EQ(Known.One.getZExtValue(), 0u);
}

TEST_F(ValueTrackingTest, ComputeKnownBitsAssumeOnArg) {
  parseAssembly(
      "define void @test(i64 %sqrt_size) {\n"
      "  %rem = urem i64 %sqrt_size, 8"
      "  %c = icmp eq i64 %rem, 0\n"
      "  call void @llvm.assume(i1 %c)\n"
      "  %A = mul i64 %sqrt_size, %sqrt_size"
      "  ret void\n"
      "}\n"
      "declare void @llvm.assume(i1)\n");
  AssumptionCache AC(*F);
  KnownBits Known = computeKnownBits(
      A, M->getDataLayout(), /* Depth */ 0, &AC, F->front().getTerminator());
  EXPECT_EQ(Known.Zero.getZExtValue(), 63u);
  EXPECT_EQ(Known.One.getZExtValue(), 0u);
}

TEST_F(ValueTrackingTest, ComputeConstantRange) {
  {
    // Assumptions:
    //  * stride >= 5
    //  * stride < 10
    //
    // stride = [5, 10)
    auto M = parseModule(R"(
  declare void @llvm.assume(i1)

  define i32 @test(i32 %stride) {
    %gt = icmp uge i32 %stride, 5
    call void @llvm.assume(i1 %gt)
    %lt = icmp ult i32 %stride, 10
    call void @llvm.assume(i1 %lt)
    %stride.plus.one = add nsw nuw i32 %stride, 1
    ret i32 %stride.plus.one
  })");
    Function *F = M->getFunction("test");

    AssumptionCache AC(*F);
    Value *Stride = &*F->arg_begin();
    ConstantRange CR1 = computeConstantRange(Stride, &AC, nullptr);
    EXPECT_TRUE(CR1.isFullSet());

    Instruction *I = &findInstructionByName(F, "stride.plus.one");
    ConstantRange CR2 = computeConstantRange(Stride, &AC, I);
    EXPECT_EQ(5, CR2.getLower());
    EXPECT_EQ(10, CR2.getUpper());
  }

  {
    // Assumptions:
    //  * stride >= 5
    //  * stride < 200
    //  * stride == 99
    //
    // stride = [99, 100)
    auto M = parseModule(R"(
  declare void @llvm.assume(i1)

  define i32 @test(i32 %stride) {
    %gt = icmp uge i32 %stride, 5
    call void @llvm.assume(i1 %gt)
    %lt = icmp ult i32 %stride, 200
    call void @llvm.assume(i1 %lt)
    %eq = icmp eq i32 %stride, 99
    call void @llvm.assume(i1 %eq)
    %stride.plus.one = add nsw nuw i32 %stride, 1
    ret i32 %stride.plus.one
  })");
    Function *F = M->getFunction("test");

    AssumptionCache AC(*F);
    Value *Stride = &*F->arg_begin();
    Instruction *I = &findInstructionByName(F, "stride.plus.one");
    ConstantRange CR = computeConstantRange(Stride, &AC, I);
    EXPECT_EQ(99, *CR.getSingleElement());
  }

  {
    // Assumptions:
    //  * stride >= 5
    //  * stride >= 50
    //  * stride < 100
    //  * stride < 200
    //
    // stride = [50, 100)
    auto M = parseModule(R"(
  declare void @llvm.assume(i1)

  define i32 @test(i32 %stride, i1 %cond) {
    %gt = icmp uge i32 %stride, 5
    call void @llvm.assume(i1 %gt)
    %gt.2 = icmp uge i32 %stride, 50
    call void @llvm.assume(i1 %gt.2)
    br i1 %cond, label %bb1, label %bb2

  bb1:
    %lt = icmp ult i32 %stride, 200
    call void @llvm.assume(i1 %lt)
    %lt.2 = icmp ult i32 %stride, 100
    call void @llvm.assume(i1 %lt.2)
    %stride.plus.one = add nsw nuw i32 %stride, 1
    ret i32 %stride.plus.one

  bb2:
    ret i32 0
  })");
    Function *F = M->getFunction("test");

    AssumptionCache AC(*F);
    Value *Stride = &*F->arg_begin();
    Instruction *GT2 = &findInstructionByName(F, "gt.2");
    ConstantRange CR = computeConstantRange(Stride, &AC, GT2);
    EXPECT_EQ(5, CR.getLower());
    EXPECT_EQ(0, CR.getUpper());

    Instruction *I = &findInstructionByName(F, "stride.plus.one");
    ConstantRange CR2 = computeConstantRange(Stride, &AC, I);
    EXPECT_EQ(50, CR2.getLower());
    EXPECT_EQ(100, CR2.getUpper());
  }

  {
    // Assumptions:
    //  * stride > 5
    //  * stride < 5
    //
    // stride = empty range, as the assumptions contradict each other.
    auto M = parseModule(R"(
  declare void @llvm.assume(i1)

  define i32 @test(i32 %stride, i1 %cond) {
    %gt = icmp ugt i32 %stride, 5
    call void @llvm.assume(i1 %gt)
    %lt = icmp ult i32 %stride, 5
    call void @llvm.assume(i1 %lt)
    %stride.plus.one = add nsw nuw i32 %stride, 1
    ret i32 %stride.plus.one
  })");
    Function *F = M->getFunction("test");

    AssumptionCache AC(*F);
    Value *Stride = &*F->arg_begin();

    Instruction *I = &findInstructionByName(F, "stride.plus.one");
    ConstantRange CR = computeConstantRange(Stride, &AC, I);
    EXPECT_TRUE(CR.isEmptySet());
  }

  {
    // Assumptions:
    //  * x.1 >= 5
    //  * x.2 < x.1
    //
    // stride = [0, 5)
    auto M = parseModule(R"(
  declare void @llvm.assume(i1)

  define i32 @test(i32 %x.1, i32 %x.2) {
    %gt = icmp uge i32 %x.1, 5
    call void @llvm.assume(i1 %gt)
    %lt = icmp ult i32 %x.2, %x.1
    call void @llvm.assume(i1 %lt)
    %stride.plus.one = add nsw nuw i32 %x.1, 1
    ret i32 %stride.plus.one
  })");
    Function *F = M->getFunction("test");

    AssumptionCache AC(*F);
    Value *X2 = &*std::next(F->arg_begin());

    Instruction *I = &findInstructionByName(F, "stride.plus.one");
    ConstantRange CR1 = computeConstantRange(X2, &AC, I);
    EXPECT_EQ(0, CR1.getLower());
    EXPECT_EQ(5, CR1.getUpper());

    // Check the depth cutoff results in a conservative result (full set) by
    // passing Depth == MaxDepth == 6.
    ConstantRange CR2 = computeConstantRange(X2, &AC, I, 6);
    EXPECT_TRUE(CR2.isFullSet());
  }
}
