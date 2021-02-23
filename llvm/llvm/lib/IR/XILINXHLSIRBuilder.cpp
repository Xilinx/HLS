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
// This file implement the HLS IRBuilder
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/XILINXHLSIRBuilder.h"
#include "llvm/Analysis/XILINXHLSValueTrackingUtils.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/XILINXFPGAIntrinsicInst.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/XILINXAggregateUtil.h"

#define DEBUG_TYPE "xilinx-hls-ir-builder"

#include <cassert>
#include <numeric>

using namespace llvm;
using namespace PatternMatch;

static cl::opt<bool> OptimalByteenableGenerate(
    "xilinx-optimal-byte-enable-gen",
    cl::desc("Assume same byte enable mask within an alignment."), cl::Hidden,
    cl::init(false));

HLSIRBuilder::HLSIRBuilder(BasicBlock *TheBB, BasicBlock::iterator IP,
                           MDNode *FPMathTag,
                           ArrayRef<OperandBundleDef> OpBundles)
    : IRBuilder<>(TheBB, IP, FPMathTag, OpBundles),
      DL(TheBB->getModule()->getDataLayout()) {}

HLSIRBuilder::HLSIRBuilder(BasicBlock *TheBB, MDNode *FPMathTag,
                           ArrayRef<OperandBundleDef> OpBundles)
    : IRBuilder<>(TheBB, FPMathTag, OpBundles),
      DL(TheBB->getModule()->getDataLayout()) {}

HLSIRBuilder::HLSIRBuilder(LLVMContext &C, const DataLayout &DL,
                           MDNode *FPMathTag,
                           ArrayRef<OperandBundleDef> OpBundles)
    : IRBuilder<>(C, FPMathTag, OpBundles), DL(DL) {}

HLSIRBuilder::HLSIRBuilder(Instruction *IP, MDNode *FPMathTag,
                           ArrayRef<OperandBundleDef> OPBundles)
    : IRBuilder<>(IP, FPMathTag, OPBundles),
      DL(IP->getModule()->getDataLayout()) {}

//===----------------------------------------------------------------------===//
Value *HLSIRBuilder::CreateMul(Value *LHS, APInt RHS, const Twine &Name) {
  if (RHS.isOneValue())
    return LHS;

  // If this is a multiply by a power of two, turn it into a shl
  // immediately.  This is a very common case.
  if (RHS.isPowerOf2()) {
    APInt Amt(RHS.getBitWidth(), RHS.logBase2());
    return CreateShl(LHS, ConstantInt::get(Context, Amt), Name);
  }

  return CreateMul(LHS, ConstantInt::get(Context, RHS), Name);
}

Value *HLSIRBuilder::CreateUDiv(Value *LHS, APInt RHS, const Twine &Name,
                                bool isExact) {
  if (RHS.isOneValue())
    return LHS;

  if (RHS.isPowerOf2()) {
    APInt Amt(RHS.getBitWidth(), RHS.logBase2());
    return CreateLShr(LHS, ConstantInt::get(Context, Amt), Name, isExact);
  }

  return CreateUDiv(LHS, ConstantInt::get(Context, RHS), Name, isExact);
}

Value *HLSIRBuilder::CreateURem(Value *LHS, APInt RHS, const Twine &Name) {
  if (RHS.isOneValue())
    return ConstantInt::get(Context, RHS);

  if (RHS.isPowerOf2()) {
    APInt Amt = APInt::getLowBitsSet(RHS.getBitWidth(), RHS.logBase2());
    return CreateAnd(LHS, ConstantInt::get(Context, Amt), Name);
  }

  return CreateURem(LHS, ConstantInt::get(Context, RHS), Name);
}

Value *HLSIRBuilder::CreateSDiv(Value *LHS, APInt RHS, const Twine &Name,
                                bool isExact) {
  if (RHS.isOneValue())
    return LHS;

  return CreateSDiv(LHS, ConstantInt::get(Context, RHS), Name, isExact);
}

//===----------------------------------------------------------------------===//
/// Return true if \p T is hls-ir lowered packed struct type: <{ [P * iQ] }>
bool HLSIRBuilder::isLoweredPackedStructTy(Type *T) {
  auto *ST = dyn_cast<StructType>(T);
  if (ST && ST->isPacked() && (ST->getNumElements() == 1))
    if (auto *AT = dyn_cast<ArrayType>(ST->getElementType(0)))
      if (isa<IntegerType>(AT->getElementType()))
        return true;

  return false;
}

//===----------------------------------------------------------------------===//
static Type *CalculateMiddleTypeForVector(const DataLayout &DL, Type *SrcTy,
                                          Type *DstTy) {
  assert(SrcTy != DstTy && "Unexpected same types!");
  unsigned SizeInBits = DL.getTypeSizeInBits(SrcTy);
  assert(SizeInBits == DL.getTypeSizeInBits(DstTy) &&
         "Cannot create middle type with different size!");

  // Direct bitcast is possible between non-pointer type with the same size
  if (SrcTy->getScalarType()->isPointerTy() ==
      DstTy->getScalarType()->isPointerTy())
    return nullptr;

  VectorType *SrcVecTy = dyn_cast<VectorType>(SrcTy);
  VectorType *DstVecTy = dyn_cast<VectorType>(DstTy);

  // Casting scalar types do not need middle type
  if (!SrcVecTy && !DstVecTy)
    return nullptr;

  // Casting between elements are possible.
  if (SrcVecTy && DstVecTy &&
      SrcVecTy->getNumElements() == DstVecTy->getNumElements())
    return nullptr;

  unsigned PtrSizeInBits = DL.getPointerSizeInBits();
  assert(SizeInBits % PtrSizeInBits == 0 &&
         "Cannot do pointer cast with different size!");
  unsigned NumElements = SizeInBits / PtrSizeInBits;
  auto *IntPtrTy = IntegerType::get(SrcTy->getContext(), PtrSizeInBits);

  if (NumElements == 1)
    return IntPtrTy;

  return VectorType::get(IntPtrTy, NumElements);
}

Value *HLSIRBuilder::twoStepsBitCast(Value *V, Type *DstTy) {
  auto *SrcTy = V->getType();

  if (SrcTy == DstTy)
    return V;

  if (auto *C = dyn_cast<Constant>(V)) {
    auto *RC = rewriteConstant(C, DstTy);

    // If the rewritten constant is integer but the DstTy is packed struct, cast
    // the constant to pack struct type
    if (isLoweredPackedStructTy(DstTy) && RC->getType()->isIntegerTy())
      return CreateUnpackNone(RC, DstTy);
    return RC;
  }

  if (SrcTy->isIntegerTy() && DstTy->isAggregateType())
    return CreateUnpackNone(V, DstTy);

  if (SrcTy->isAggregateType() && DstTy->isIntegerTy())
    return CreatePackNone(V, cast<IntegerType>(DstTy));

  if (SrcTy->isIntegerTy() && DstTy->isIntegerTy())
    return CreateZExtOrTrunc(V, DstTy);

  // SrcTy is packed struct type and DstTy is aggregate type
  if (isLoweredPackedStructTy(SrcTy) && DstTy->isAggregateType()) {
    IntegerType *IntTy =
        IntegerType::get(SrcTy->getContext(), DL.getTypeSizeInBits(SrcTy));
    auto *PackV = CreatePackNone(V, IntTy);
    return twoStepsBitCast(PackV, DstTy);
  }

  // SrcTy is aggregate type and DstTy is packed struct type
  if (SrcTy->isAggregateType() && isLoweredPackedStructTy(DstTy)) {
    IntegerType *IntTy =
        IntegerType::get(DstTy->getContext(), DL.getTypeSizeInBits(DstTy));
    auto *PackV = CreatePackNone(V, IntTy);
    return twoStepsBitCast(PackV, DstTy);
  }

  if (auto *MidTy = CalculateMiddleTypeForVector(getDataLayout(), SrcTy, DstTy))
    return twoStepsBitCast(twoStepsBitCast(V, MidTy), DstTy);

  return CreateBitOrPointerCast(V, DstTy);
}

Value *HLSIRBuilder::twoStepsBitCast(Value *V) {
  if (isa<IntegerType>(V->getType()))
    return V;

  unsigned SizeInBits = DL.getTypeSizeInBits(StripPadding(V->getType(), DL));
  return twoStepsBitCast(V, getIntNTy(SizeInBits));
}

//===----------------------------------------------------------------------===//
/// Rewrite the global variable initializer for StructType \pT. \pT is a packed
/// struct: <{ [P * iQ] }>
void HLSIRBuilder::rewriteConstants(SmallVectorImpl<Constant *> &Constants,
                                    SmallVectorImpl<char> &Bytes,
                                    StructType *T) {

  assert(!Bytes.empty() && "No constants to rewrite!");
  assert(isLoweredPackedStructTy(T) && "Expect lowered packed struct type!");
  auto &DL = getDataLayout();
  auto DstSizeInBytes = DL.getTypeSizeInBits(T) / 8; /* (P*Q/8) */
  auto *AT = cast<ArrayType>(T->getElementType(0));
  auto *ElemTy = dyn_cast<IntegerType>(AT->getElementType());

  auto ElemSizeInBits = ElemTy->getBitWidth();
  auto ElemSizeInBytes = ElemSizeInBits / 8;
  auto ElemSizeInInt64 = divideCeil(ElemSizeInBytes, 64 / 8);

  // Process P*Q bits each time
  for (unsigned i = 0, e = Bytes.size(); i < e; i += DstSizeInBytes) {
    // Only 1 member(an array) in the packed struct type T
    SmallVector<Constant *, 1> Data;

    // Elts.size() should be P. Type of each elements in Elts is iQ
    SmallVector<Constant *, 128> Elts;

    // Copy Q bits from Bytes
    SmallVector<uint64_t, 4> DataEachQ(ElemSizeInInt64, 0);
    for (unsigned j = 0; j < DstSizeInBytes; j += ElemSizeInBytes) {
      std::memcpy(DataEachQ.data(), Bytes.data() + i + j, ElemSizeInBytes);
      auto *Elt =
          ConstantInt::get(getContext(), APInt(ElemSizeInBits, DataEachQ));
      Elts.push_back(Elt);
    }

    // Construct <{ [P * iQ] }>
    Data.push_back(ConstantArray::get(AT, Elts));
    Constants.push_back(ConstantStruct::get(T, Data));
  }
}

void HLSIRBuilder::rewriteConstants(SmallVectorImpl<Constant *> &Constants,
                                    SmallVectorImpl<char> &Bytes,
                                    IntegerType *T) {
  auto &DL = getDataLayout();
  auto DstSizeInBytes = DL.getTypeAllocSize(T);
  assert(!Bytes.empty() && "No constants to rewrite!");

  for (unsigned i = 0, e = Bytes.size(); i < e; i += DstSizeInBytes) {
    auto DstSizeInInt64 = divideCeil(DstSizeInBytes, 64 / 8);
    SmallVector<uint64_t, 4> Data(DstSizeInInt64, 0);
    std::memcpy(Data.data(), Bytes.data() + i,
                i + DstSizeInBytes <= e ? DstSizeInBytes : e - i);
    auto *Elt = ConstantInt::get(getContext(), APInt(T->getBitWidth(), Data));
    Constants.push_back(Elt);
  }
}

Constant *HLSIRBuilder::rewriteConstant(Constant *C, Type *T) {
  auto *CT = C->getType();

  DEBUG(dbgs() << "rewriteConstant(" << *C << ", " << *T << ")\n");

  if (CT == T)
    return C;

  if (isa<UndefValue>(C))
    return UndefValue::get(T);

  if (isa<ConstantAggregateZero>(C))
    return Constant::getNullValue(T);

  if (isa<ConstantPointerNull>(C))
    return Constant::getNullValue(T);

  if (CT->isIntegerTy() && T->isIntegerTy())
    return cast<Constant>(CreateZExtOrTrunc(C, T));

  SmallVector<Constant *, 16> Elts;
  if (T->isIntegerTy() && FillBytes(C, Elts))
    return cast<Constant>(CreateZExtOrTrunc(GetherElements(Elts), T));

  if (auto *ST = dyn_cast<StructType>(T))
    if (ST->isPacked()) {
      // First, treat packed struct type as an integer type to rewrite the
      // constant. Then, unpack the value back to a packed struct type.
      IntegerType *IntTy =
          IntegerType::get(T->getContext(), DL.getTypeSizeInBits(ST));
      return rewriteConstant(C, IntTy);
    }

  if (T->isArrayTy())
    llvm_unreachable("Cannot rewrite array!");

  return cast<Constant>(CreateBitOrPointerCast(C, T));
}

//===----------------------------------------------------------------------===//
Value *HLSIRBuilder::GenerateMask(Value *Hi, Value *Lo) {
  auto *Ty = cast<IntegerType>(Hi->getType());
  auto *SizeInBits = ConstantInt::get(Ty, Ty->getBitWidth());
  auto *One = ConstantInt::get(Ty, 1);
  auto *HiPlusOne = CreateAdd(Hi, ConstantInt::get(Ty, 1), "", true, true);
  auto *Mask =
      CreateSelect(CreateICmpUGE(HiPlusOne, SizeInBits),
                   ConstantInt::getNullValue(Ty), CreateShl(One, HiPlusOne));
  return CreateSub(Mask, CreateShl(One, Lo));
}

Value *HLSIRBuilder::GeneratePartSelect(Value *V, Value *Lo, Value *Hi,
                                        IntegerType *RetTy) {
  auto *Result = CreateLShr(
      CreateAnd(V, GenerateMask(CreateZExtOrTrunc(Hi, V->getType()),
                                CreateZExtOrTrunc(Lo, V->getType()))),
      CreateZExtOrTrunc(Lo, V->getType()));
  assert(Result->getType()->getIntegerBitWidth() >= RetTy->getBitWidth() &&
         "Bad return type!");
  if (Result->getType() != RetTy)
    Result = CreateTrunc(Result, RetTy);
  return Result;
}

unsigned HLSIRBuilder::getVectorTypeNumElts(VectorType *VT,
                                            unsigned EltSizeInBits) const {
  if (DL.getTypeSizeInBits(VT->getElementType()) == EltSizeInBits)
    return VT->getNumElements();

  return 0;
}

unsigned HLSIRBuilder::getConstantVectorNumElts(Value *V,
                                                unsigned EltSizeInBits) const {
  if (auto *CV = dyn_cast<ConstantVector>(V))
    return getVectorTypeNumElts(CV->getType(), EltSizeInBits);

  if (auto *CV = dyn_cast<ConstantDataVector>(V))
    return getVectorTypeNumElts(CV->getType(), EltSizeInBits);

  return 0;
}

Value *HLSIRBuilder::CreatePartSelect(Value *V, uint32_t Lo, uint32_t Hi) {
  uint32_t DstSizeInBits = Hi - Lo + 1;

  if (isa<UndefValue>(V))
    return UndefValue::get(getIntNTy(DstSizeInBits));

  // FIXME: Move this to reflow instruction simplifier
  if (auto *IT = dyn_cast<IntegerType>(V->getType())) {
    if (Lo == 0)
      return CreateZExtOrTrunc(V, getIntNTy(DstSizeInBits));

    if (auto *ZE = dyn_cast<ZExtInst>(V)) {
      auto SrcSizeInBits = ZE->getSrcTy()->getIntegerBitWidth();
      if (SrcSizeInBits <= Lo)
        return getIntN(DstSizeInBits, 0);

      if (SrcSizeInBits - 1 >= Hi)
        return CreatePartSelect(ZE->getOperand(0), Lo, Hi);
    }

    Value *Data;
    uint64_t Amt;
    if (match(V, m_Shl(m_Value(Data), m_ConstantInt(Amt)))) {
      if (Lo > Amt)
        return CreatePartSelect(Data, Lo - Amt, Hi - Amt);
    }

    // Try to extract element from bitconcat.
    if (auto *BitConcat = dyn_cast<BitConcatInst>(V))
      if (auto *Elt = BitConcat->getElement(Hi, Lo))
        return Elt;
  }

  // Ask the constant folder to extract element from vector
  unsigned NumElts = getConstantVectorNumElts(V, DstSizeInBits);
  unsigned Idx = Lo / DstSizeInBits;
  if (NumElts > Idx && Lo % DstSizeInBits == 0)
    return CreateExtractElement(V, Lo / DstSizeInBits);

  auto *SrcTy = getIntNTy(DL.getTypeSizeInBits(V->getType()));
  return CreatePartSelectCall(V, ConstantInt::get(SrcTy, Lo),
                              ConstantInt::get(SrcTy, Hi),
                              getIntNTy(DstSizeInBits));
}

Value *HLSIRBuilder::CreatePartSelect(Value *V, Value *Lo, Value *Hi,
                                      IntegerType *RetTy) {

  if (isa<UndefValue>(V))
    return UndefValue::get(RetTy);

  // Do constant folding whenever possible
  auto *IntHi = dyn_cast<ConstantInt>(Hi);
  auto *IntLo = dyn_cast<ConstantInt>(Lo);
  if (IntHi && IntLo) {
    if (isa<ConstantInt>(V))
      return cast<ConstantInt>(GeneratePartSelect(V, Lo, Hi, RetTy));
    assert(IntHi->getZExtValue() >= IntLo->getZExtValue() && "Bad range!");
    return CreatePartSelect(V, IntLo->getZExtValue(), IntHi->getZExtValue());
  }

  return CreatePartSelectCall(V, Lo, Hi, RetTy);
}

/// Generate the fpga.part.select call without any optimizations
Value *HLSIRBuilder::CreatePartSelectCall(Value *V, Value *Lo, Value *Hi,
                                          IntegerType *RetTy) {
  V = twoStepsBitCast(V);
  auto *SrcTy = cast<IntegerType>(V->getType());
  Type *ArgTys[] = {RetTy, SrcTy};
  auto *PartSelect = Intrinsic::getDeclaration(
      getModule(), Intrinsic::fpga_part_select, ArgTys);

  Value *Args[] = {V, Lo, Hi};
  return CreateCall(PartSelect, Args);
}

/// Generate the fpga.legacy.part.select call without any optimizations
Value *HLSIRBuilder::CreateLegacyPartSelectCall(Value *V, Value *Lo,
                                                Value *Hi) {
  IntegerType *ValTy = dyn_cast<llvm::IntegerType>(V->getType());
  assert(ValTy && "Only accept IntegerType value to do part select!");

  IntegerType *LoTy = dyn_cast<llvm::IntegerType>(Lo->getType());
  assert(LoTy && "Lo should be IntegerType!");
  IntegerType *HiTy = dyn_cast<llvm::IntegerType>(Hi->getType());
  assert(HiTy && "Hi should be IntegerType!");
  if (LoTy != Type::getInt32Ty(Context))
    Lo = CreateIntCast(Lo, Type::getInt32Ty(Context), false, "");
  if (HiTy != Type::getInt32Ty(Context))
    Hi = CreateIntCast(Hi, Type::getInt32Ty(Context), false, "");

  auto *PartSelect = Intrinsic::getDeclaration(
      getModule(), Intrinsic::fpga_legacy_part_select, {ValTy});
  Value *Args[] = {V, Lo, Hi};
  return CreateCall(PartSelect, Args);
}

Value *HLSIRBuilder::CreateBitSelect(Value *V, unsigned Bit) {
  return CreateTrunc(CreateLShr(V, Bit), getInt1Ty());
}

// Returns the updated data computed from \p Byteenable. When the bit in the
// \p Byteenable mask is 1, choose the data from \p CurrValue. Otherwise, choose
// the data from \p PrevValue.
Value *HLSIRBuilder::CreateByteenableUpdate(Value *PrevValue, Value *CurrValue,
                                            Value *Byteenable, unsigned Align) {

  assert(PrevValue->getType() == CurrValue->getType() && "Type mismatch!");

  // First, transform to integer type if the value is lowered packed struct
  // type.
  Type *PrevValueTy = PrevValue->getType();
  bool ValIsPSTy = isLoweredPackedStructTy(PrevValueTy);
  if (ValIsPSTy) {
    Type *EquIntTy = getIntNTy(DL.getTypeSizeInBits(PrevValueTy));
    PrevValue = twoStepsBitCast(PrevValue, EquIntTy);
    CurrValue = twoStepsBitCast(CurrValue, EquIntTy);
  }

  assert(PrevValue->getType()->isIntegerTy() &&
         CurrValue->getType()->isIntegerTy() &&
         Byteenable->getType()->isIntegerTy() && "Expect integer!");
  auto ValueSizeInBits = cast<IntegerType>(PrevValue->getType())->getBitWidth();
  auto ValueSizeInBytes = divideCeil(ValueSizeInBits, 8);
  auto BESizeInBits = Byteenable->getType()->getIntegerBitWidth();
  assert(ValueSizeInBytes == BESizeInBits && "Byteenable mismatch!");

  if (!OptimalByteenableGenerate)
    Align = 1;

  SmallVector<Value *, 16> Bytes;
  for (unsigned i = 0; i < ValueSizeInBytes; i += Align) {
    auto *Enable = CreateBitSelect(Byteenable, i);
    auto LB = i * 8;
    auto UB = std::min(LB + Align * 8 - 1, ValueSizeInBits - 1);
    auto LBVal = getIntN(ValueSizeInBits, LB),
         UBVal = getIntN(ValueSizeInBits, UB);
    auto OldByte = [&]() {
      return CreateAnd(PrevValue, GenerateMask(UBVal, LBVal));
    };
    auto NewByte = [&]() {
      return CreateAnd(CurrValue, GenerateMask(UBVal, LBVal));
    };

    if (match(Enable, m_AllOnes()))
      Bytes.push_back(NewByte());
    else if (match(Enable, m_Zero()))
      Bytes.push_back(OldByte());
    else
      Bytes.push_back(CreateSelect(Enable, NewByte(), OldByte()));
  }

  auto *Val = Bytes.pop_back_val();
  while (!Bytes.empty())
    Val = CreateOr(Val, Bytes.pop_back_val());

  // If the value is lowered packed struct type. Cast back to computed data: Val
  // to lowered packed struct type.
  if (ValIsPSTy)
    Val = twoStepsBitCast(Val, PrevValueTy);

  return Val;
}

Value *HLSIRBuilder::GeneratePartSet(Value *Dst, Value *Src, Value *Lo,
                                     Value *Hi) {
  return CreateOr(CreateAnd(Dst, CreateNot(GenerateMask(
                                     CreateZExtOrTrunc(Hi, Dst->getType()),
                                     CreateZExtOrTrunc(Lo, Dst->getType())))),
                  CreateShl(CreateZExt(Src, Dst->getType()),
                            CreateZExtOrTrunc(Lo, Dst->getType())));
}

Value *HLSIRBuilder::CreatePartSet(Value *Dst, Value *Src, uint32_t Lo,
                                   uint32_t Hi) {
  auto *DstTy = cast<IntegerType>(Dst->getType());
  assert(Lo <= Hi && "Bad range!");
  return CreatePartSet(Dst, Src, ConstantInt::get(DstTy, Lo),
                       ConstantInt::get(DstTy, Hi));
}

Value *HLSIRBuilder::CreatePartSet(Value *Dst, Value *Src, Value *Lo,
                                   Value *Hi) {
  Dst = twoStepsBitCast(Dst);
  Src = twoStepsBitCast(Src);
  assert(cast<IntegerType>(Dst->getType())->getBitWidth() >=
         cast<IntegerType>(Src->getType())->getBitWidth());

  // assert(Lo->getZExtValue() <= Hi->getZExtValue() && "Bad range!");
  Type *ArgTys[] = {Dst->getType(), Src->getType()};
  auto *PartSet =
      Intrinsic::getDeclaration(getModule(), Intrinsic::fpga_part_set, ArgTys);
  Value *Args[] = {Dst, Src, Lo, Hi};
  return CreateCall(PartSet, Args);
}

Value *HLSIRBuilder::GenerateBitConat(ArrayRef<Value *> Args,
                                      IntegerType *RetTy) {
  unsigned TotalSizeInBits = 0;

  Value *Result = ConstantInt::get(RetTy, 0);

  for (auto *V : make_range(Args.rbegin(), Args.rend())) {
    auto *T = cast<IntegerType>(V->getType());
    Result = CreateOr(Result, CreateShl(CreateZExt(V, RetTy), TotalSizeInBits));
    TotalSizeInBits += T->getBitWidth();
  }

  assert(TotalSizeInBits == RetTy->getBitWidth() && "Bad width!");
  return Result;
}

Value *HLSIRBuilder::CreateBitConcat(ArrayRef<Value *> Args) {
  SmallVector<Value *, 8> IntArgs;
  unsigned TotalSizeInBits = 0;
  bool AllConstant = true;

  for (auto *V : Args) {
    V = twoStepsBitCast(V);
    auto *T = cast<IntegerType>(V->getType());
    IntArgs.push_back(V);
    TotalSizeInBits += T->getBitWidth();
    AllConstant &= isa<Constant>(V);
  }

  auto *RetTy = getIntNTy(TotalSizeInBits);

  if (AllConstant)
    return cast<Constant>(GenerateBitConat(IntArgs, RetTy));

  // Create the result type
  auto *BitConcat =
      Intrinsic::getDeclaration(getModule(), Intrinsic::fpga_bit_concat, RetTy);

  return CreateCall(BitConcat, IntArgs);
}

Value *HLSIRBuilder::GetherElements(MutableArrayRef<Value *> Elts) {
  assert(!Elts.empty() && "No element to gether!");

  if (Elts.size() == 1 && Elts[0]->getType()->isIntegerTy())
    return Elts[0];

  std::reverse(Elts.begin(), Elts.end());

  // Move elements to the correct order
  std::transform(Elts.begin(), Elts.end(), Elts.begin(), [](Value *V) {
    if (isa<UndefValue>(V))
      V = Constant::getNullValue(V->getType());

    return V;
  });

  return CreateBitConcat(Elts);
}

Constant *HLSIRBuilder::GetherElements(MutableArrayRef<Constant *> Elts) {
  assert(!Elts.empty() && "No element to gether!");

  if (Elts.size() == 1 && Elts[0]->getType()->isIntegerTy())
    return Elts[0];

  std::reverse(Elts.begin(), Elts.end());

  // Move elements to the correct order
  std::transform(Elts.begin(), Elts.end(), Elts.begin(), [this](Constant *V) {
    if (isa<UndefValue>(V))
      V = Constant::getNullValue(V->getType());

    return cast<Constant>(twoStepsBitCast(V));
  });

  unsigned TotalSizeInBits = std::accumulate(
      Elts.begin(), Elts.end(), 0u, [](unsigned Size, Constant *C) {
        return Size + C->getType()->getIntegerBitWidth();
      });

  auto *RetTy = getIntNTy(TotalSizeInBits);

  ArrayRef<Value *> Values(reinterpret_cast<Value **>(Elts.data()),
                           Elts.size());
  return cast<Constant>(GenerateBitConat(Values, RetTy));
}

Constant *HLSIRBuilder::GetAggregateAsInteger(Constant *C, IntegerType *IntTy) {
  if (C->isNullValue() || isa<UndefValue>(C))
    return Constant::getNullValue(IntTy);

  if (isa<ConstantInt>(C))
    return cast<Constant>(CreateZExtOrTrunc(C, IntTy));

  if (isa<ConstantFP>(C))
    return cast<Constant>(CreateBitCast(C, IntTy));

  if (auto *ConstStruct = dyn_cast<ConstantStruct>(C)) {
    auto *STy = ConstStruct->getType();
    auto EleNum = STy->getNumElements();
    // Little-endian
    auto TotalSize = DL.getTypeAllocSizeInBits(STy);
    APInt APVal(TotalSize, 0);
    for (unsigned Idx = 0; Idx < EleNum; Idx++) {
      auto *EleC = ConstStruct->getAggregateElement(Idx);
      auto *RetC = GetAggregateAsInteger(
          EleC, getIntNTy(DL.getTypeSizeInBits(EleC->getType())));
      auto *SL = DL.getStructLayout(STy);
      auto Offset = SL->getElementOffsetInBits(Idx);
      APInt APV(cast<ConstantInt>(RetC)->getValue());
      assert(APV.getBitWidth() <= TotalSize);
      if (APV.getBitWidth() < TotalSize) // when the struct has only 1 member
        APV = APV.zext(TotalSize);
      APVal = APVal | (APV << Offset);
    }
    return cast<Constant>(CreateZExtOrTrunc(getInt(APVal), IntTy));
  }

  // FIXME: what's the difference between:
  // ConstantArray and ConstantDataArray
  // ConstantVector and ConstantDataVector

  // Handle ConstantArray and ConstantVector
  if (auto *ConstAggr = dyn_cast<ConstantAggregate>(C)) {
    auto *SeqTy = cast<SequentialType>(ConstAggr->getType());
    auto *EleTy = SeqTy->getElementType();
    auto EleNum = SeqTy->getNumElements();
    auto TotalSize = DL.getTypeAllocSizeInBits(SeqTy);
    auto EleSize = DL.getTypeAllocSizeInBits(EleTy);
    assert(EleSize * EleNum == TotalSize);
    APInt APVal(TotalSize, 0);
    for (unsigned Idx = 0; Idx < EleNum; Idx++) {
      auto *EleC = ConstAggr->getAggregateElement(Idx);
      auto *RetC = GetAggregateAsInteger(
          EleC, getIntNTy(DL.getTypeSizeInBits(EleC->getType())));
      assert(RetC->getType()->isIntegerTy());
      APInt APV(cast<ConstantInt>(RetC)->getValue());
      assert(APV.getBitWidth() <= TotalSize);
      if (APV.getBitWidth() < TotalSize) // when the array size is 1.
        APV = APV.zext(TotalSize);
      APVal = APVal | (APV << (Idx * EleSize));
    }
    return cast<Constant>(CreateZExtOrTrunc(getInt(APVal), IntTy));
  }

  llvm_unreachable("Other constant types?!");
  return nullptr;
}

Value *HLSIRBuilder::GetAggregateAsInteger(Value *V) {
  assert(!V->getType()->isPointerTy());
  if (V->getType()->isIntegerTy())
    return V;

  if (auto *U = dyn_cast<UnpackNoneInst>(V))
    return GetAggregateAsInteger(U->getOperand());

  if (isa<Constant>(V))
    return GetAggregateAsInteger(
        cast<Constant>(V),
        getIntNTy(DL.getTypeSizeInBits(StripPadding(V->getType(), DL))));

  if (V->getType()->isFloatTy())
    return CreateBitCast(V, getIntNTy(DL.getTypeSizeInBits(V->getType())));

  if (V->getType()->isAggregateType())
    return CreatePackNone(
        V, getIntNTy(DL.getTypeSizeInBits(StripPadding(V->getType(), DL))));

  assert(!V->getType()->isVectorTy() &&
         "Oops! Need to handle non-constant VectorType!");

  llvm_unreachable("Any other types?");
  return V;
}

Value *HLSIRBuilder::CreateUnpackNone(Value *Bytes, Type *DstTy) {
  auto *SrcTy = Bytes->getType();
  assert(SrcTy->isIntegerTy() && "Unexpected type!");

  if (auto *U = dyn_cast<PackNoneInst>(Bytes))
    if (DstTy == U->getSrcType())
      return U->getOperand();

  // Create the result type
  auto *UnpackF = Intrinsic::getDeclaration(
      getModule(), Intrinsic::fpga_unpack_none, {DstTy, SrcTy});

  return CreateCall(UnpackF, Bytes);
}

Value *HLSIRBuilder::CreatePackNone(Value *Bytes, IntegerType *DstTy) {
  auto *SrcTy = Bytes->getType();
  assert(DstTy->isIntegerTy() && "Unexpected type!");

  if (auto *U = dyn_cast<UnpackNoneInst>(Bytes))
    return CreateZExtOrTrunc(U->getOperand(), DstTy);

  // Create the result type
  auto *PackF = Intrinsic::getDeclaration(
      getModule(), Intrinsic::fpga_pack_none, {DstTy, SrcTy});

  return CreateCall(PackF, Bytes);
}

Value *HLSIRBuilder::CreateUnpackBits(Value *Bits, Type *DstTy) {
  auto *SrcTy = Bits->getType();
  assert(SrcTy->isIntegerTy() && "Unexpected type!");
  assert(cast<IntegerType>(SrcTy)->getBitWidth() ==
         getAggregatedBitwidthInBitLevel(DL, DstTy));

  if (auto *U = dyn_cast<PackBitsInst>(Bits))
    if (DstTy == U->getSrcType())
      return U->getOperand();

  // Create the result type
  auto *UnpackF = Intrinsic::getDeclaration(
      getModule(), Intrinsic::fpga_unpack_bits, {DstTy, SrcTy});

  return CreateCall(UnpackF, Bits);
}

Value *HLSIRBuilder::CreatePackBits(Value *Bits, IntegerType *DstTy) {
  auto *SrcTy = Bits->getType();
  assert(DstTy->isIntegerTy() && "Unexpected type!");
  assert(cast<IntegerType>(DstTy)->getBitWidth() ==
         getAggregatedBitwidthInBitLevel(DL, SrcTy));

  if (auto *U = dyn_cast<UnpackBitsInst>(Bits))
    return CreateZExtOrTrunc(U->getOperand(), DstTy);

  // Create the result type
  auto *PackF = Intrinsic::getDeclaration(
      getModule(), Intrinsic::fpga_pack_bits, {DstTy, SrcTy});

  return CreateCall(PackF, Bits);
}

Value *HLSIRBuilder::CreateUnpackBytes(Value *Bits, Type *DstTy) {
  auto *SrcTy = Bits->getType();
  assert(SrcTy->isIntegerTy() && "Unexpected type!");
  assert(cast<IntegerType>(SrcTy)->getBitWidth() ==
         getAggregatedBitwidthInByteLevel(DL, DstTy));

  if (auto *U = dyn_cast<PackBytesInst>(Bits))
    if (DstTy == U->getSrcType())
      return U->getOperand();

  // Create the result type
  auto *UnpackF = Intrinsic::getDeclaration(
      getModule(), Intrinsic::fpga_unpack_bytes, {DstTy, SrcTy});

  return CreateCall(UnpackF, Bits);
}

Value *HLSIRBuilder::CreatePackBytes(Value *Bits, IntegerType *DstTy) {
  auto *SrcTy = Bits->getType();
  assert(DstTy->isIntegerTy() && "Unexpected type!");
  assert(cast<IntegerType>(DstTy)->getBitWidth() ==
         getAggregatedBitwidthInByteLevel(DL, SrcTy));

  if (auto *U = dyn_cast<UnpackBytesInst>(Bits))
    return CreateZExtOrTrunc(U->getOperand(), DstTy);

  // Create the result type
  auto *PackF = Intrinsic::getDeclaration(
      getModule(), Intrinsic::fpga_pack_bytes, {DstTy, SrcTy});

  return CreateCall(PackF, Bits);
}

Value *HLSIRBuilder::CreateUnpackIntrinsic(Value *IntV, Type *DstTy,
                                           AggregateType AggrTy) {
  switch (AggrTy) {
  case AggregateType::Bit:
    return CreateUnpackBits(IntV, DstTy);
  case AggregateType::Byte:
    return CreateUnpackBytes(IntV, DstTy);
  case AggregateType::NoCompact:
    return CreateUnpackNone(IntV, DstTy);
  default:
    llvm_unreachable("Generate pack intrinsic for NoSpec?!");
  }
  return nullptr;
}

Value *HLSIRBuilder::CreatePackIntrinsic(Value *AggrV, IntegerType *DstTy,
                                         AggregateType AggrTy) {
  switch (AggrTy) {
  case AggregateType::Bit:
    return CreatePackBits(AggrV, DstTy);
  case AggregateType::Byte:
    return CreatePackBytes(AggrV, DstTy);
  case AggregateType::NoCompact:
    return CreatePackNone(AggrV, DstTy);
  default:
    llvm_unreachable("Generate pack intrinsic for NoSpec?!");
  }
  return nullptr;
}

StructType *HLSIRBuilder::getSPIRRTInfoTy() const {
  auto *This = const_cast<HLSIRBuilder *>(this);
  return StructType::create(
      {
          // struct __spir_rt_info_tT {
          //  unsigned int work_dim;
          (Type *)This->getInt32Ty(),

          //  unsigned int global_size[3];
          (Type *)ArrayType::get(This->getInt32Ty(), 3),
          //  unsigned int global_id[3];
          (Type *)ArrayType::get(This->getInt32Ty(), 3),

          //  unsigned int local_size[3];
          (Type *)ArrayType::get(This->getInt32Ty(), 3),
          //  unsigned int local_id[3];
          (Type *)ArrayType::get(This->getInt32Ty(), 3),

          //  unsigned int num_groups[3];
          (Type *)ArrayType::get(This->getInt32Ty(), 3),
          //  unsigned int group_id[3];
          (Type *)ArrayType::get(This->getInt32Ty(), 3),
          //  unsigned int global_offset[3];
          (Type *)ArrayType::get(This->getInt32Ty(), 3),

          //  // of type pthread_barrier_t *
          //  void *pthread_barrier;
          (Type *)This->getInt8PtrTy(),

          //  //barrier using setcontext/getcontext
          //  volatile bool barriertoscheduler;
          (Type *)This->getInt8Ty(),
          //  void *scheduler_context;
          (Type *)This->getInt8PtrTy(),
          //  void *pe_context;
          (Type *)This->getInt8PtrTy(),

          //  unsigned long long printf_buffer;
          (Type *)This->getInt64Ty(),

          // void * instrumentation_context
          (Type *)This->getInt8PtrTy()
          //}
      },
      "struct.__spir_rt_info_tT");
}

Value *HLSIRBuilder::CreateFIFOStatus(Intrinsic::ID ID, Value *Fifo) {
  auto *FifoTy = Fifo->getType();
  auto *Intr = Intrinsic::getDeclaration(getModule(), ID, {FifoTy});
  return CreateCall(Intr, {Fifo});
}

Value *HLSIRBuilder::CreateFIFONotEmpty(Value *Fifo) {
  return CreateFIFOStatus(Intrinsic::fpga_fifo_not_empty, Fifo);
}

Value *HLSIRBuilder::CreateFIFONotFull(Value *Fifo) {
  return CreateFIFOStatus(Intrinsic::fpga_fifo_not_full, Fifo);
}

Value *HLSIRBuilder::CreateFIFOPop(Intrinsic::ID ID, Value *Fifo) {
  auto *FifoTy = Fifo->getType();
  auto *ValTy = FifoTy->getPointerElementType();
  auto *Intr = Intrinsic::getDeclaration(getModule(), ID, {ValTy, FifoTy});
  return CreateCall(Intr, {Fifo});
}

Value *HLSIRBuilder::CreateFIFOPush(Intrinsic::ID ID, Value *V, Value *Fifo) {
  auto *FifoTy = Fifo->getType();
  Type *FifoElemTy = FifoTy->getPointerElementType();
  if (V->getType() != FifoElemTy)
    V = twoStepsBitCast(V, FifoElemTy);

  auto *Intr =
      Intrinsic::getDeclaration(getModule(), ID, {V->getType(), FifoTy});
  return CreateCall(Intr, {V, Fifo});
}

Value *HLSIRBuilder::CreateFIFOPop(Value *Fifo) {
  return CreateFIFOPop(Intrinsic::fpga_fifo_pop, Fifo);
}

Value *HLSIRBuilder::CreateFIFOPush(Value *V, Value *Fifo) {
  return CreateFIFOPush(Intrinsic::fpga_fifo_push, V, Fifo);
}

Value *HLSIRBuilder::CreateFIFONbPop(Value *Fifo) {
  return CreateFIFOPop(Intrinsic::fpga_fifo_nb_pop, Fifo);
}

Value *HLSIRBuilder::CreateFIFONbPush(Value *V, Value *Fifo) {
  return CreateFIFOPush(Intrinsic::fpga_fifo_nb_push, V, Fifo);
}

uint64_t HLSIRBuilder::calculateByteOffset(Type *T,
                                           ArrayRef<unsigned> Indices) {
  if (Indices.empty())
    return 0;

  unsigned Idx = Indices.front();
  if (StructType *ST = dyn_cast<StructType>(T)) {
    uint64_t Offset = DL.getStructLayout(ST)->getElementOffset(Idx);
    auto *EltT = ST->getElementType(Idx);
    return Offset + calculateByteOffset(EltT, Indices.drop_front());
  }

  auto *AT = cast<ArrayType>(T);
  auto *EltT = AT->getElementType();
  uint64_t Offset = Idx * DL.getTypeAllocSize(EltT);
  return Offset + calculateByteOffset(EltT, Indices.drop_front());
}

Value *HLSIRBuilder::CreateSeqBeginEnd(Intrinsic::ID ID, Value *WordAddr,
                                       Value *Size) {
  auto *PtrTy = WordAddr->getType();
  auto *SizeTy = Size->getType();
  auto *SeqFn = Intrinsic::getDeclaration(getModule(), ID, {PtrTy, SizeTy});
  return CreateCall(SeqFn, {WordAddr, Size});
}

CallInst *HLSIRBuilder::CreateReadPipeBlock(Value *Pipe) {
  auto *PipeTy = Pipe->getType();
  auto *Ty = PipeTy->getPointerElementType();
  auto *F = Intrinsic::getDeclaration(
      getModule(), Intrinsic::spir_read_pipe_block_2, {Ty, PipeTy});
  return CreateCall(F, {Pipe});
}

CallInst *HLSIRBuilder::CreateWritePipeBlock(Value *Data, Value *Pipe) {
  auto *PipeTy = Pipe->getType();
  auto *Ty = PipeTy->getPointerElementType();
  Data = twoStepsBitCast(Data, Ty);

  auto *F = Intrinsic::getDeclaration(
      getModule(), Intrinsic::spir_write_pipe_block_2, {Ty, PipeTy});
  return CreateCall(F, {Data, Pipe});
}

Value *HLSIRBuilder::alignDataFromMemory(Value *Data, Value *ByteOffset) {
  // First, transform to integer type if the data is lowered packed struct
  // type.
  Type *DataTy = Data->getType();
  // If Data size <= 1 byte, assume offset must be 0.
  if (DL.getTypeStoreSize(DataTy) <= 1)
    return Data;

  bool DataIsPSTy = isLoweredPackedStructTy(DataTy);
  if (DataIsPSTy)
    Data = twoStepsBitCast(Data, getIntNTy(DL.getTypeSizeInBits(DataTy)));

  // ZExt before we translate bit offset from byte offset, otherwise we may lost
  // data during the translation, and get the bit offset in the word
  auto *BitOffset =
      CreateShl(CreateZExtOrTrunc(ByteOffset, Data->getType()), 3);

  // Shift the data to LSB based on byte offset.
  Data = CreateLShr(Data, BitOffset);

  // If the value is lowered packed struct type. Cast the data back to lowered
  // packed struct type.
  if (DataIsPSTy)
    Data = twoStepsBitCast(Data, DataTy);

  return Data;
}

Value *HLSIRBuilder::alignDataToMemory(Value *Data, Value *ByteOffset) {
  // First, transform to integer type if the data is lowered packed struct
  // type.
  Type *DataTy = Data->getType();
  // If Data size <= 1 byte, assume offset must be 0.
  if (DL.getTypeStoreSize(DataTy) <= 1)
    return Data;

  bool DataIsPSTy = isLoweredPackedStructTy(DataTy);
  if (DataIsPSTy)
    Data = twoStepsBitCast(Data, getIntNTy(DL.getTypeSizeInBits(DataTy)));

  // ZExt before we translate bit offset from byte offset, otherwise we may
  // lost data during the translation, and get the bit offset in the word
  auto *BitOffset =
      CreateShl(CreateZExtOrTrunc(ByteOffset, Data->getType()), 3);

  // Shift the data to LSB based on byte offset.
  Data = CreateShl(Data, BitOffset);

  // If the value is lowered packed struct type. Cast the data back to lowered
  // packed struct type.
  if (DataIsPSTy)
    Data = twoStepsBitCast(Data, DataTy);

  return Data;
}

Value *HLSIRBuilder::alignByteEnableToMemory(Value *ByteEnable,
                                             Value *ByteOffset) {
  Type *ByteEnableTy = ByteEnable->getType();
  if (DL.getTypeSizeInBits(ByteEnableTy) > 1)
    ByteEnable =
        CreateShl(ByteEnable, CreateZExtOrTrunc(ByteOffset, ByteEnableTy));
  return ByteEnable;
}

Value *HLSIRBuilder::ExtractDataFromWord(Type *ResultTy, Value *Word,
                                         Value *ByteOffset) {
  if (ByteOffset && !match(ByteOffset, m_Zero()))
    Word = alignDataFromMemory(Word, ByteOffset);

  Type *WordTy = Word->getType();
  if (isLoweredPackedStructTy(WordTy))
    Word = twoStepsBitCast(Word, getIntNTy(DL.getTypeSizeInBits(WordTy)));

  // Truncate the data
  Word = CreateZExtOrTrunc(Word, getIntNTy(DL.getTypeSizeInBits(ResultTy)));

  return twoStepsBitCast(Word, ResultTy);
}

//===----------------------------------------------------------------------===//
size_t HLSIRBuilder::PadZeros(size_t CurAddr, size_t TargetAddr,
                              size_t WordSizeInBytes, Type *DataTy,
                              SmallVectorImpl<Constant *> &Data) {
  while (TargetAddr > CurAddr) {
    if (auto *STy = dyn_cast<StructType>(DataTy)) {
      // Construct padding zeors for packed struct type: <{ [P * iQ] } >
      assert(isLoweredPackedStructTy(DataTy) &&
             "Expect lowered packed struct type!");
      auto *AT = cast<ArrayType>(STy->getElementType(0));
      auto *ElemTy = dyn_cast<IntegerType>(AT->getElementType());
      auto ElemSizeInBits = ElemTy->getBitWidth();

      SmallVector<Constant *, 1> ArrayField;
      SmallVector<Constant *, 8> Elts;
      auto NumElems =
          getDataLayout().getTypeSizeInBits(DataTy) / ElemSizeInBits;
      for (unsigned i = 0; i < NumElems; ++i)
        Elts.emplace_back(getIntN(ElemSizeInBits, 0));

      ArrayField.emplace_back(ConstantArray::get(AT, Elts));
      Data.push_back(ConstantStruct::get(STy, ArrayField));
    } else
      Data.push_back(getIntN(WordSizeInBytes * 8u, 0));
    CurAddr += WordSizeInBytes;
  }

  assert(TargetAddr == CurAddr && "Base address do not align?");

  return CurAddr;
}

static bool IsAggregateConstant(Constant *C) {
  Type *Ty = C->getType();
  return isa<StructType>(Ty) || isa<ArrayType>(Ty) || isa<VectorType>(Ty);
}

static unsigned GetNumElements(Constant *C) {
  // LLVM has crappy aggregate API...
  assert(isa<ConstantData>(C) || isa<ConstantAggregate>(C));
  // Even those have way too many childrens...

  if (auto *CA = dyn_cast<ConstantAggregate>(C))
    return CA->getNumOperands();
  if (auto *CAZ = dyn_cast<ConstantAggregateZero>(C))
    return CAZ->getNumElements();
  if (auto *CDS = dyn_cast<ConstantDataSequential>(C))
    return CDS->getNumElements();
  if (auto *UV = dyn_cast<UndefValue>(C))
    return UV->getNumElements();

  llvm_unreachable("Not handled.");
}

static Type *GetElementType(Constant *C, unsigned Idx) {
  // LLVM has crappy aggregate API...
  assert(isa<ConstantData>(C) || isa<ConstantAggregate>(C));
  // Even those have way too many childrens...

  Type *Ty = C->getType();
  if (auto *STy = dyn_cast<StructType>(Ty))
    return STy->getElementType(Idx);
  if (auto *ATy = dyn_cast<ArrayType>(Ty))
    return ATy->getElementType();
  if (auto *VTy = dyn_cast<VectorType>(Ty))
    return VTy->getElementType();

  llvm_unreachable("Not handled.");
}

static uint64_t GetElementOffset(Constant *C, unsigned Idx,
                                 const DataLayout &DL) {
  // LLVM has crappy aggregate API...
  assert(isa<ConstantData>(C) || isa<ConstantAggregate>(C));
  // Even those have way too many childrens...

  Type *Ty = C->getType();
  if (auto *STy = dyn_cast<StructType>(Ty)) {
    const StructLayout *SL = DL.getStructLayout(STy);
    return SL->getElementOffset(Idx);
  }
  if (auto *ATy = dyn_cast<ArrayType>(Ty)) {
    Type *ETy = ATy->getElementType();
    return Idx * DL.getTypeAllocSize(ETy);
  }
  if (auto *VTy = dyn_cast<VectorType>(Ty)) {
    Type *ETy = VTy->getElementType();
    return Idx * DL.getTypeAllocSize(ETy);
  }

  llvm_unreachable("Not handled.");
}

static Constant *GetElementConstant(Constant *C, unsigned Idx) {
  // LLVM has crappy aggregate API...
  assert(isa<ConstantData>(C) || isa<ConstantAggregate>(C));
  // Oh, wait, that one is okay, yay!

  return C->getAggregateElement(Idx);

  llvm_unreachable("Not handled.");
}

uint64_t HLSIRBuilder::FillBytes(Constant *C,
                                 SmallVectorImpl<Constant *> &Bytes) {
  // If it's not an aggregate, we have special handling
  if (!IsAggregateConstant(C)) {
    Type *T = C->getType();
    // Anything weird get transformed into a ConstantInt
    if (!isa<IntegerType>(T)) {
      T = IntegerType::get(Context, DL.getTypeSizeInBits(T));
      C = cast<Constant>(CreateBitOrPointerCast(C, T));
    }
    Bytes.push_back(C);
    return DL.getTypeSizeInBits(C->getType());
  }

  uint64_t TotalSizeInBits = 0;

  for (unsigned Idx = 0, N = GetNumElements(C); Idx < N; ++Idx) {
    Constant *V = GetElementConstant(C, Idx);
    uint64_t O = GetElementOffset(C, Idx, DL) * 8;

    // Add padding if we are missing some bytes (typically element are struct)
    if (TotalSizeInBits < O) {
      APInt IPad = APInt((unsigned)(O - TotalSizeInBits), 0);
      auto *CPad = ConstantInt::get(Context, IPad);
      Bytes.push_back(CPad);
      TotalSizeInBits = O;
    }

    // Add the element (recursive)
    TotalSizeInBits += FillBytes(V, Bytes);
  }

  // Purposefully ignore possible padding at the end of the aggregate.
  // If it's desired the caller above must deal with it.
  return TotalSizeInBits;
}

//===----------------------------------------------------------------------===//
//===--------------- pack/unpack intrinsics begin -------------------------===//
Value *HLSIRBuilder::packAggregateToInt(Value *AggObj, AggregateType AggrTy) {
  switch(AggrTy) {
  case AggregateType::NoCompact:
    return packAggregateToInt(AggObj);
  case AggregateType::Bit:
    return packAggregateToIntInBitLevel(AggObj);
  case AggregateType::Byte:
    return packAggregateToIntInByteLevel(AggObj);
  default:
    llvm_unreachable("Other aggregate type?!");
    return nullptr;
  }
  return nullptr;
}
Value *HLSIRBuilder::unpackIntToAggregate(Value *IntObj, Type *RetTy, AggregateType AggrTy) {
  switch(AggrTy) {
  case AggregateType::NoCompact:
    return unpackIntToAggregate(IntObj, RetTy);
  case AggregateType::Bit:
    return unpackIntToAggregateInBitLevel(IntObj, RetTy);
  case AggregateType::Byte:
    return unpackIntToAggregateInByteLevel(IntObj, RetTy);
  default:
    llvm_unreachable("Other aggregate type?!");
    return nullptr;
  }
  return nullptr;
}
//===--------------- pack/unpack intrinsics end   -------------------------===//
//===----------------------------------------------------------------------===//
Value *HLSIRBuilder::packAggregateToInt(Value *AggObj) {
  auto *AggTy = AggObj->getType();
  if (AggTy->isArrayTy()) {
    return packArrayToInt(AggObj);
  } else if (AggTy->isStructTy()) {
    return packStructToInt(AggObj);
  } else if (AggTy->isIntegerTy()) {
    auto SizeInBits = DL.getTypeAllocSizeInBits(AggTy);
    return CreateZExtOrTrunc(AggObj, getIntNTy(SizeInBits),
                             AggObj->getName() + ".cast");
  } else {
    auto SizeInBits = DL.getTypeAllocSizeInBits(AggTy);
    return CreateBitCast(AggObj, getIntNTy(SizeInBits),
                         AggObj->getName() + ".cast");
  }
}

Value *HLSIRBuilder::packArrayToInt(Value *ArrayObj) {
  auto *ATy = cast<ArrayType>(ArrayObj->getType());
  auto SizeInBits = DL.getTypeAllocSizeInBits(ATy);
  Value *RetVal = UndefValue::get(getIntNTy(SizeInBits));
  auto *ElemTy = ATy->getElementType();
  auto ElemSizeInBits = DL.getTypeAllocSizeInBits(ElemTy);
  for (unsigned i = 0; i < ATy->getNumElements(); i++) {
    auto *EVI = CreateExtractValue(
        ArrayObj, {i}, ArrayObj->getName() + "." + std::to_string(i));
    Value *Elem = nullptr;
    if (ElemTy->isStructTy()) {
      Elem = packStructToInt(EVI);
    } else if (ElemTy->isArrayTy()) {
      Elem = packArrayToInt(EVI);
    } else if (ElemTy->isIntegerTy()) {
      // mainly do ext, due to the elem always has smaller bit-width than alloc
      Elem = CreateZExtOrTrunc(EVI, getIntNTy(ElemSizeInBits),
                               EVI->getName() + ".cast");
    } else {
      Elem = CreateBitCast(EVI, getIntNTy(ElemSizeInBits),
                           EVI->getName() + ".cast");
    }

    // part.set
    auto Begin = i * ElemSizeInBits;
    auto End = Begin + ElemSizeInBits - 1;
    RetVal = GeneratePartSet(RetVal, Elem, getIntN(SizeInBits, Begin),
                             getIntN(SizeInBits, End));
  }
  return RetVal;
}

Value *HLSIRBuilder::packStructToInt(Value *StructObj) {
  auto *STy = cast<StructType>(StructObj->getType());
  auto SizeInBits = DL.getTypeAllocSizeInBits(STy);
  Value *RetVal = UndefValue::get(getIntNTy(SizeInBits));
  for (unsigned i = 0; i < STy->getNumElements(); i++) {
    auto *EVI = CreateExtractValue(
        StructObj, {i}, StructObj->getName() + "." + std::to_string(i));
    auto *ElemTy = STy->getElementType(i);
    auto ElemSizeInBits = DL.getTypeAllocSizeInBits(ElemTy);
    Value *Elem = nullptr;
    if (ElemTy->isStructTy()) {
      Elem = packStructToInt(EVI);
    } else if (ElemTy->isArrayTy()) {
      Elem = packArrayToInt(EVI);
    } else if (ElemTy->isIntegerTy()) {
      // mainly do ext, due to the elem always has smaller bit-width than alloc
      Elem = CreateZExtOrTrunc(EVI, getIntNTy(ElemSizeInBits),
                               EVI->getName() + ".cast");
    } else {
      Elem = CreateBitCast(EVI, getIntNTy(ElemSizeInBits),
                           EVI->getName() + ".cast");
    }

    // part.set
    auto Begin = DL.getStructLayout(STy)->getElementOffsetInBits(i);
    auto End = Begin + ElemSizeInBits - 1;
    RetVal = GeneratePartSet(RetVal, Elem, getIntN(SizeInBits, Begin),
                             getIntN(SizeInBits, End));
  }
  return RetVal;
}

Value *HLSIRBuilder::unpackIntToAggregate(Value *IntObj, Type *RetTy) {
  if (auto *ArrTy = dyn_cast<ArrayType>(RetTy))
    return unpackIntToArray(IntObj, ArrTy);
  else if (auto *STy = dyn_cast<StructType>(RetTy))
    return unpackIntToStruct(IntObj, STy);
  else if (RetTy->isIntegerTy())
    return CreateZExtOrTrunc(IntObj, RetTy, IntObj->getName() + ".cast");
  else {
    auto RetSize = DL.getTypeAllocSizeInBits(RetTy);
    return CreateBitCast(CreateZExtOrTrunc(IntObj, getIntNTy(RetSize)), RetTy,
                         IntObj->getName() + ".cast");
  }
}

Value *HLSIRBuilder::unpackIntToArray(Value *IntObj, ArrayType *RetTy) {
  auto SizeInBits = DL.getTypeAllocSizeInBits(RetTy);
  Value *RetVal = UndefValue::get(RetTy);
  auto *ElemTy = RetTy->getElementType();
  auto ElemSizeInBits = DL.getTypeAllocSizeInBits(ElemTy);
  for (unsigned i = 0; i < RetTy->getNumElements(); i++) {
    // part.select
    auto Begin = i * ElemSizeInBits;
    auto End = Begin + ElemSizeInBits - 1;
    auto *SubIntObj =
        GeneratePartSelect(IntObj, getIntN(SizeInBits, Begin),
                           getIntN(SizeInBits, End), getIntNTy(ElemSizeInBits));
    Value *Elem = nullptr;
    if (ElemTy->isStructTy()) {
      Elem = unpackIntToStruct(SubIntObj, cast<StructType>(ElemTy));
    } else if (ElemTy->isArrayTy()) {
      Elem = unpackIntToArray(SubIntObj, cast<ArrayType>(ElemTy));
    } else if (ElemTy->isIntegerTy()) {
      // mainly do trunc
      Elem = CreateZExtOrTrunc(SubIntObj, ElemTy,
                               IntObj->getName() + "." + std::to_string(i) +
                                   ".cast");
    } else {
      Elem =
          CreateBitCast(SubIntObj, ElemTy,
                        IntObj->getName() + "." + std::to_string(i) + ".cast");
    }

    RetVal = CreateInsertValue(RetVal, Elem, {i},
                               IntObj->getName() + "." + std::to_string(i));
  }
  return RetVal;
}

Value *HLSIRBuilder::unpackIntToStruct(Value *IntObj, StructType *RetTy) {
  auto SizeInBits = DL.getTypeAllocSizeInBits(RetTy);
  Value *RetVal = UndefValue::get(RetTy);
  for (unsigned i = 0; i < RetTy->getNumElements(); i++) {
    auto *ElemTy = RetTy->getElementType(i);
    auto ElemSizeInBits = DL.getTypeAllocSizeInBits(ElemTy);
    // part.select
    auto Begin = DL.getStructLayout(RetTy)->getElementOffsetInBits(i);
    auto End = Begin + ElemSizeInBits - 1;
    auto *SubIntObj =
        GeneratePartSelect(IntObj, getIntN(SizeInBits, Begin),
                           getIntN(SizeInBits, End), getIntNTy(ElemSizeInBits));
    Value *Elem = nullptr;
    if (ElemTy->isStructTy()) {
      Elem = unpackIntToStruct(SubIntObj, cast<StructType>(ElemTy));
    } else if (ElemTy->isArrayTy()) {
      Elem = unpackIntToArray(SubIntObj, cast<ArrayType>(ElemTy));
    } else if (ElemTy->isIntegerTy()) {
      // mainly do trunc
      Elem = CreateZExtOrTrunc(SubIntObj, ElemTy,
                               IntObj->getName() + "." + std::to_string(i) +
                                   ".cast");
    } else {
      Elem =
          CreateBitCast(SubIntObj, ElemTy,
                        IntObj->getName() + "." + std::to_string(i) + ".cast");
    }

    RetVal = CreateInsertValue(RetVal, Elem, {i},
                               IntObj->getName() + "." + std::to_string(i));
  }
  return RetVal;
}

//-- llvm.fpga.pack/unpack.bits -- begin --

Value *HLSIRBuilder::packAggregateToIntInBitLevel(Value *AggObj) {
  auto *AggTy = AggObj->getType();
  if (AggTy->isArrayTy()) {
    return packArrayToIntInBitLevel(AggObj);
  } else if (AggTy->isStructTy()) {
    return packStructToIntInBitLevel(AggObj);
  } else if (AggTy->isIntegerTy()) {
    return AggObj;
  } else {
    auto SizeInBits = DL.getTypeAllocSizeInBits(AggTy);
    return CreateBitCast(AggObj, getIntNTy(SizeInBits),
                         AggObj->getName() + ".cast");
  }
}

Value *HLSIRBuilder::packArrayToIntInBitLevel(Value *ArrayObj) {
  auto *ATy = cast<ArrayType>(ArrayObj->getType());
  auto SizeInBits = getAggregatedBitwidthInBitLevel(DL, ATy);
  Value *RetVal = UndefValue::get(getIntNTy(SizeInBits));
  auto *ElemTy = ATy->getElementType();
  auto ElemSizeInBits = getAggregatedBitwidthInBitLevel(DL, ElemTy);
  for (unsigned i = 0; i < ATy->getNumElements(); i++) {
    auto *EVI = CreateExtractValue(
        ArrayObj, {i}, ArrayObj->getName() + "." + std::to_string(i));
    Value *Elem = nullptr;
    if (ElemTy->isStructTy()) {
      Elem = packStructToIntInBitLevel(EVI);
    } else if (ElemTy->isArrayTy()) {
      Elem = packArrayToIntInBitLevel(EVI);
    } else if (ElemTy->isIntegerTy()) {
      assert(cast<IntegerType>(ElemTy)->getBitWidth() == ElemSizeInBits);
      Elem = EVI;
    } else {
      Elem = CreateBitCast(EVI, getIntNTy(ElemSizeInBits),
                           EVI->getName() + ".cast");
    }

    // part.set
    auto Begin = i * ElemSizeInBits;
    auto End = Begin + ElemSizeInBits - 1;
    RetVal = GeneratePartSet(RetVal, Elem, getIntN(SizeInBits, Begin),
                             getIntN(SizeInBits, End));
  }
  return RetVal;
}

Value *HLSIRBuilder::packStructToIntInBitLevel(Value *StructObj) {
  auto *STy = cast<StructType>(StructObj->getType());
  auto SizeInBits = getAggregatedBitwidthInBitLevel(DL, STy);
  Value *RetVal = UndefValue::get(getIntNTy(SizeInBits));
  uint64_t ElemOffset = 0;
  for (unsigned i = 0; i < STy->getNumElements(); i++) {
    auto *EVI = CreateExtractValue(
        StructObj, {i}, StructObj->getName() + "." + std::to_string(i));
    auto *ElemTy = STy->getElementType(i);
    auto ElemSizeInBits = getAggregatedBitwidthInBitLevel(DL, ElemTy);
    Value *Elem = nullptr;
    if (ElemTy->isStructTy()) {
      Elem = packStructToIntInBitLevel(EVI);
    } else if (ElemTy->isArrayTy()) {
      Elem = packArrayToIntInBitLevel(EVI);
    } else if (ElemTy->isIntegerTy()) {
      assert(cast<IntegerType>(ElemTy)->getBitWidth() == ElemSizeInBits);
      Elem = EVI;
    } else {
      Elem = CreateBitCast(EVI, getIntNTy(ElemSizeInBits),
                           EVI->getName() + ".cast");
    }

    // part.set
    auto Begin = ElemOffset;
    auto End = Begin + ElemSizeInBits - 1;
    RetVal = GeneratePartSet(RetVal, Elem, getIntN(SizeInBits, Begin),
                             getIntN(SizeInBits, End));
    ElemOffset += ElemSizeInBits;
  }
  return RetVal;
}

Value *HLSIRBuilder::unpackIntToAggregateInBitLevel(Value *IntObj,
                                                    Type *RetTy) {
  if (auto *ArrTy = dyn_cast<ArrayType>(RetTy))
    return unpackIntToArrayInBitLevel(IntObj, ArrTy);
  else if (auto *STy = dyn_cast<StructType>(RetTy))
    return unpackIntToStructInBitLevel(IntObj, STy);
  else if (RetTy->isIntegerTy())
    return CreateZExtOrTrunc(IntObj, RetTy, IntObj->getName() + ".cast");
  else {
    auto RetSize = DL.getTypeAllocSizeInBits(RetTy);
    return CreateBitCast(CreateZExtOrTrunc(IntObj, getIntNTy(RetSize)), RetTy,
                         IntObj->getName() + ".cast");
  }
}

Value *HLSIRBuilder::unpackIntToArrayInBitLevel(Value *IntObj,
                                                ArrayType *RetTy) {
  auto SizeInBits = getAggregatedBitwidthInBitLevel(DL, RetTy);
  assert(cast<IntegerType>(IntObj->getType())->getBitWidth() == SizeInBits);
  Value *RetVal = UndefValue::get(RetTy);
  auto *ElemTy = RetTy->getElementType();
  auto ElemSizeInBits = getAggregatedBitwidthInBitLevel(DL, ElemTy);
  for (unsigned i = 0; i < RetTy->getNumElements(); i++) {
    // part.select
    auto Begin = i * ElemSizeInBits;
    auto End = Begin + ElemSizeInBits - 1;
    auto *SubIntObj =
        GeneratePartSelect(IntObj, getIntN(SizeInBits, Begin),
                           getIntN(SizeInBits, End), getIntNTy(ElemSizeInBits));
    Value *Elem = nullptr;
    if (ElemTy->isStructTy()) {
      Elem = unpackIntToStructInBitLevel(SubIntObj, cast<StructType>(ElemTy));
    } else if (ElemTy->isArrayTy()) {
      Elem = unpackIntToArrayInBitLevel(SubIntObj, cast<ArrayType>(ElemTy));
    } else if (ElemTy->isIntegerTy()) {
      assert(cast<IntegerType>(ElemTy)->getBitWidth() == ElemSizeInBits);
      Elem = SubIntObj;
    } else {
      assert(DL.getTypeSizeInBits(ElemTy) == ElemSizeInBits);
      Elem =
          CreateBitCast(SubIntObj, ElemTy,
                        IntObj->getName() + "." + std::to_string(i) + ".cast");
    }

    RetVal = CreateInsertValue(RetVal, Elem, {i},
                               IntObj->getName() + "." + std::to_string(i));
  }
  return RetVal;
}

Value *HLSIRBuilder::unpackIntToStructInBitLevel(Value *IntObj,
                                                 StructType *RetTy) {
  auto SizeInBits = getAggregatedBitwidthInBitLevel(DL, RetTy);
  assert(cast<IntegerType>(IntObj->getType())->getBitWidth() == SizeInBits);
  Value *RetVal = UndefValue::get(RetTy);
  uint64_t ElemOffset = 0;
  for (unsigned i = 0; i < RetTy->getNumElements(); i++) {
    auto *ElemTy = RetTy->getElementType(i);
    auto ElemSizeInBits = getAggregatedBitwidthInBitLevel(DL, ElemTy);
    // part.select
    auto Begin = ElemOffset;
    auto End = Begin + ElemSizeInBits - 1;
    ElemOffset += ElemSizeInBits;
    auto *SubIntObj =
        GeneratePartSelect(IntObj, getIntN(SizeInBits, Begin),
                           getIntN(SizeInBits, End), getIntNTy(ElemSizeInBits));
    Value *Elem = nullptr;
    if (ElemTy->isStructTy()) {
      Elem = unpackIntToStructInBitLevel(SubIntObj, cast<StructType>(ElemTy));
    } else if (ElemTy->isArrayTy()) {
      Elem = unpackIntToArrayInBitLevel(SubIntObj, cast<ArrayType>(ElemTy));
    } else if (ElemTy->isIntegerTy()) {
      assert(cast<IntegerType>(ElemTy)->getBitWidth() == ElemSizeInBits);
      Elem = SubIntObj;
    } else {
      Elem =
          CreateBitCast(SubIntObj, ElemTy,
                        IntObj->getName() + "." + std::to_string(i) + ".cast");
    }

    RetVal = CreateInsertValue(RetVal, Elem, {i},
                               IntObj->getName() + "." + std::to_string(i));
  }
  return RetVal;
}
//-- llvm.fpga.pack/unpack.bits -- end --

//-- llvm.fpga.pack/unpack.bytes -- begin --
#if 1
Value *HLSIRBuilder::packAggregateToIntInByteLevel(Value *AggObj) {
  auto *AggTy = AggObj->getType();
  if (AggTy->isArrayTy()) {
    return packArrayToIntInByteLevel(AggObj);
  } else if (AggTy->isStructTy()) {
    return packStructToIntInByteLevel(AggObj);
  } else if (AggTy->isIntegerTy()) {
    auto SizeInBits = getAggregatedBitwidthInByteLevel(DL, AggTy);
    return CreateZExt(AggObj, getIntNTy(SizeInBits));
  } else {
    auto SizeInBits = DL.getTypeAllocSizeInBits(AggTy);
    return CreateBitCast(AggObj, getIntNTy(SizeInBits),
                         AggObj->getName() + ".cast");
  }
}

Value *HLSIRBuilder::packArrayToIntInByteLevel(Value *ArrayObj) {
  auto *ATy = cast<ArrayType>(ArrayObj->getType());
  auto SizeInBits = getAggregatedBitwidthInByteLevel(DL, ATy);
  Value *RetVal = UndefValue::get(getIntNTy(SizeInBits));
  auto *ElemTy = ATy->getElementType();
  auto ElemSizeInBits = getAggregatedBitwidthInByteLevel(DL, ElemTy);
  for (unsigned i = 0; i < ATy->getNumElements(); i++) {
    auto *EVI = CreateExtractValue(
        ArrayObj, {i}, ArrayObj->getName() + "." + std::to_string(i));
    Value *Elem = nullptr;
    if (ElemTy->isStructTy()) {
      Elem = packStructToIntInByteLevel(EVI);
    } else if (ElemTy->isArrayTy()) {
      Elem = packArrayToIntInByteLevel(EVI);
    } else if (ElemTy->isIntegerTy()) {
      assert(cast<IntegerType>(ElemTy)->getBitWidth() <= ElemSizeInBits);
      Elem = CreateZExt(EVI, getIntNTy(ElemSizeInBits));
    } else {
      Elem = CreateBitCast(EVI, getIntNTy(ElemSizeInBits),
                           EVI->getName() + ".cast");
    }

    // part.set
    auto Begin = i * ElemSizeInBits;
    auto End = Begin + ElemSizeInBits - 1;
    RetVal = GeneratePartSet(RetVal, Elem, getIntN(SizeInBits, Begin),
                             getIntN(SizeInBits, End));
  }
  return RetVal;
}

Value *HLSIRBuilder::packStructToIntInByteLevel(Value *StructObj) {
  auto *STy = cast<StructType>(StructObj->getType());
  auto SizeInBits = getAggregatedBitwidthInByteLevel(DL, STy);
  Value *RetVal = UndefValue::get(getIntNTy(SizeInBits));
  uint64_t ElemOffset = 0;
  for (unsigned i = 0; i < STy->getNumElements(); i++) {
    auto *EVI = CreateExtractValue(
        StructObj, {i}, StructObj->getName() + "." + std::to_string(i));
    auto *ElemTy = STy->getElementType(i);
    auto ElemSizeInBits = getAggregatedBitwidthInByteLevel(DL, ElemTy);
    Value *Elem = nullptr;
    if (ElemTy->isStructTy()) {
      Elem = packStructToIntInByteLevel(EVI);
    } else if (ElemTy->isArrayTy()) {
      Elem = packArrayToIntInByteLevel(EVI);
    } else if (ElemTy->isIntegerTy()) {
      assert(cast<IntegerType>(ElemTy)->getBitWidth() <= ElemSizeInBits);
      Elem = CreateZExt(EVI, getIntNTy(ElemSizeInBits));
    } else {
      Elem = CreateBitCast(EVI, getIntNTy(ElemSizeInBits),
                           EVI->getName() + ".cast");
    }

    // part.set
    auto Begin = ElemOffset;
    auto End = Begin + ElemSizeInBits - 1;
    RetVal = GeneratePartSet(RetVal, Elem, getIntN(SizeInBits, Begin),
                             getIntN(SizeInBits, End));
    ElemOffset += ElemSizeInBits;
  }
  return RetVal;
}

Value *HLSIRBuilder::unpackIntToAggregateInByteLevel(Value *IntObj,
                                                     Type *RetTy) {
  if (auto *ArrTy = dyn_cast<ArrayType>(RetTy))
    return unpackIntToArrayInByteLevel(IntObj, ArrTy);
  else if (auto *STy = dyn_cast<StructType>(RetTy))
    return unpackIntToStructInByteLevel(IntObj, STy);
  else if (RetTy->isIntegerTy())
    return CreateZExtOrTrunc(IntObj, RetTy, IntObj->getName() + ".cast");
  else {
    auto RetSize = DL.getTypeAllocSizeInBits(RetTy);
    return CreateBitCast(CreateZExtOrTrunc(IntObj, getIntNTy(RetSize)), RetTy,
                         IntObj->getName() + ".cast");
  }
}

Value *HLSIRBuilder::unpackIntToArrayInByteLevel(Value *IntObj,
                                                 ArrayType *RetTy) {
  auto SizeInBits = getAggregatedBitwidthInByteLevel(DL, RetTy);
  assert(cast<IntegerType>(IntObj->getType())->getBitWidth() == SizeInBits);
  Value *RetVal = UndefValue::get(RetTy);
  auto *ElemTy = RetTy->getElementType();
  auto ElemSizeInBits = getAggregatedBitwidthInByteLevel(DL, ElemTy);
  for (unsigned i = 0; i < RetTy->getNumElements(); i++) {
    // part.select
    auto Begin = i * ElemSizeInBits;
    auto End = Begin + ElemSizeInBits - 1;
    auto *SubIntObj =
        GeneratePartSelect(IntObj, getIntN(SizeInBits, Begin),
                           getIntN(SizeInBits, End), getIntNTy(ElemSizeInBits));
    Value *Elem = nullptr;
    if (ElemTy->isStructTy()) {
      Elem = unpackIntToStructInByteLevel(SubIntObj, cast<StructType>(ElemTy));
    } else if (ElemTy->isArrayTy()) {
      Elem = unpackIntToArrayInByteLevel(SubIntObj, cast<ArrayType>(ElemTy));
    } else if (ElemTy->isIntegerTy()) {
      auto ElemBW = cast<IntegerType>(ElemTy)->getBitWidth();
      assert(ElemBW <= ElemSizeInBits);
      Elem = CreateTrunc(SubIntObj, getIntNTy(ElemBW),
                         IntObj->getName() + "." + std::to_string(i) + ".cast");
    } else {
      assert(DL.getTypeSizeInBits(ElemTy) == ElemSizeInBits);
      Elem =
          CreateBitCast(SubIntObj, ElemTy,
                        IntObj->getName() + "." + std::to_string(i) + ".cast");
    }

    RetVal = CreateInsertValue(RetVal, Elem, {i},
                               IntObj->getName() + "." + std::to_string(i));
  }
  return RetVal;
}

Value *HLSIRBuilder::unpackIntToStructInByteLevel(Value *IntObj,
                                                  StructType *RetTy) {
  auto SizeInBits = getAggregatedBitwidthInByteLevel(DL, RetTy);
  assert(cast<IntegerType>(IntObj->getType())->getBitWidth() == SizeInBits);
  Value *RetVal = UndefValue::get(RetTy);
  uint64_t ElemOffset = 0;
  for (unsigned i = 0; i < RetTy->getNumElements(); i++) {
    auto *ElemTy = RetTy->getElementType(i);
    auto ElemSizeInBits = getAggregatedBitwidthInByteLevel(DL, ElemTy);
    // part.select
    auto Begin = ElemOffset;
    auto End = Begin + ElemSizeInBits - 1;
    ElemOffset += ElemSizeInBits;
    auto *SubIntObj =
        GeneratePartSelect(IntObj, getIntN(SizeInBits, Begin),
                           getIntN(SizeInBits, End), getIntNTy(ElemSizeInBits));
    Value *Elem = nullptr;
    if (ElemTy->isStructTy()) {
      Elem = unpackIntToStructInByteLevel(SubIntObj, cast<StructType>(ElemTy));
    } else if (ElemTy->isArrayTy()) {
      Elem = unpackIntToArrayInByteLevel(SubIntObj, cast<ArrayType>(ElemTy));
    } else if (ElemTy->isIntegerTy()) {
      auto ElemBW = cast<IntegerType>(ElemTy)->getBitWidth();
      assert(ElemBW <= ElemSizeInBits);
      Elem = CreateTrunc(SubIntObj, getIntNTy(ElemBW),
                         IntObj->getName() + "." + std::to_string(i) + ".cast");
    } else {
      Elem =
          CreateBitCast(SubIntObj, ElemTy,
                        IntObj->getName() + "." + std::to_string(i) + ".cast");
    }

    RetVal = CreateInsertValue(RetVal, Elem, {i},
                               IntObj->getName() + "." + std::to_string(i));
  }
  return RetVal;
}
#endif
//-- llvm.fpga.pack/unpack.bytes -- end --

Value *HLSIRBuilder::CreateArrayPartitionInst(
    Value *V, ArrayXFormInst<ArrayPartitionInst>::XFormMode Mode, int32_t Dim,
    int32_t Factor) {
  auto *M = getModule();
  Type *Int32Ty = Type::getInt32Ty(M->getContext());
  return Insert(PragmaInst::Create<ArrayPartitionInst>(
      {V, ConstantInt::get(Int32Ty, Mode), ConstantInt::get(Int32Ty, Factor),
       ConstantInt::getSigned(Int32Ty, Dim)},
      nullptr, M));
}

Value *HLSIRBuilder::CreateArrayReshapeInst(
    Value *V, ArrayXFormInst<ArrayReshapeInst>::XFormMode Mode, int32_t Dim,
    int32_t Factor) {
  auto *M = getModule();
  Type *Int32Ty = Type::getInt32Ty(M->getContext());
  return Insert(PragmaInst::Create<ArrayReshapeInst>(
      {V, ConstantInt::get(Int32Ty, Mode), ConstantInt::get(Int32Ty, Factor),
       ConstantInt::getSigned(Int32Ty, Dim)},
      nullptr, M));
}

Value *HLSIRBuilder::CreateDependenceInst(Value *V, bool isEnforced,
                                          DependenceInst::DepType Ty,
                                          DependenceInst::Direction Dir,
                                          int32_t Dist) {
  auto *M = getModule();
  Type *Int32Ty = Type::getInt32Ty(M->getContext());
  auto DirCode = static_cast<int>(Dir);
  auto TypeCode = static_cast<int>(Ty);
  return Insert(PragmaInst::Create<DependenceInst>(
      {V, ConstantInt::get(Int32Ty, /* No class*/ 0),
       ConstantInt::get(Int32Ty, isEnforced),
       ConstantInt::get(Int32Ty, DirCode),
       ConstantInt::getSigned(Int32Ty, Dist),
       ConstantInt::get(Int32Ty, TypeCode)},
      nullptr, M));
}

Value *HLSIRBuilder::CreateAggregateInst(Value *V) {
  auto *M = getModule();
  return Insert(PragmaInst::Create<AggregateInst>({V}, nullptr, M));
}

Value *HLSIRBuilder::CreateDisaggregateInst(Value *V) {
  auto *M = getModule();
  return Insert(PragmaInst::Create<DisaggrInst>({V}, nullptr, M));
}

Value *HLSIRBuilder::CreateStreamPragmaInst(Value *V, int32_t Depth) {
  auto *M = getModule();
  Type *Int32Ty = Type::getInt32Ty(M->getContext());
  return Insert(PragmaInst::Create<StreamPragmaInst>(
      {V, ConstantInt::getSigned(Int32Ty, Depth)}, nullptr, M));
}

Value *HLSIRBuilder::CreatePipoPragmaInst(Value *V, int32_t Depth) {
  auto *M = getModule();
  Type *Int32Ty = Type::getInt32Ty(M->getContext());
  return Insert(PragmaInst::Create<PipoPragmaInst>(
      {V, ConstantInt::getSigned(Int32Ty, Depth)}, nullptr, M));
}

Value *HLSIRBuilder::CreateSAXIPragmaInst(Value *V, StringRef Bundle,
                                          uint64_t Offset, bool HasRegister,
                                          StringRef SignalName) {
  auto *M = getModule();
  auto &Ctx = M->getContext();
  return Insert(PragmaInst::Create<SAXIInst>(
      {V, ConstantDataArray::getString(Ctx, Bundle, false),
       ConstantInt::getSigned(Type::getInt64Ty(Ctx), Offset),
       ConstantInt::get(Type::getInt1Ty(Ctx), HasRegister),
       ConstantDataArray::getString(Ctx, SignalName, false)},
      nullptr, M));
}

Value *HLSIRBuilder::CreateMAXIPragmaInst(
    Value *V, StringRef Bundle, int64_t Depth, StringRef Offset,
    StringRef SignalName, int64_t NumReadOutstanding,
    int64_t NumWriteOutstanding, int64_t MaxReadBurstLen,
    int64_t MaxWriteBurstLen, int64_t Latency, int64_t MaxWidenBitwidth) {
  auto *M = getModule();
  auto &Ctx = M->getContext();
  Type *Int64Ty = Type::getInt64Ty(Ctx);
  return Insert(PragmaInst::Create<MaxiInst>(
      {V, ConstantDataArray::getString(Ctx, Bundle, false),
       ConstantInt::getSigned(Int64Ty, Depth),
       ConstantDataArray::getString(Ctx, Offset, false),
       ConstantDataArray::getString(Ctx, SignalName, false),
       ConstantInt::getSigned(Int64Ty, NumReadOutstanding),
       ConstantInt::getSigned(Int64Ty, NumWriteOutstanding),
       ConstantInt::getSigned(Int64Ty, MaxReadBurstLen),
       ConstantInt::getSigned(Int64Ty, MaxWriteBurstLen),
       ConstantInt::getSigned(Int64Ty, Latency),
       ConstantInt::getSigned(Int64Ty, MaxWidenBitwidth)},
      nullptr, M));
}

Value *HLSIRBuilder::CreateAXISPragmaInst(Value *V, bool HasRegister,
                                          int64_t RegisterMode, int64_t Depth,
                                          StringRef SignalName) {
  auto *M = getModule();
  auto &Ctx = M->getContext();
  Type *Int64Ty = Type::getInt64Ty(Ctx);
  return Insert(PragmaInst::Create<AxiSInst>(
      {V, ConstantInt::get(Type::getInt1Ty(Ctx), HasRegister),
       ConstantInt::getSigned(Int64Ty, RegisterMode),
       ConstantInt::getSigned(Int64Ty, Depth),
       ConstantDataArray::getString(Ctx, SignalName, false)},
      nullptr, M));
}

Value *HLSIRBuilder::CreateAPFIFOPragmaInst(Value *V, bool HasRegister,
                                            StringRef SignalName,
                                            int64_t Depth) {
  auto *M = getModule();
  auto &Ctx = M->getContext();
  return Insert(PragmaInst::Create<ApFifoInst>(
      {V, ConstantInt::get(Type::getInt1Ty(Ctx), HasRegister),
       ConstantDataArray::getString(Ctx, SignalName, false),
       ConstantInt::getSigned(Type::getInt64Ty(Ctx), Depth)},
      nullptr, M));
}
