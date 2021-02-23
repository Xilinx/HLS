// (c) Copyright 2016-2020 Xilinx, Inc.
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
// Test the arbitrary-precision integer type in HLS extension.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendActions.h"
#include "gtest/gtest.h"

using namespace clang;
using namespace llvm;

struct TestAPIntTypeAction : public ASTFrontendAction {
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override {
    return llvm::make_unique<ASTConsumer>();
  }
};

TEST(APIntTypeTest, APIntTypeProperties) {
  CompilerInstance Compiler;
  Compiler.createDiagnostics();
  std::shared_ptr<CompilerInvocation> Invocation(new CompilerInvocation);
  const char *Args[] = {"-fhls"};
  CompilerInvocation::CreateFromArgs(*Invocation, Args,
                                     Args + array_lengthof(Args),
                                     Compiler.getDiagnostics());
  Compiler.setInvocation(Invocation);
  Compiler.setTarget(TargetInfo::CreateTargetInfo(Compiler.getDiagnostics(),
                                                  Invocation->TargetOpts));
  Compiler.createFileManager();
  Compiler.createSourceManager(Compiler.getFileManager());
  Compiler.createPreprocessor(TU_Complete);
  Compiler.createASTContext();
  auto &Context = Compiler.getASTContext();

  QualType Int3Ty = Context.getAPIntType(3, true),
           UInt15Ty = Context.getAPIntType(15, false);

  ASSERT_TRUE(Int3Ty->isAPIntType());
  ASSERT_TRUE(UInt15Ty->isAPIntType());
  EXPECT_EQ(UInt15Ty->getScalarTypeKind(), clang::Type::STK_Integral);
  ASSERT_TRUE(Int3Ty->isScalarType());
  ASSERT_TRUE(UInt15Ty->isScalarType());
  ASSERT_TRUE(Int3Ty->isIntegerType());
  ASSERT_TRUE(Int3Ty->isSignedIntegerType());
  ASSERT_FALSE(Int3Ty->isUnsignedIntegerType());
  ASSERT_TRUE(UInt15Ty->isUnsignedIntegerType());
  ASSERT_TRUE(Int3Ty->isIntegralOrEnumerationType());
  ASSERT_TRUE(Int3Ty->isIntegralOrUnscopedEnumerationType());
  ASSERT_TRUE(Int3Ty->hasIntegerRepresentation());
  ASSERT_TRUE(Int3Ty->hasSignedIntegerRepresentation());
  ASSERT_FALSE(Int3Ty->hasUnsignedIntegerRepresentation());
  ASSERT_TRUE(UInt15Ty->hasUnsignedIntegerRepresentation());

  QualType Int47Ty = Context.getAPIntType(47, true),
           Int1024Ty = Context.getAPIntType(1024, true);

  QualType UInt47Ty = Context.getCorrespondingUnsignedType(Int47Ty),
           UInt1024Ty = Context.getCorrespondingUnsignedType(Int1024Ty);
  ASSERT_TRUE(UInt47Ty->isAPIntType());
  ASSERT_TRUE(UInt1024Ty->isAPIntType());

  QualType Int15Ty = Context.getAPIntType(15, true),
           Int14Ty = Context.getAPIntType(14, true);

  // Integer conversion rank (C99 6.3.1.1p1)
  // 660 - No two signed integer types shall have the same rank,
  // even if they have the same representation.
  EXPECT_NE(Context.getIntegerTypeOrder(Int14Ty, Int15Ty), 0);
  // 661 - The rank of a signed integer type shall be greater than the rank of
  // any signed integer type with less precision.
  EXPECT_EQ(Context.getIntegerTypeOrder(Int15Ty, Int14Ty), 1);
  // 662 - The rank of long long int shall be greater than the rank of long
  // int, which shall be greater than the rank of int, which shall be greater
  // than the rank of short int, which shall be greater than the rank of signed
  // char.

  // 663 - The rank of any unsigned integer type shall equal the rank of the
  // corresponding signed integer type, if any.
  EXPECT_EQ(
      Context.getIntegerTypeOrder(Int47Ty, UInt47Ty),
      Context.getIntegerTypeOrder(
          Context.IntTy, Context.getCorrespondingUnsignedType(Context.IntTy)));

  // 664 - The rank of any standard integer type shall be greater than the
  // rank of any extended integer type with the same width.

  // 665 - The rank of char shall equal the rank of signed char and unsigned
  // char.

  // 666 - The rank of _Bool shall be less than the rank of all other standard
  // integer types.
  QualType Int1Ty = Context.getAPIntType(1, true);
  EXPECT_EQ(Context.getIntegerTypeOrder(Int1Ty, Context.BoolTy), -1);

  // 667 - The rank of any enumerated type shall equal the rank of the
  // compatible integer type(see 6.7.2.2).

  // 668 - The rank of any extended signed integer type relative to another
  // extended signed integer type with the same precision is implementation-
  // defined, but still subject to the other rules for determining the integer
  // conversion rank.

  // 669 - For all integer types T1, T2, and T3, if T1 has greater rank than T2
  // and T2 has greater rank than T3, then T1 has greater rank than T3.
  EXPECT_EQ(Context.getIntegerTypeOrder(Int47Ty, Int14Ty), 1);
  EXPECT_EQ(Context.getIntegerTypeOrder(Int14Ty, Int3Ty), 1);
  EXPECT_EQ(Context.getIntegerTypeOrder(Int47Ty, Int3Ty), 1);

  // 670 The following may be used in an expression wherever an int or unsigned
  // int may be used :

  // 671 - An object or expression with an integer type whose integer
  // conversion rank is less than or equal to the rank of int and unsigned int.

  // 672 - A bit - field of type _Bool, int, signed int, or unsigned int.

  // 673 If an int can represent all values of the original type, the value is
  // converted to an int;

  // 674 otherwise, it is converted to an unsigned int.

  // 675 These are called the integer promotions.48)

  // Do not promote APIntType to int or unsigned int.
  ASSERT_FALSE(Int3Ty->isPromotableIntegerType());
  ASSERT_FALSE(Int14Ty->isPromotableIntegerType());
  ASSERT_FALSE(Int15Ty->isPromotableIntegerType());
  ASSERT_FALSE(Int47Ty->isPromotableIntegerType());
  ASSERT_FALSE(UInt1024Ty->isPromotableIntegerType());

  // 676 All other types are unchanged by the integer promotions.

  // 677 The integer promotions preserve value including sign.

  // 678 As discussed earlier, whether a “plain” char is treated as signed is
  // implementation - defined.

  // 679 Forward references : enumeration specifiers(6.7.2.2), structure and
  // union specifiers(6.7.2.1).

  // Size of APInt
  EXPECT_EQ(Context.getTypeSizeInChars(Int3Ty), CharUnits::One());
  EXPECT_EQ(Context.getTypeSizeInChars(Int47Ty),
            Context.toCharUnitsFromBits(llvm::NextPowerOf2(47)));
  EXPECT_EQ(Context.getTypeSizeInChars(UInt15Ty),
            Context.toCharUnitsFromBits(llvm::NextPowerOf2(15)));
  EXPECT_EQ(Context.getTypeSizeInChars(Int1024Ty),
            Context.toCharUnitsFromBits(1024));

  // The width of an integer, as defined in C99 6.2.6.2. This is the number
  // of bits in an integer type excluding any padding bits.
  EXPECT_EQ(Context.getIntWidth(Int3Ty), 3u);
  EXPECT_EQ(Context.getIntWidth(Int47Ty), 47u);
  EXPECT_EQ(Context.getIntWidth(Int1024Ty), 1024u);

  EXPECT_EQ(Context.getAPIntType(8, true),
            Context.getIntTypeForBitwidth(8, true));
  EXPECT_EQ(Context.getAPIntType(16, false),
            Context.getIntTypeForBitwidth(16, false));
  EXPECT_EQ(Context.getAPIntType(32, true),
            Context.getIntTypeForBitwidth(32, true));
  EXPECT_EQ(Context.getAPIntType(64, false),
            Context.getIntTypeForBitwidth(64, false));

  // C99 6.2.5p17 (real floating + integer)
  // The type char, the signed and unsigned integer types, and the enumerated
  // types are collectively called integer types.The integer and real floating
  // types are collectively called real types
  EXPECT_TRUE(Int47Ty->isRealType());

  // APIntType is also a POD type
  EXPECT_TRUE(Int47Ty.isPODType(Context));

  // C99 6.3.1.1p2 int promoation will first promote to int, then unsigned int
  EXPECT_EQ(Context.getPromotedIntegerType(UInt15Ty), Context.IntTy);
}
