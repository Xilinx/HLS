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
// This file implements classes that make it really easy to deal with intrinsic
// functions in FPGA with the isa/dyncast family of functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/XILINXFPGAIntrinsicInst.h"

using namespace llvm;
using namespace PatternMatch;

static Value *StripBitOrPointerCast(Value *V) {
  if (auto Cast = dyn_cast<BitCastInst>(V))
    return StripBitOrPointerCast(Cast->getOperand(0));

  if (auto Cast = dyn_cast<IntToPtrInst>(V))
    return StripBitOrPointerCast(Cast->getOperand(0));

  if (auto Cast = dyn_cast<PtrToIntInst>(V))
    return StripBitOrPointerCast(Cast->getOperand(0));

  return V;
}

Value *BitConcatInst::getBits(unsigned Hi, unsigned Lo) const {
  unsigned CurHi = getType()->getBitWidth();
  for (auto &V : arg_operands()) {
    if (Hi > CurHi)
      break;

    unsigned SizeInBits = V->getType()->getIntegerBitWidth();
    if (Hi + 1 == CurHi && Lo == CurHi - SizeInBits)
      return V;

    assert(CurHi >= SizeInBits && "Bad size!");
    CurHi -= SizeInBits;
  }

  return nullptr;
}

Value *BitConcatInst::getElement(unsigned Hi, unsigned Lo) const {
  return StripBitOrPointerCast(getBits(Hi, Lo));
}

Value *PartSelectInst::getRawSrc() const {
  return StripBitOrPointerCast(getSrc());
}

Value *PartSelectInst::getSrc() const { return getOperand(0); }

Value *LegacyPartSelectInst::getSrc() const { return getOperand(0); }

Type *LegacyPartSelectInst::getSrcTy() const { return getSrc()->getType(); }

Value *LegacyPartSelectInst::getLo() const { return getOperand(1); }

Value *LegacyPartSelectInst::getHi() const { return getOperand(2); }

Value *LegacyPartSetInst::getSrc() const { return getOperand(0); }

Type *LegacyPartSetInst::getSrcTy() const { return getSrc()->getType(); }

Value *LegacyPartSetInst::getRep() const { return getOperand(1); }

Type *LegacyPartSetInst::getRepTy() const { return getRep()->getType(); }

Value *LegacyPartSetInst::getLo() const { return getOperand(2); }

Value *LegacyPartSetInst::getHi() const { return getOperand(3); }

Value *UnpackNoneInst::getOperand() const { return getArgOperand(0); }

Value *PackNoneInst::getOperand() const { return getArgOperand(0); }

Value *UnpackBytesInst::getOperand() const { return getArgOperand(0); }

Value *PackBytesInst::getOperand() const { return getArgOperand(0); }

Value *UnpackBitsInst::getOperand() const { return getArgOperand(0); }

Value *PackBitsInst::getOperand() const { return getArgOperand(0); }

Value *FPGALoadStoreInst::getPointerOperand() {
  if (auto *LD = dyn_cast<FPGALoadInst>(this))
    return LD->getPointerOperand();

  return cast<FPGAStoreInst>(this)->getPointerOperand();
}

const Value *FPGALoadStoreInst::getPointerOperand() const {
  if (auto *LD = dyn_cast<FPGALoadInst>(this))
    return LD->getPointerOperand();

  return cast<FPGAStoreInst>(this)->getPointerOperand();
}

unsigned FPGALoadStoreInst::getPointerAddressSpace() const {
  return getPointerOperand()->getType()->getPointerAddressSpace();
}

PointerType *FPGALoadStoreInst::getPointerType() const {
  return cast<PointerType>(getPointerOperand()->getType());
}

Type *FPGALoadStoreInst::getDataType() const {
  return getPointerType()->getElementType();
}

Value *FPGALoadInst::getPointerOperand() { return getArgOperand(0); }

const Value *FPGALoadInst::getPointerOperand() const {
  return getArgOperand(0);
}

Value *FPGAStoreInst::getPointerOperand() { return getArgOperand(1); }

const Value *FPGAStoreInst::getPointerOperand() const {
  return getArgOperand(1);
}

Value *SeqBeginInst::getPointerOperand() const { return getArgOperand(0); }

PointerType *SeqBeginInst::getPointerType() const {
  return cast<PointerType>(getPointerOperand()->getType());
}

void SeqBeginInst::updatePointer(Value *V) { getArgOperandUse(0).set(V); }

uint64_t SeqBeginInst::getSmallConstantSize() const {
  uint64_t Size = 0;
  match(getSize(), m_ConstantInt(Size));
  return Size;
}

uint64_t SeqBeginInst::getSmallConstantSizeInBytes(const DataLayout &DL) const {
  return getSmallConstantSize() * DL.getTypeAllocSize(getDataType());
}

void SeqBeginInst::updateSize(Value *V) { return getArgOperandUse(1).set(V); }

SeqBeginInst *SeqEndInst::getBegin() const {
  return cast<SeqBeginInst>(getArgOperand(0));
}

void SeqEndInst::updateSize(Value *V) { return getArgOperandUse(1).set(V); }

Type *SeqAccessInst::getDataType() const {
  if (auto *Ld = dyn_cast<SeqLoadInst>(this))
    return Ld->getDataType();

  return cast<SeqStoreInst>(this)->getDataType();
}

SeqBeginInst *SeqAccessInst::getPointerOperand() const {
  if (auto *Ld = dyn_cast<SeqLoadInst>(this))
    return Ld->getPointerOperand();

  return cast<SeqStoreInst>(this)->getPointerOperand();
}

Value *SeqAccessInst::getIndex() const {
  if (auto *Ld = dyn_cast<SeqLoadInst>(this))
    return Ld->getIndex();

  return cast<SeqStoreInst>(this)->getIndex();
}

void SeqAccessInst::updateIndex(Value *V) {
  if (auto *Ld = dyn_cast<SeqLoadInst>(this))
    return Ld->updateIndex(V);

  return cast<SeqStoreInst>(this)->updateIndex(V);
}

SeqBeginInst *SeqLoadInst::getPointerOperand() const {
  return cast<SeqBeginInst>(getArgOperand(0));
}

void SeqLoadInst::updateIndex(Value *V) { getArgOperandUse(1).set(V); }

SeqBeginInst *SeqStoreInst::getPointerOperand() const {
  return cast<SeqBeginInst>(getArgOperand(1));
}

void SeqStoreInst::updateIndex(Value *V) { getArgOperandUse(2).set(V); }

Value *SeqStoreInst::getValueOperand() const { return getArgOperand(0); }

Value *SeqStoreInst::getByteEnable() const { return getArgOperand(3); }

bool SeqStoreInst::isMasked() const {
  return !match(getByteEnable(), m_AllOnes());
}

Value *ShiftRegInst::getPointerOperand() const {
  if (auto *Shift = dyn_cast<ShiftRegShiftInst>(this))
    return Shift->getPointerOperand();

  return cast<ShiftRegPeekInst>(this)->getPointerOperand();
}

Type *ShiftRegInst::getDataType() const {
  if (auto *Shift = dyn_cast<ShiftRegShiftInst>(this))
    return Shift->getDataType();

  return cast<ShiftRegPeekInst>(this)->getType();
}

Value *ShiftRegShiftInst::getValueOperand() const { return getArgOperand(0); }
Value *ShiftRegShiftInst::getPointerOperand() const { return getArgOperand(1); }
Value *ShiftRegShiftInst::getPredicate() const { return getArgOperand(2); }
Value *ShiftRegPeekInst::getPointerOperand() const { return getArgOperand(0); }
Value *ShiftRegPeekInst::getIndex() const { return getArgOperand(1); }

DirectiveScopeExit *
DirectiveScopeEntry::BuildDirectiveScope(ArrayRef<OperandBundleDef> ScopeAttrs,
                                         Instruction &Entry,
                                         Instruction &Exit) {
  IRBuilder<> Builder(&Entry);
  auto *ScopeEntry = llvm::Intrinsic::getDeclaration(
      Entry.getModule(), llvm::Intrinsic::directive_scope_entry);

  auto *Token = Builder.CreateCall(ScopeEntry, None, ScopeAttrs);

  Builder.SetInsertPoint(&Exit);
  auto *ScopeExit = llvm::Intrinsic::getDeclaration(
      Entry.getModule(), llvm::Intrinsic::directive_scope_exit);
  return cast<DirectiveScopeExit>(Builder.CreateCall(ScopeExit, Token));
}

DirectiveScopeExit *DirectiveScopeEntry::BuildDirectiveScope(
    StringRef Tag, ArrayRef<Value *> Operands, Instruction &Entry,
    Instruction &Exit) {
  SmallVector<OperandBundleDef, 4> ScopeAttrs;
  ScopeAttrs.emplace_back(Tag, Operands);
  return BuildDirectiveScope(ScopeAttrs, Entry, Exit);
}

// Return true if this pragma should be applied on variable declaration site.
bool PragmaInst::ShouldBeOnDeclaration() {
  if (isa<DisaggrInst>(this) || isa<AggregateInst>(this) ||
      isa<ArrayPartitionInst>(this) || isa<ArrayReshapeInst>(this) ||
      isa<StreamPragmaInst>(this) || isa<PipoPragmaInst>(this) ||
      isa<BindStoragePragmaInst>(this)) {
    return true;
  } else {
    // Use assert to make sure we cover all pragmas on variables.
    assert((isa<DependenceInst>(this) || isa<StableInst>(this) ||
            isa<StableContentInst>(this) || isa<SharedInst>(this) ||
            isa<BindOpPragmaInst>(this) || isa<ConstSpecInst>(this) ||
            isa<CrossDependenceInst>(this) || isa<SAXIInst>(this)) ||
            isa<MaxiInst>(this) || isa<AxiSInst>(this) || 
            isa<ApFifoInst>(this) || isa<ApMemoryInst>(this) ||
            isa<BRAMInst>(this) || isa<ApStableInst>(this) ||
            isa<ApNoneInst>(this) || isa<ApAckInst>(this) ||
            isa<ApVldInst>(this) || isa<ApOvldInst>(this) ||
            isa<ApHsInst>(this)  &&
           "Unexpected pragma");
    return false;
  }
}

Value *PragmaInst::getVariable() const {
  if (const DependenceInst *DepInst = dyn_cast<DependenceInst>(this))
    return DepInst->getVariable();
  assert(getNumOperandBundles() == 1 &&
         "PragmaInst is invalid and its bundle num should be 1");
  OperandBundleUse Bundle = getOperandBundleAt(0);
  return Bundle.Inputs[0];
}

bool InterfaceInst::classof(const PragmaInst *I) {
  return (isa<SAXIInst>(I)
       || isa<MaxiInst>(I)
       || isa<AxiSInst>(I)
       || isa<ApFifoInst>(I)
       || isa<ApMemoryInst>(I)
       || isa<BRAMInst>(I)
       || isa<ApStableInst>(I)
       || isa<ApNoneInst>(I)
       || isa<ApAckInst>(I)
       || isa<ApVldInst>(I)
       || isa<ApOvldInst>(I)
       || isa<ApHsInst>(I));
}


const std::string DependenceInst::BundleTagName = "fpga.dependence";
const std::string CrossDependenceInst::BundleTagName = "fpga.cross.dependence";
const std::string StableInst::BundleTagName = "stable";
const std::string StableContentInst::BundleTagName = "stable_content";
const std::string SharedInst::BundleTagName = "shared";
const std::string DisaggrInst::BundleTagName = "disaggr";
const std::string AggregateInst::BundleTagName = "aggregate";
const std::string ArrayPartitionInst::BundleTagName = "xlx_array_partition";
const std::string ArrayReshapeInst::BundleTagName = "xlx_array_reshape";
const std::string StreamPragmaInst::BundleTagName = "xlx_reqd_pipe_depth";
const std::string PipoPragmaInst::BundleTagName = "xcl_fpga_pipo_depth";
const std::string BindStoragePragmaInst::BundleTagName = "xlx_bind_storage";
const std::string BindOpPragmaInst::BundleTagName = "xlx_bind_op";
const std::string ConstSpecInst::BundleTagName = "const";
const std::string FPGAResourceLimitInst::BundleTagName = "fpga_resource_limit_hint";
const std::string XlxFunctionAllocationInst::BundleTagName = "xlx_function_allocation";
const std::string SAXIInst::BundleTagName = "xlx_s_axilite";
const std::string MaxiInst::BundleTagName = "xlx_m_axi";
const std::string AxiSInst::BundleTagName = "xlx_axis";
const std::string ApFifoInst::BundleTagName = "xlx_ap_fifo";
const std::string ApMemoryInst::BundleTagName = "xlx_ap_memory";
const std::string BRAMInst::BundleTagName = "xlx_bram";
const std::string ApStableInst::BundleTagName = "xlx_ap_stable";
const std::string ApNoneInst::BundleTagName = "xlx_ap_none";
const std::string ApAckInst::BundleTagName = "xlx_ap_ack";
const std::string ApVldInst::BundleTagName = "xlx_ap_vld";
const std::string ApOvldInst::BundleTagName = "xlx_ap_ovld";
const std::string ApHsInst::BundleTagName = "xlx_ap_hs";

