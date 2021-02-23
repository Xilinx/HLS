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
/// \file
/// This file a TargetTransformInfo::Concept conforming object specific to the
/// FPGA target machine. It uses the target's detailed information to
/// provide more precise answers to certain TTI queries, while letting the
/// target independent and default TTI implementations handle the rest.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_FPGA_FPGATARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_FPGA_FPGATARGETTRANSFORMINFO_H

#include "FPGATargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/TargetTransformInfoImpl.h"

namespace llvm {

class FPGATTIImpl : public TargetTransformInfoImplCRTPBase<FPGATTIImpl> {
  typedef TargetTransformInfoImplCRTPBase<FPGATTIImpl> BaseT;
  typedef TargetTransformInfo TTI;
  friend BaseT;

  const FPGATargetMachine *TM;
  const FPGASubtarget *ST;

  const FPGATargetMachine *getTM() const { return TM; }
  const FPGASubtarget *getST() const { return ST; }

public:
  explicit FPGATTIImpl(const FPGATargetMachine *TM, const Function &F)
      : BaseT(F.getParent()->getDataLayout()), TM(TM),
        ST(TM->getSubtargetImpl(F)) {}

  // Provide value semantics. MSVC requires that we spell all of these out.
  FPGATTIImpl(const FPGATTIImpl &Arg)
      : BaseT(static_cast<const BaseT &>(Arg)), TM(Arg.TM), ST(Arg.ST) {}
  FPGATTIImpl(FPGATTIImpl &&Arg)
      : BaseT(std::move(static_cast<BaseT &>(Arg))), TM(std::move(Arg.TM)),
        ST(std::move(Arg.ST)) {}

  enum InterfaceAS {
    AS_MAXI = 1 // Address space for AXI-MM master
  };

  int getOperationCost(unsigned Opcode, Type *Ty, Type *OpTy);
  // int getGEPCost(Type *PointeeType, const Value *Ptr, ArrayRef<const Value *>
  // Operands);
  int getCallCost(FunctionType *FTy, int NumArgs);
  int getCallCost(const Function *F, int NumArgs);
  int getCallCost(const Function *F, ArrayRef<const Value *> Arguments);
  // unsigned getInliningThresholdMultiplier();
  int getIntrinsicCost(Intrinsic::ID IID, Type *RetTy,
                       ArrayRef<Type *> ParamTys);
  int getIntrinsicCost(Intrinsic::ID IID, Type *RetTy,
                       ArrayRef<const Value *> Arguments);
  int getUserCost(const User *U, ArrayRef<const Value *> Operands);
  bool isRegisterRich();
  bool hasBranchDivergence();
  bool isSourceOfDivergence(const Value *V);
  // bool isLoweredToCall(const Function *F);
  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP);
  bool isLegalAddImmediate(int64_t Imm);
  bool isLegalICmpImmediate(int64_t Imm);
  // bool isLegalAddressingMode(Type *Ty, GlobalValue *BaseGV, int64_t
  // BaseOffset, bool HasBaseReg, int64_t Scale, unsigned AddrSpace);
  bool isLegalMaskedStore(Type *DataType);
  bool isLegalMaskedLoad(Type *DataType);
  // bool isLegalMaskedScatter(Type *DataType);
  // bool isLegalMaskedGather(Type *DataType);
  // int getScalingFactorCost(Type *Ty, GlobalValue *BaseGV, int64_t BaseOffset,
  // bool HasBaseReg, int64_t Scale, unsigned AddrSpace);
  bool isTruncateFree(Type *Ty1, Type *Ty2);
  // bool isProfitableToHoist(Instruction *I);
  bool isTypeLegal(Type *Ty);
  // unsigned getJumpBufAlignment();
  // unsigned getJumpBufSize();
  bool shouldBuildLookupTables();
  // bool enableAggressiveInterleaving(bool LoopHasReductions);
  // bool enableInterleavedAccessVectorization();
  // bool isFPVectorizationPotentiallyUnsafe();
  // bool allowsMisalignedMemoryAccesses(unsigned BitWidth, unsigned
  // AddressSpace, unsigned Alignment, bool *Fast);
  TTI::PopcntSupportKind getPopcntSupport(unsigned IntTyWidthInBit);
  // bool haveFastSqrt(Type *Ty);
  // int getFPOpCost(Type *Ty);
  // int getIntImmCodeSizeCost(unsigned Opc, unsigned Idx, const APInt &Imm,
  // Type *Ty);
  // int getIntImmCost(const APInt &Imm, Type *Ty);
  // int getIntImmCost(unsigned Opc, unsigned Idx, const APInt &Imm, Type *Ty);
  // int getIntImmCost(Intrinsic::ID IID, unsigned Idx, const APInt &Imm, Type
  // *Ty);
  unsigned getNumberOfRegisters(bool Vector);
  unsigned getRegisterBitWidth(bool Vector) const;
  // unsigned getCacheLineSize();
  // unsigned getPrefetchDistance();
  // unsigned getMinPrefetchStride();
  // unsigned getMaxPrefetchIterationsAhead();
  // unsigned getMaxInterleaveFactor(unsigned VF);
  // unsigned getArithmeticInstrCost(unsigned Opcode, Type *Ty,
  // TTI::OperandValueKind Opd1Info, TTI::OperandValueKind Opd2Info,
  // TTI::OperandValueProperties Opd1PropInfo, TTI::OperandValueProperties
  // Opd2PropInfo);
  int getShuffleCost(TTI::ShuffleKind Kind, Type *Tp, int Index, Type *SubTp);
  int getCastInstrCost(unsigned Opcode, Type *Dst, Type *Src,
                       const Instruction *I);
  int getExtractWithExtendCost(unsigned Opcode, Type *Dst, VectorType *VecTy,
                               unsigned Index);
  // int getCFInstrCost(unsigned Opcode);
  // int getCmpSelInstrCost(unsigned Opcode, Type *ValTy, Type *CondTy);
  int getVectorInstrCost(unsigned Opcode, Type *Val, unsigned Index);
  // int getMemoryOpCost(unsigned Opcode, Type *Src, unsigned Alignment,
  // unsigned AddressSpace);
  // int getMaskedMemoryOpCost(unsigned Opcode, Type *Src, unsigned Alignment,
  // unsigned AddressSpace);
  // int getGatherScatterOpCost(unsigned Opcode, Type *DataTy, Value *Ptr, bool
  // VariableMask, unsigned Alignment);
  // int getInterleavedMemoryOpCost(unsigned Opcode, Type *VecTy, unsigned
  // Factor, ArrayRef<unsigned> Indices, unsigned Alignment, unsigned
  // AddressSpace);
  // int getReductionCost(unsigned Opcode, Type *Ty, bool IsPairwiseForm);
  // int getIntrinsicInstrCost(Intrinsic::ID ID, Type *RetTy, ArrayRef<Type *>
  // Tys, FastMathFlags FMF);
  // int getIntrinsicInstrCost(Intrinsic::ID ID, Type *RetTy, ArrayRef<Value *>
  // Args, FastMathFlags FMF);
  // int getCallInstrCost(Function *F, Type *RetTy, ArrayRef<Type *> Tys);
  // unsigned getNumberOfParts(Type *Tp);
  // int getAddressComputationCost(Type *Ty, bool IsComplex);
  // unsigned getCostOfKeepingLiveOverCall(ArrayRef<Type *> Tys);
  // bool getTgtMemIntrinsic(IntrinsicInst *Inst, MemIntrinsicInfo &Info);
  // Value *getOrCreateResultFromMemIntrinsic(IntrinsicInst *Inst, Type
  // *ExpectedType);
  // bool areInlineCompatible(const Function *Caller, const Function *Callee)
  // const;
  Type *getMemcpyLoopLoweringType(LLVMContext &Context, Value *LengthInBytes,
                                  unsigned SrcAlignInBytes,
                                  unsigned DestAlignInBytes) const;

  unsigned getLoadStoreVecRegBitWidth(unsigned AddrSpace) const;
  bool isLegalToVectorizeLoadChain(unsigned ChainSizeInBytes,
                                   unsigned Alignment,
                                   unsigned AddrSpace) const;
  bool isLegalToVectorizeStoreChain(unsigned ChainSizeInBytes,
                                    unsigned Alignment,
                                    unsigned AddrSpace) const;
};

} // end namespace llvm

#endif
