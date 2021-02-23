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
// This file defines the HLS IRBuilder
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_XILINXHLSIRBUILDER_H
#define LLVM_IR_XILINXHLSIRBUILDER_H

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/XILINXFPGAIntrinsicInst.h"
#include "llvm/Support/XILINXAggregateUtil.h"

namespace llvm {

class DataLayout;
class InterfaceMD;

class HLSIRBuilder : public IRBuilder<> {

  Value *GenerateMask(Value *Hi, Value *Lo);

  unsigned getVectorTypeNumElts(VectorType *VT, unsigned EltSizeInBits) const;

  unsigned getConstantVectorNumElts(Value *V, unsigned EltSizeInBits) const;

  Value *CreatePartSelectCall(Value *V, Value *Lo, Value *Hi,
                              IntegerType *RetTy);
  Value *CreateLegacyPartSelectCall(Value *V, Value *Lo, Value *Hi);

protected:
  const DataLayout &DL;

public:
  HLSIRBuilder(BasicBlock *TheBB, BasicBlock::iterator IP,
               MDNode *FPMathTag = nullptr,
               ArrayRef<OperandBundleDef> OpBundles = None);
  explicit HLSIRBuilder(BasicBlock *TheBB, MDNode *FPMathTag = nullptr,
                        ArrayRef<OperandBundleDef> OpBundles = None);
  HLSIRBuilder(LLVMContext &C, const DataLayout &DL,
               MDNode *FPMathTag = nullptr,
               ArrayRef<OperandBundleDef> OpBundles = None);
  explicit HLSIRBuilder(Instruction *IP, MDNode *FPMathTag = nullptr,
                        ArrayRef<OperandBundleDef> OPBundles = None);

  //===--------------------------------------------------------------------===//
  Value *CreateMul(Value *LHS, APInt RHS, const Twine &Name = "");
  Value *CreateUDiv(Value *LHS, APInt RHS, const Twine &Name = "",
                    bool isExact = false);
  Value *CreateURem(Value *LHS, APInt RHS, const Twine &Name = "");
  Value *CreateSDiv(Value *LHS, APInt RHS, const Twine &Name = "",
                    bool isExact = false);

  using IRBuilder::CreateMul;
  using IRBuilder::CreateSDiv;
  using IRBuilder::CreateUDiv;
  using IRBuilder::CreateURem;

  //===--------------------------------------------------------------------===//
  const DataLayout &getDataLayout() const { return DL; }

  /// \brief Fetch the type representing a pointer to an integer value.
  IntegerType *getIntPtrTy(unsigned AddrSpace = 0) const {
    return DL.getIntPtrType(Context, AddrSpace);
  }

  /// \brief Get a constant IntPtr value.
  ConstantInt *getIntPtr(uint64_t C, unsigned AddrSpace = 0) {
    return ConstantInt::get(getIntPtrTy(AddrSpace), C);
  }
  ConstantInt *getIntPtrNull(unsigned AddrSpace = 0) {
    return ConstantInt::get(getIntPtrTy(AddrSpace), 0);
  }

  bool isLoweredPackedStructTy(Type *T);

  Module *getModule() const { return GetInsertBlock()->getModule(); }

  Value *twoStepsBitCast(Value *V);
  Value *twoStepsBitCast(Value *V, Type *DstTy);

  //===--------------------------------------------------------------------===//
  Constant *rewriteConstant(Constant *C, Type *T);
  void rewriteConstants(SmallVectorImpl<Constant *> &Constants,
                        SmallVectorImpl<char> &Bytes, IntegerType *T);
  void rewriteConstants(SmallVectorImpl<Constant *> &Constants,
                        SmallVectorImpl<char> &Bytes, StructType *T);

  //===--------------------------------------------------------------------===//
  // Bit manipulate operators, also cast the arguments to bits first

  /// \brief Generate the logic to perform fpga.part.select
  Value *GeneratePartSelect(Value *V, Value *Lo, Value *Hi, IntegerType *RetTy);

  /// \brief Create Call to fpga.part.select
  Value *CreatePartSelect(Value *V, Value *Lo, Value *Hi, IntegerType *RetTy);
  Value *CreatePartSelect(Value *V, uint32_t Lo, uint32_t Hi);

  /// \brief Generate the logic to perform fpga.part.set
  Value *GeneratePartSet(Value *Dst, Value *Src, Value *Lo, Value *Hi);

  Value *CreateBitSelect(Value *V, unsigned Bit);
  Value *CreateByteenableUpdate(Value *OldValue, Value *NewValue,
                                Value *Byteenable, unsigned Align);

  /// \brief Create Call to fpga.part.set
  Value *CreatePartSet(Value *Dst, Value *Src, Value *Lo, Value *Hi);
  Value *CreatePartSet(Value *Dst, Value *Src, uint32_t Lo, uint32_t Hi);

  /// \brief Create Call to fpga.bit.concat

  /// \brief Generate the logic to perform fpga.bit.concat
  Value *GenerateBitConat(ArrayRef<Value *> Args, IntegerType *RetTy);

  /// \brief Create Call to fpga.bit.concat
  Value *CreateBitConcat(ArrayRef<Value *> Args);

  Value *GetherElements(MutableArrayRef<Value *> Elts);
  Constant *GetherElements(MutableArrayRef<Constant *> Elts);

  /// \brief Create Call to fpga.unpack.none
  Value *CreateUnpackNone(Value *Bytes, Type *DstTy);
  /// \brief Create Call to fpga.pack.none
  Value *CreatePackNone(Value *Struct, IntegerType *DstTy);
  /// \brief Create Call to fpga.unpack.bits
  Value *CreateUnpackBits(Value *Bits, Type *DstTy);
  /// \brief Create Call to fpga.pack.bits
  Value *CreatePackBits(Value *Struct, IntegerType *DstTy);
  /// \brief Create Call to fpga.unpack.bytes
  Value *CreateUnpackBytes(Value *Bits, Type *DstTy);
  /// \brief Create Call to fpga.pack.bytes
  Value *CreatePackBytes(Value *Struct, IntegerType *DstTy);
  /// \brief Create Call to fpga.unpack related intrinsics
  Value *CreateUnpackIntrinsic(Value *IntV, Type *DstTy, AggregateType AggrTy);
  /// \brief Create Call to fpga.pack related intrinsics
  Value *CreatePackIntrinsic(Value *AggrV, IntegerType *DstTy,
                             AggregateType AggrTy);

  Value *GetAggregateAsInteger(Value *V);
  Constant *GetAggregateAsInteger(Constant *C, IntegerType *IntTy);

  //===--------------------------------------------------------------------===//
  // FIFO related
  Value *CreateFIFOStatus(Intrinsic::ID ID, Value *Fifo);
  Value *CreateFIFOPop(Intrinsic::ID ID, Value *Fifo);
  Value *CreateFIFOPush(Intrinsic::ID ID, Value *V, Value *Fifo);
  // FIFO instinsics
  Value *CreateFIFONotEmpty(Value *Fifo);
  Value *CreateFIFONotFull(Value *Fifo);
  Value *CreateFIFOPop(Value *Fifo);
  Value *CreateFIFOPush(Value *V, Value *Fifo);
  Value *CreateFIFONbPop(Value *Fifo);
  Value *CreateFIFONbPush(Value *V, Value *Fifo);

  //===--------------------------------------------------------------------===//
  /// Memory related

  uint64_t calculateByteOffset(Type *T, ArrayRef<unsigned> Indices);

  Value *alignDataFromMemory(Value *Data, Value *ByteOffset);
  Value *alignDataToMemory(Value *Data, Value *ByteOffset);
  Value *alignByteEnableToMemory(Value *ByteEnable, Value *ByteOffset);

  Value *ExtractDataFromWord(Type *ResultTy, Value *Word, Value *ByteOffset);

  Value *CreateSeqBeginEnd(Intrinsic::ID ID, Value *WordAddr, Value *Size);
  CallInst *CreateWritePipeBlock(Value *Data, Value *Pipe);
  CallInst *CreateReadPipeBlock(Value *Pipe);

  //===--------------------------------------------------------------------===//
  /// Datalayout in memory (assume byte address system)
  size_t PadZeros(size_t CurAddr, size_t TargetAddr, size_t WordSizeInBytes,
                  Type *DataTy, SmallVectorImpl<Constant *> &Data);
  uint64_t FillBytes(Constant *C, SmallVectorImpl<Constant *> &Bytes);

  //===--------------------------------------------------------------------===//
  // SPIR runtime info
  StructType *getSPIRRTInfoTy() const;

  //===--------------------------------------------------------------------===//
  // Lower pack/unpack intrinsics
  Value *packAggregateToInt(Value *AggObj, AggregateType AggrTy);
  Value *unpackIntToAggregate(Value *IntObj, Type *RetTy, AggregateType AggrTy);

  //===--------------------------------------------------------------------===//
  // Lower pack/unpack intrinsics (-compact none)
  Value *packAggregateToInt(Value *AggObj);
  Value *packStructToInt(Value *StructObj);
  Value *packArrayToInt(Value *ArrayObj);
  Value *unpackIntToAggregate(Value *IntObj, Type *RetTy);
  Value *unpackIntToStruct(Value *IntObj, StructType *RetTy);
  Value *unpackIntToArray(Value *IntObj, ArrayType *RetTy);

  //===--------------------------------------------------------------------===//
  // Lower bit-level pack/unpack intrinsics
  Value *packAggregateToIntInBitLevel(Value *AggObj);
  Value *packStructToIntInBitLevel(Value *StructObj);
  Value *packArrayToIntInBitLevel(Value *ArrayObj);
  Value *unpackIntToAggregateInBitLevel(Value *IntObj, Type *RetTy);
  Value *unpackIntToStructInBitLevel(Value *IntObj, StructType *RetTy);
  Value *unpackIntToArrayInBitLevel(Value *IntObj, ArrayType *RetTy);

  //===--------------------------------------------------------------------===//
  // Lower byte-level pack/unpack intrinsics
  Value *packAggregateToIntInByteLevel(Value *AggObj);
  Value *packStructToIntInByteLevel(Value *StructObj);
  Value *packArrayToIntInByteLevel(Value *ArrayObj);
  Value *unpackIntToAggregateInByteLevel(Value *IntObj, Type *RetTy);
  Value *unpackIntToStructInByteLevel(Value *IntObj, StructType *RetTy);
  Value *unpackIntToArrayInByteLevel(Value *IntObj, ArrayType *RetTy);

  //===--------------------------------------------------------------------===//
  // Pragma on variables

  Value *
  CreateArrayPartitionInst(Value *V,
                           ArrayXFormInst<ArrayPartitionInst>::XFormMode Mode,
                           int32_t Dim, int32_t Factor = 0);
  Value *
  CreateArrayReshapeInst(Value *V,
                         ArrayXFormInst<ArrayReshapeInst>::XFormMode Mode,
                         int32_t Dim, int32_t Factor = 0);
  Value *CreateDependenceInst(
      Value *V, bool isEnforced, DependenceInst::DepType Ty,
      DependenceInst::Direction Dir = DependenceInst::Direction::NODIR,
      int32_t Dist = 0);
  Value *CreateAggregateInst(Value *V);
  Value *CreateDisaggregateInst(Value *V);
  Value *CreateStreamPragmaInst(Value *V, int32_t Depth);
  Value *CreatePipoPragmaInst(Value *V, int32_t Depth);
  Value *CreateSAXIPragmaInst(Value *V, StringRef Bundle, uint64_t Offset,
                              bool HasRegister, StringRef SignalName);
  Value *CreateMAXIPragmaInst(Value *V, StringRef Bundle, int64_t Depth,
                              StringRef Offset, StringRef SignalName,
                              int64_t NumReadOutstanding,
                              int64_t NumWriteOutstanding,
                              int64_t MaxReadBurstLen, int64_t MaxWriteBurstLen,
                              int64_t Latency, int64_t MaxWidenBitwidth);
  Value *CreateAXISPragmaInst(Value *V, bool HasRegister, int64_t RegisterMode,
                              int64_t Depth, StringRef SignalName);
  Value *CreateAPFIFOPragmaInst(Value *V, bool HasRegister,
                                StringRef SignalName, int64_t Depth);

  /// Create bram/ap_memory PragmaInst
  template <class Kind>
  Value *CreateBRAMPragmaInst(Value *V, int64_t StorageType, int64_t ImplType,
                              int64_t Latency, StringRef SignalName) {
    auto *M = getModule();
    auto &Ctx = M->getContext();
    Type *Int64Ty = Type::getInt64Ty(Ctx);
    return Insert(PragmaInst::Create<Kind>(
        {V, ConstantInt::getSigned(Int64Ty, StorageType),
         ConstantInt::getSigned(Int64Ty, ImplType),
         ConstantInt::getSigned(Int64Ty, Latency),
         ConstantDataArray::getString(Ctx, SignalName)},
        nullptr, M));
  }

  /// Create ap_none/ap_ack/ap_vld/ap_ovld/ap_hs/ap_stable PragmaInst
  template <class Kind>
  Value *CreateScalarPragmaInst(Value *V, bool HasRegister,
                                StringRef SignalName) {
    auto *M = getModule();
    auto &Ctx = M->getContext();
    return Insert(PragmaInst::Create<Kind>(
        {V, ConstantInt::get(Type::getInt1Ty(Ctx), HasRegister),
         ConstantDataArray::getString(Ctx, SignalName)},
        nullptr, M));
  }
};

} // namespace llvm

#endif // !LLVM_IR_XILINXHLSIRBUILDER_H
