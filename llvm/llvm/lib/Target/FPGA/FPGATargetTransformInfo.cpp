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
/// This file implements a TargetTransformInfo analysis pass specific to the
/// FPGA target machine. It uses the target's detailed information to provide
/// more precise answers to certain TTI queries, while letting the target
/// independent and default TTI implementations handle the rest.
///
//===----------------------------------------------------------------------===//

#include "FPGATargetTransformInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/CodeGen/CostTable.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"

using namespace llvm;

#define DEBUG_TYPE "fpgatti"

//===----------------------------------------------------------------------===//
//
// FPGA spmd/simt execution.
//
//===----------------------------------------------------------------------===//

bool FPGATTIImpl::hasBranchDivergence() { return true; }

bool FPGATTIImpl::isRegisterRich() { return true; }

static bool IsKernelFunction(const Function &F) {
  return F.getCallingConv() == CallingConv::SPIR_KERNEL;
}

static bool readsLocalId(const IntrinsicInst *II) {
  switch (II->getIntrinsicID()) {
  default:
    return false;
  case Intrinsic::spir_get_local_id:
  case Intrinsic::spir_get_local_linear_id:
  case Intrinsic::spir_get_global_id:
  case Intrinsic::spir_get_global_linear_id:
    return true;
  }
}

static bool readWorkgroupInvariant(const IntrinsicInst *II) {
  switch (II->getIntrinsicID()) {
  default:
    return false;
  case Intrinsic::spir_get_work_dim:
  case Intrinsic::spir_get_global_size:
  case Intrinsic::spir_get_local_size:
  case Intrinsic::spir_get_enqueued_local_size:
  case Intrinsic::spir_get_num_groups:
  case Intrinsic::spir_get_group_id:
  case Intrinsic::spir_get_global_offset:

  case Intrinsic::spir_get_global_id_base:
    return true;
  }
}

static bool isPureFunction(const IntrinsicInst *II) {
  switch (II->getIntrinsicID()) {
  default:
    return false;
  case Intrinsic::ssa_copy:
    return true;
  }
}

bool FPGATTIImpl::isSourceOfDivergence(const Value *V) {
  // Without inter-procedural analysis, we conservatively assume that arguments
  // to spir_func functions are divergent.
  if (const Argument *Arg = dyn_cast<Argument>(V))
    return !IsKernelFunction(*Arg->getParent());

  if (const Instruction *I = dyn_cast<Instruction>(V)) {
    // Without pointer analysis, we conservatively assume values loaded from
    // private address space are divergent.
    if (const LoadInst *LI = dyn_cast<LoadInst>(I)) {
      unsigned AS = LI->getPointerAddressSpace();
      return AS == 0;
    }

    // Alloca in private address space are divergent.
    if (const AllocaInst *AI = dyn_cast<AllocaInst>(I)) {
      unsigned AS = AI->getType()->getPointerAddressSpace();
      return AS == 0;
    }

    // Atomic instructions may cause divergence.
    // In CUDA, Atomic instructions are executed sequentially across all threads
    // in a warp. Therefore, an earlier executed thread may see different memory
    // inputs than a later executed thread. For example, suppose *a = 0
    // initially.
    //
    //   atom.global.add.s32 d, [a], 1
    //
    // returns 0 for the first thread that enters the critical region, and 1 for
    // the second thread.
    // TODO: Are there atomic instructions in OpenCL?
    if (I->isAtomic())
      return true;

    if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
      // Instructions that read threadIdx are obviously divergent.
      if (readsLocalId(II))
        return true;
      // Handle the NVPTX atomic instrinsics that cannot be represented as an
      // atomic IR instruction.
      // if (isNVVMAtomic(II))
      //   return true;

      if (readWorkgroupInvariant(II))
        return false;

      if (isPureFunction(II))
        return false;
    }

    // Conservatively consider the return value of function calls as divergent.
    // We could analyze callees with bodies more precisely using
    // inter-procedural analysis.
    if (isa<CallInst>(I))
      return true;
  }

  return false;
}

//===----------------------------------------------------------------------===//
//
// FPGA cost model.
//
//===----------------------------------------------------------------------===//

int FPGATTIImpl::getOperationCost(unsigned Opcode, Type *Ty, Type *OpTy) {
  switch (Opcode) {
  default:
    return BaseT::getOperationCost(Opcode, Ty, OpTy);
  // Bit fan are free
  case Instruction::BitCast:
  case Instruction::IntToPtr:
  case Instruction::PtrToInt:
  case Instruction::Trunc:
    return TTI::TCC_Free;
  }
}

// Function calls are free
int FPGATTIImpl::getCallCost(FunctionType *FTy, int NumArgs) {
  return BaseT::getCallCost(FTy, NumArgs);
}
int FPGATTIImpl::getCallCost(const Function *F, int NumArgs) {
  return BaseT::getCallCost(F, NumArgs);
}
int FPGATTIImpl::getCallCost(const Function *F,
                             ArrayRef<const Value *> Arguments) {
  return BaseT::getCallCost(F, Arguments);
}

// Any immediate value can be synthesized
bool FPGATTIImpl::isLegalAddImmediate(int64_t Imm) { return true; }
bool FPGATTIImpl::isLegalICmpImmediate(int64_t Imm) { return true; }

// Masked memory operations are free
bool FPGATTIImpl::isLegalMaskedStore(Type *DataType) { return true; }
bool FPGATTIImpl::isLegalMaskedLoad(Type *DataType) { return true; }

bool FPGATTIImpl::isTruncateFree(Type *Ty1, Type *Ty2) {
  if (!Ty1->isIntegerTy() || !Ty2->isIntegerTy())
    return false;
  unsigned NumBits1 = Ty1->getPrimitiveSizeInBits();
  unsigned NumBits2 = Ty2->getPrimitiveSizeInBits();
  return NumBits1 > NumBits2;
}

bool FPGATTIImpl::isTypeLegal(Type *Ty) { return true; }

// Switch as lookup tables is not desired
bool FPGATTIImpl::shouldBuildLookupTables() { return false; }

TargetTransformInfo::PopcntSupportKind
FPGATTIImpl::getPopcntSupport(unsigned IntTyWidthInBit) {
  return TTI::PSK_FastHardware;
}

unsigned FPGATTIImpl::getNumberOfRegisters(bool Vector) {
  return std::numeric_limits<unsigned>::max() >> 2;
}

unsigned FPGATTIImpl::getRegisterBitWidth(bool Vector) const {
  return std::numeric_limits<unsigned>::max() >> 2;
}

int FPGATTIImpl::getShuffleCost(TTI::ShuffleKind Kind, Type *Tp, int Index,
                                Type *SubTp) {
  return TTI::TCC_Free;
}

int FPGATTIImpl::getCastInstrCost(unsigned Opcode, Type *Dst, Type *Src,
                                  const Instruction *I) {
  return TTI::TCC_Free;
}

int FPGATTIImpl::getExtractWithExtendCost(unsigned Opcode, Type *Dst,
                                          VectorType *VecTy, unsigned Index) {
  return TTI::TCC_Free;
}

int FPGATTIImpl::getVectorInstrCost(unsigned Opcode, Type *Val,
                                    unsigned Index) {
  return TTI::TCC_Free;
}

int FPGATTIImpl::getIntrinsicCost(Intrinsic::ID IID, Type *RetTy,
                                  ArrayRef<Type *> ParamTys) {
  switch (IID) {
  default:
    return BaseT::getIntrinsicCost(IID, RetTy, ParamTys);

  // Local and uniform simply return a variable
  case Intrinsic::spir_get_work_dim:
  case Intrinsic::spir_get_local_size:
  case Intrinsic::spir_get_enqueued_local_size:
  case Intrinsic::spir_get_local_id:
  case Intrinsic::spir_get_num_groups:
  case Intrinsic::spir_get_group_id:
  case Intrinsic::spir_get_global_offset:
    return TTI::TCC_Free;

  // Those technically do a little bit of computation
  case Intrinsic::spir_get_global_size:
  case Intrinsic::spir_get_global_id:
  case Intrinsic::spir_get_global_linear_id:
  case Intrinsic::spir_get_local_linear_id:
    return TTI::TCC_Basic;

  // Pipe read/write are just like ordinary instructions
  case Intrinsic::spir_read_pipe_block_2:
  case Intrinsic::spir_write_pipe_block_2:
  case Intrinsic::spir_read_pipe_2:
  case Intrinsic::spir_write_pipe_2:
  // Burst read/write is also ok
  case Intrinsic::fpga_seq_load:
  case Intrinsic::fpga_seq_store:
    return TTI::TCC_Basic;
  // Requests are expensive
  case Intrinsic::fpga_seq_load_begin:
  case Intrinsic::fpga_seq_store_begin:
    return ParamTys.front()->getPointerAddressSpace() == AS_MAXI
               ? TTI::TCC_Expensive
               : TTI::TCC_Free;
  }
}

int FPGATTIImpl::getIntrinsicCost(Intrinsic::ID IID, Type *RetTy,
                                  ArrayRef<const Value *> Arguments) {
  return BaseT::getIntrinsicCost(IID, RetTy, Arguments);
}

static bool IsBitwiseBinaryOperator(unsigned Opcode) {
  switch (Opcode) {
  default:
    return Instruction::isShift(Opcode);
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
  case Instruction::ExtractElement:
  case Instruction::ExtractValue:
  case Instruction::InsertElement:
  case Instruction::InsertValue:
    return true;
  }
}

static bool IsFreeOperator(const User *U) {
  if (isa<BitCastOperator>(U))
    return true;

  auto *O = dyn_cast<Operator>(U);
  if (!O)
    return false;

  auto Opcode = O->getOpcode();

  // Bitwise operator with constant on RHS is definitely free.
  // Notice that a free bitwise operator is not necessary having a constant RHS
  Value *RHS = *std::prev(U->op_end());
  if (IsBitwiseBinaryOperator(Opcode) && isa<Constant>(RHS))
    return true;

  return false;
}

int FPGATTIImpl::getUserCost(const User *U, ArrayRef<const Value *> Operands) {
  if (IsFreeOperator(U))
    return TTI::TCC_Free;

  return BaseT::getUserCost(U, Operands);
}

void FPGATTIImpl::getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                                          TTI::UnrollingPreferences &UP) {
  UP.Threshold = 0;
  UP.PartialThreshold = 0;
  UP.Count = 1;
  UP.MaxCount = 1;
  UP.FullUnrollMaxCount = 1;
  UP.Partial = false;
  UP.Runtime = false;
  UP.AllowRemainder = false;
}

Type *FPGATTIImpl::getMemcpyLoopLoweringType(LLVMContext &Context,
                                             Value *LengthInBytes,
                                             unsigned SrcAlignInBytes,
                                             unsigned DestAlignInBytes) const {
  uint64_t Min = MinAlign(SrcAlignInBytes, DestAlignInBytes);
  KnownBits KB = computeKnownBits(LengthInBytes, getDataLayout());
  Min = MinAlign(Min, 1 << KB.countMinTrailingZeros());
  return IntegerType::get(Context, 8 * Min);
}

unsigned FPGATTIImpl::getLoadStoreVecRegBitWidth(unsigned AddrSpace) const {
  return 512;
}

bool FPGATTIImpl::isLegalToVectorizeLoadChain(unsigned ChainSizeInBytes,
                                              unsigned Alignment,
                                              unsigned AddrSpace) const {
  return true;
}

bool FPGATTIImpl::isLegalToVectorizeStoreChain(unsigned ChainSizeInBytes,
                                               unsigned Alignment,
                                               unsigned AddrSpace) const {
  return true;
}
