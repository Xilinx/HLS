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
// This file defines classes that make it really easy to deal with intrinsic
// functions in FPGA with the isa/dyncast family of functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_XILINXFPGAINTRINSICINST_H
#define LLVM_IR_XILINXFPGAINTRINSICINST_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/XILINXFPGAPlatformBasic.h"

namespace llvm {

/// This represents the fpga_bit_concat
class BitConcatInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_bit_concat;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// getType - Overload to return most specific vector type.
  ///
  IntegerType *getType() const {
    return cast<IntegerType>(IntrinsicInst::getType());
  }

  /// \brief Return the element at [Hi, Lo]
  Value *getBits(unsigned Hi, unsigned Lo) const;

  /// \brief Return the element at [Hi, Lo], without the bitcast
  Value *getElement(unsigned Hi, unsigned Lo) const;
};

/// This represent the fpga_part_select
class PartSelectInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_part_select;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// \brief Return the source from which we select part, without bitcast
  Value *getRawSrc() const;

  /// \brief Return the source from which we select part
  Value *getSrc() const;
};

/// This represent the fpga_legacy_part_select
class LegacyPartSelectInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_legacy_part_select;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// \brief Return the source from which we select part
  Value *getSrc() const;

  /// \brief Return the type of source from which we select part
  Type *getSrcTy() const;

  /// \brief Return the Lo from which we select part
  Value *getLo() const;

  /// \brief Return the Hi from which we select part
  Value *getHi() const;
};

/// This represents the fpga_unpack_none
class UnpackNoneInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_unpack_none;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  IntegerType *getSrcType() const {
    return cast<IntegerType>(getOperand()->getType());
  }

  Value *getOperand() const;
};

/// This represents the fpga_pack_none
class PackNoneInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_pack_none;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// getType - Overload to return most specific vector type.
  ///
  IntegerType *getType() const {
    return cast<IntegerType>(IntrinsicInst::getType());
  }

  Type *getSrcType() const { return getOperand()->getType(); }

  Value *getOperand() const;
};

/// This represents the fpga_unpack_bits
class UnpackBitsInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_unpack_bits;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  IntegerType *getSrcType() const {
    return cast<IntegerType>(getOperand()->getType());
  }

  Value *getOperand() const;
};

/// This represents the fpga_pack_bits
class PackBitsInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_pack_bits;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// getType - Overload to return most specific vector type.
  ///
  IntegerType *getType() const {
    return cast<IntegerType>(IntrinsicInst::getType());
  }

  Type *getSrcType() const { return getOperand()->getType(); }

  Value *getOperand() const;
};

/// This represents the fpga_unpack_bytes
class UnpackBytesInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_unpack_bytes;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  IntegerType *getSrcType() const {
    return cast<IntegerType>(getOperand()->getType());
  }

  Value *getOperand() const;
};

/// This represents the fpga_pack_bytes
class PackBytesInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_pack_bytes;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// getType - Overload to return most specific vector type.
  ///
  IntegerType *getType() const {
    return cast<IntegerType>(IntrinsicInst::getType());
  }

  Type *getSrcType() const { return getOperand()->getType(); }

  Value *getOperand() const;
};

/// This represent the fpga_seq_[load|store]_[begin|end]
class SeqBeginInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return is_one_value_of(I->getIntrinsicID(), Intrinsic::fpga_seq_load_begin,
                           Intrinsic::fpga_seq_store_begin);
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  bool isLoad() const {
    return getIntrinsicID() == Intrinsic::fpga_seq_load_begin;
  }

  bool isStore() const {
    return getIntrinsicID() == Intrinsic::fpga_seq_store_begin;
  }

  unsigned getPointerAddressSpace() const {
    return getPointerOperand()->getType()->getPointerAddressSpace();
  }

  Value *getPointerOperand() const;
  Value *getSize() const { return getArgOperand(1); }
  uint64_t getSmallConstantSize() const;
  uint64_t getSmallConstantSizeInBytes(const DataLayout &DL) const;

  PointerType *getPointerType() const;
  Type *getDataType() const { return getPointerType()->getElementType(); }
  Type *getSizeType() const { return getSize()->getType(); }

  void updatePointer(Value *V);
  void updateSize(Value *V);
};

class SeqEndInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return is_one_value_of(I->getIntrinsicID(), Intrinsic::fpga_seq_load_end,
                           Intrinsic::fpga_seq_store_end);
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  bool isLoad() const {
    return getIntrinsicID() == Intrinsic::fpga_seq_load_end;
  }

  bool isStore() const {
    return getIntrinsicID() == Intrinsic::fpga_seq_store_end;
  }

  SeqBeginInst *getPointerOperand() const { return getBegin(); }
  SeqBeginInst *getBegin() const;
  Value *getSize() const { return getArgOperand(1); }
  Type *getSizeType() const { return getSize()->getType(); }
  void updateSize(Value *V);
};

class SeqAccessInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return is_one_value_of(I->getIntrinsicID(), Intrinsic::fpga_seq_load,
                           Intrinsic::fpga_seq_store);
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  bool isLoad() const { return getIntrinsicID() == Intrinsic::fpga_seq_load; }

  bool isStore() const { return getIntrinsicID() == Intrinsic::fpga_seq_store; }

  Type *getDataType() const;
  SeqBeginInst *getPointerOperand() const;
  Value *getIndex() const;
  unsigned getPointerAddressSpace() const {
    return getPointerOperand()->getType()->getPointerAddressSpace();
  }

  void updateIndex(Value *V);
};

class ShiftRegInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return is_one_value_of(I->getIntrinsicID(),
                           Intrinsic::fpga_shift_register_peek,
                           Intrinsic::fpga_shift_register_shift);
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  bool isShift() const {
    return getIntrinsicID() == Intrinsic::fpga_shift_register_shift;
  }

  bool isPeek() const {
    return getIntrinsicID() == Intrinsic::fpga_shift_register_peek;
  }

  Value *getPointerOperand() const;
  unsigned getPointerAddressSpace() const {
    return getPointerOperand()->getType()->getPointerAddressSpace();
  }

  Type *getDataType() const;
};

class ShiftRegPeekInst : public ShiftRegInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_shift_register_peek;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  Value *getPointerOperand() const;
  Value *getIndex() const;
};

class ShiftRegShiftInst : public ShiftRegInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_shift_register_shift;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  Value *getValueOperand() const;
  Value *getPointerOperand() const;
  Value *getPredicate() const;
  Type *getDataType() const { return getValueOperand()->getType(); }
};

class SeqLoadInst : public SeqAccessInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_seq_load;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  Type *getDataType() const { return getType(); }
  SeqBeginInst *getPointerOperand() const;
  Value *getIndex() const { return getArgOperand(1); }
  void updateIndex(Value *V);
};

class SeqStoreInst : public SeqAccessInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_seq_store;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  Value *getValueOperand() const;
  Type *getDataType() const { return getValueOperand()->getType(); }
  Value *getByteEnable() const;
  IntegerType *getByteEnableType() const {
    return cast<IntegerType>(getByteEnable()->getType());
  }
  bool isMasked() const;
  SeqBeginInst *getPointerOperand() const;
  Value *getIndex() const { return getArgOperand(2); }
  void updateIndex(Value *V);
};

/// This represent the assume
class AssumeInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::assume;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represent the fpga_part_select
class PartSetInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_part_set;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represent the fpga_legacy_part_set
class LegacyPartSetInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_legacy_part_set;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// \brief Return the source from which we set part
  Value *getSrc() const;

  /// \brief Return the type of source from which we set part
  Type *getSrcTy() const;

  /// \brief Return the Rep from which we set part
  Value *getRep() const;

  /// \brief Return the type of Rep from which we set part
  Type *getRepTy() const;

  /// \brief Return the Lo from which we set part
  Value *getLo() const;

  /// \brief Return the Hi from which we set part
  Value *getHi() const;
};

class FPGALoadStoreInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_maxi_load ||
           I->getIntrinsicID() == Intrinsic::fpga_maxi_store ||
           I->getIntrinsicID() == Intrinsic::fpga_bram_load ||
           I->getIntrinsicID() == Intrinsic::fpga_bram_store;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  Value *getPointerOperand();
  const Value *getPointerOperand() const;

  /// \brief Returns the address space of the pointer operand.
  unsigned getPointerAddressSpace() const;
  PointerType *getPointerType() const;
  Type *getDataType() const;
};

class FPGALoadInst : public FPGALoadStoreInst {
public:
  static inline bool classof(const FPGALoadStoreInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_maxi_load ||
           I->getIntrinsicID() == Intrinsic::fpga_bram_load;
  }

  static inline bool classof(const Value *V) {
    return isa<FPGALoadStoreInst>(V) && classof(cast<FPGALoadStoreInst>(V));
  }

  Value *getPointerOperand();
  const Value *getPointerOperand() const;
};

class FPGAStoreInst : public FPGALoadStoreInst {
public:
  static inline bool classof(const FPGALoadStoreInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_maxi_store ||
           I->getIntrinsicID() == Intrinsic::fpga_bram_store;
  }

  static inline bool classof(const Value *V) {
    return isa<FPGALoadStoreInst>(V) && classof(cast<FPGALoadStoreInst>(V));
  }

  Value *getValueOperand() { return getArgOperand(0); }
  const Value *getValueOperand() const { return getArgOperand(0); }

  Value *getPointerOperand();
  const Value *getPointerOperand() const;

  Value *getByteEnable() { return getArgOperand(2); }
  const Value *getByteEnable() const { return getArgOperand(2); }
};

struct MAXILoadInst : public FPGALoadInst {
  static inline bool classof(const FPGALoadInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_maxi_load;
  }

  static inline bool classof(const Value *V) {
    return isa<FPGALoadInst>(V) && classof(cast<FPGALoadInst>(V));
  }
};

struct BRAMLoadInst : public FPGALoadInst {
  static inline bool classof(const FPGALoadInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_bram_load;
  }

  static inline bool classof(const Value *V) {
    return isa<FPGALoadInst>(V) && classof(cast<FPGALoadInst>(V));
  }
};

struct MAXIStoreInst : public FPGAStoreInst {
  static inline bool classof(const FPGAStoreInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_maxi_store;
  }

  static inline bool classof(const Value *V) {
    return isa<FPGAStoreInst>(V) && classof(cast<FPGAStoreInst>(V));
  }
};

struct BRAMStoreInst : public FPGAStoreInst {
  static inline bool classof(const FPGAStoreInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_bram_store;
  }

  static inline bool classof(const Value *V) {
    return isa<FPGAStoreInst>(V) && classof(cast<FPGAStoreInst>(V));
  }
};

//===---
//
//  FIFO Intrinsics
//
//  Inheritance diagram (only leaves are actual instrinsics):
//
//  FPGAFIFOInst
//  |- FPGAFIFOStatusInst
//  |  |- FPGAFIFONotEmptyInst
//  |  `- FPGAFIFONotFullInst
//  |- FPGAFIFOBlockingInst
//  |  |- FPGAFIFOPopInst
//  |  `- FPGAFIFOPushInst
//  `- FPGAFIFONonBlockingInst
//     |- FPGAFIFONbPopInst
//     `- FPGAFIFONbPushInst
//
//===---

class FPGAFIFOInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_fifo_not_empty ||
           I->getIntrinsicID() == Intrinsic::fpga_fifo_not_full ||
           I->getIntrinsicID() == Intrinsic::fpga_fifo_pop ||
           I->getIntrinsicID() == Intrinsic::fpga_fifo_push ||
           I->getIntrinsicID() == Intrinsic::fpga_fifo_nb_pop ||
           I->getIntrinsicID() == Intrinsic::fpga_fifo_nb_push;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  // FIFO operand is always the last one
  Value *getFIFOOperand() { return getArgOperand(getNumArgOperands() - 1); }
  const Value *getFIFOOperand() const {
    return getArgOperand(getNumArgOperands() - 1);
  }

  PointerType *getFIFOType() const {
    return cast<PointerType>(getFIFOOperand()->getType());
  }
  Type *getDataType() const { return getFIFOType()->getElementType(); }

  bool isConsumerSide() const {
    switch (getIntrinsicID()) {
    case Intrinsic::fpga_fifo_not_empty:
    case Intrinsic::fpga_fifo_pop:
    case Intrinsic::fpga_fifo_nb_pop:
      return true;
    case Intrinsic::fpga_fifo_not_full:
    case Intrinsic::fpga_fifo_push:
    case Intrinsic::fpga_fifo_nb_push:
      return false;
    default:
      llvm_unreachable("Forgot to handle a FIFO intrinsic?");
    }
  }
  bool isProducerSide() const { return !isConsumerSide(); }
};

class FPGAFIFOStatusInst : public FPGAFIFOInst {
public:
  static inline bool classof(const FPGAFIFOInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_fifo_not_empty ||
           I->getIntrinsicID() == Intrinsic::fpga_fifo_not_full;
  }

  static inline bool classof(const Value *V) {
    return isa<FPGAFIFOInst>(V) && classof(cast<FPGAFIFOInst>(V));
  }
};

class FPGAFIFONotEmptyInst : public FPGAFIFOStatusInst {
public:
  static inline bool classof(const FPGAFIFOStatusInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_fifo_not_empty;
  }

  static inline bool classof(const Value *V) {
    return isa<FPGAFIFOStatusInst>(V) && classof(cast<FPGAFIFOStatusInst>(V));
  }
};

class FPGAFIFONotFullInst : public FPGAFIFOStatusInst {
public:
  static inline bool classof(const FPGAFIFOStatusInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_fifo_not_full;
  }

  static inline bool classof(const Value *V) {
    return isa<FPGAFIFOStatusInst>(V) && classof(cast<FPGAFIFOStatusInst>(V));
  }
};

class FPGAFIFOBlockingInst : public FPGAFIFOInst {
public:
  static inline bool classof(const FPGAFIFOInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_fifo_pop ||
           I->getIntrinsicID() == Intrinsic::fpga_fifo_push;
  }

  static inline bool classof(const Value *V) {
    return isa<FPGAFIFOInst>(V) && classof(cast<FPGAFIFOInst>(V));
  }
};

class FPGAFIFOPopInst : public FPGAFIFOBlockingInst {
public:
  static inline bool classof(const FPGAFIFOBlockingInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_fifo_pop;
  }

  static inline bool classof(const Value *V) {
    return isa<FPGAFIFOBlockingInst>(V) &&
           classof(cast<FPGAFIFOBlockingInst>(V));
  }
};

class FPGAFIFOPushInst : public FPGAFIFOBlockingInst {
public:
  static inline bool classof(const FPGAFIFOBlockingInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_fifo_push;
  }

  static inline bool classof(const Value *V) {
    return isa<FPGAFIFOBlockingInst>(V) &&
           classof(cast<FPGAFIFOBlockingInst>(V));
  }

  // Push take the value as first argument, lets provide a helper
  Value *getValueOperand() { return getArgOperand(0); }
  const Value *getValueOperand() const { return getArgOperand(0); }
};

class FPGAFIFONonBlockingInst : public FPGAFIFOInst {
public:
  static inline bool classof(const FPGAFIFOInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_fifo_nb_pop ||
           I->getIntrinsicID() == Intrinsic::fpga_fifo_nb_push;
  }

  static inline bool classof(const Value *V) {
    return isa<FPGAFIFOInst>(V) && classof(cast<FPGAFIFOInst>(V));
  }
};

class FPGAFIFONbPopInst : public FPGAFIFONonBlockingInst {
public:
  static inline bool classof(const FPGAFIFONonBlockingInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_fifo_nb_pop;
  }

  static inline bool classof(const Value *V) {
    return isa<FPGAFIFONonBlockingInst>(V) &&
           classof(cast<FPGAFIFONonBlockingInst>(V));
  }

  // Non-blocking pop returns a struct { bool success; type_t value; }
  static unsigned getReturnedBoolIdx() { return 0; }
  static unsigned getReturnedValueIdx() { return 1; }
};

class FPGAFIFONbPushInst : public FPGAFIFONonBlockingInst {
public:
  static inline bool classof(const FPGAFIFONonBlockingInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_fifo_nb_push;
  }

  static inline bool classof(const Value *V) {
    return isa<FPGAFIFONonBlockingInst>(V) &&
           classof(cast<FPGAFIFONonBlockingInst>(V));
  }

  // Non-blocking push simply returns a bool (no need for helper)

  // Push take the value as first argument, lets provide a helper
  Value *getValueOperand() { return getArgOperand(0); }
  const Value *getValueOperand() const { return getArgOperand(0); }
};

//===---
//
//  AXIS related Intrinsics
//
//  Inheritance diagram (only leaves are actual instrinsics):
//
//  AXISIntrinsicInst
//  |- AXISStatusIntrinsic
//  |  |- AXISReadyIntrinsic
//  |  `- AXISValidIntrinsic
//  |- AXISOpIntrinsicInst
//     |- AXISReadIntrinsic
//     |  |- AXISBlockingReadInst
//     |  `- AXISNonBlockingReadInst
//     `- AXISWriteIntrinsic
//        |- AXISBlockingWriteInst
//        `- AXISNonBlockingWriteInst
//
//===---

class AXISIntrinsicInst : public IntrinsicInst {
public:
  static const unsigned NumChannels = 7;

  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_axis_pop ||
           I->getIntrinsicID() == Intrinsic::fpga_axis_nb_pop ||
           I->getIntrinsicID() == Intrinsic::fpga_axis_push ||
           I->getIntrinsicID() == Intrinsic::fpga_axis_nb_push ||
           I->getIntrinsicID() == Intrinsic::fpga_axis_valid ||
           I->getIntrinsicID() == Intrinsic::fpga_axis_ready;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  bool isConsumerSide() const {
    switch (getIntrinsicID()) {
    case Intrinsic::fpga_axis_valid:
    case Intrinsic::fpga_axis_pop:
    case Intrinsic::fpga_axis_nb_pop:
      return true;
    case Intrinsic::fpga_axis_ready:
    case Intrinsic::fpga_axis_push:
    case Intrinsic::fpga_axis_nb_push:
      return false;
    default:
      llvm_unreachable("Forgot to handle a FIFO intrinsic?");
    }
  }
  bool isProducerSide() const { return !isConsumerSide(); }
};

class AXISStatusIntrinsic : public AXISIntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_axis_ready ||
           I->getIntrinsicID() == Intrinsic::fpga_axis_valid;
  }

  static inline bool classof(const Value *V) {
    return isa<AXISIntrinsicInst>(V) && classof(cast<AXISIntrinsicInst>(V));
  }
};

class AXISReadyIntrinsic : public AXISStatusIntrinsic {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_axis_ready;
  }

  static inline bool classof(const Value *V) {
    return isa<AXISStatusIntrinsic>(V) && classof(cast<AXISStatusIntrinsic>(V));
  }
};

class AXISValidIntrinsic : public AXISStatusIntrinsic {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_axis_valid;
  }

  static inline bool classof(const Value *V) {
    return isa<AXISStatusIntrinsic>(V) && classof(cast<AXISStatusIntrinsic>(V));
  }
};

/// This represent the AXIS operation related intrinsics,
/// like Read/Write/NbRead/NbWrite.
class AXISOpIntrinsicInst : public AXISIntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_axis_pop ||
           I->getIntrinsicID() == Intrinsic::fpga_axis_nb_pop ||
           I->getIntrinsicID() == Intrinsic::fpga_axis_push ||
           I->getIntrinsicID() == Intrinsic::fpga_axis_nb_push;
  }

  static inline bool classof(const Value *V) {
    return isa<AXISIntrinsicInst>(V) && classof(cast<AXISIntrinsicInst>(V));
  }
};

class AXISReadIntrinsic : public AXISOpIntrinsicInst {
public:
  static inline bool classof(const AXISOpIntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_axis_pop ||
           I->getIntrinsicID() == Intrinsic::fpga_axis_nb_pop;
  }

  static inline bool classof(const Value *V) {
    return isa<AXISOpIntrinsicInst>(V) && classof(cast<AXISOpIntrinsicInst>(V));
  }
};

class AXISBlockingReadInst : public AXISReadIntrinsic {
public:
  static inline bool classof(const AXISReadIntrinsic *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_axis_pop;
  }

  static inline bool classof(const Value *V) {
    return isa<AXISReadIntrinsic>(V) && classof(cast<AXISReadIntrinsic>(V));
  }
};

class AXISNonBlockingReadInst : public AXISReadIntrinsic {
public:
  static inline bool classof(const AXISReadIntrinsic *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_axis_nb_pop;
  }

  static inline bool classof(const Value *V) {
    return isa<AXISReadIntrinsic>(V) && classof(cast<AXISReadIntrinsic>(V));
  }
};

class AXISWriteIntrinsic : public AXISOpIntrinsicInst {
public:
  static inline bool classof(const AXISOpIntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_axis_push ||
           I->getIntrinsicID() == Intrinsic::fpga_axis_nb_push;
  }

  static inline bool classof(const Value *V) {
    return isa<AXISOpIntrinsicInst>(V) && classof(cast<AXISOpIntrinsicInst>(V));
  }
};

class AXISBlockingWriteInst : public AXISWriteIntrinsic {
public:
  static inline bool classof(const AXISWriteIntrinsic *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_axis_push;
  }

  static inline bool classof(const Value *V) {
    return isa<AXISWriteIntrinsic>(V) && classof(cast<AXISWriteIntrinsic>(V));
  }
};

class AXISNonBlockingWriteInst : public AXISWriteIntrinsic {
public:
  static inline bool classof(const AXISWriteIntrinsic *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_axis_nb_push;
  }

  static inline bool classof(const Value *V) {
    return isa<AXISWriteIntrinsic>(V) && classof(cast<AXISWriteIntrinsic>(V));
  }
};

class MAXIIOInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_maxi_read_req ||
           I->getIntrinsicID() == Intrinsic::fpga_maxi_read ||
           I->getIntrinsicID() == Intrinsic::fpga_maxi_write_req ||
           I->getIntrinsicID() == Intrinsic::fpga_maxi_write ||
           I->getIntrinsicID() == Intrinsic::fpga_maxi_write_resp;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  bool isReadIO() const {
    return getIntrinsicID() == Intrinsic::fpga_maxi_read_req ||
           getIntrinsicID() == Intrinsic::fpga_maxi_read;
  }

  Value *getPointerOperand() {
    return getIntrinsicID() == Intrinsic::fpga_maxi_write ? getArgOperand(1)
                                                          : getArgOperand(0);
  }

  const Value *getPointerOperand() const {
    return getIntrinsicID() == Intrinsic::fpga_maxi_write ? getArgOperand(1)
                                                          : getArgOperand(0);
  }

  PointerType *getPointerType() const {
    return cast<PointerType>(getPointerOperand()->getType());
  }

  Type *getDataType() const { return getPointerType()->getElementType(); }
};

class MAXIReadReqInst : public MAXIIOInst {
public:
  static inline bool classof(const MAXIIOInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_maxi_read_req;
  }

  static inline bool classof(const Value *V) {
    return isa<MAXIIOInst>(V) && classof(cast<MAXIIOInst>(V));
  }

  Value *getLength() { return getArgOperand(1); }
  const Value *getLength() const { return getArgOperand(1); }
};

class MAXIReadInst : public MAXIIOInst {
public:
  static inline bool classof(const MAXIIOInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_maxi_read;
  }

  static inline bool classof(const Value *V) {
    return isa<MAXIIOInst>(V) && classof(cast<MAXIIOInst>(V));
  }
};

class MAXIWriteReqInst : public MAXIIOInst {
public:
  static inline bool classof(const MAXIIOInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_maxi_write_req;
  }

  static inline bool classof(const Value *V) {
    return isa<MAXIIOInst>(V) && classof(cast<MAXIIOInst>(V));
  }

  Value *getLength() { return getArgOperand(1); }
  const Value *getLength() const { return getArgOperand(1); }
};

class MAXIWriteInst : public MAXIIOInst {
public:
  static inline bool classof(const MAXIIOInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_maxi_write;
  }

  static inline bool classof(const Value *V) {
    return isa<MAXIIOInst>(V) && classof(cast<MAXIIOInst>(V));
  }

  Value *getValueOperand() { return getArgOperand(0); }
  const Value *getValueOperand() const { return getArgOperand(0); }

  Value *getByteEnable() { return getArgOperand(2); }
  const Value *getByteEnable() const { return getArgOperand(2); }
};

class MAXIWriteRespInst : public MAXIIOInst {
public:
  static inline bool classof(const MAXIIOInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_maxi_write_resp;
  }

  static inline bool classof(const Value *V) {
    return isa<MAXIIOInst>(V) && classof(cast<MAXIIOInst>(V));
  }
};

//===---
//
//  Directive Scope
//
//===---

class DirectiveScopeExit;
class DirectiveScopeEntry : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::directive_scope_entry;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  static DirectiveScopeExit *
  BuildDirectiveScope(ArrayRef<OperandBundleDef> ScopeAttrs, Instruction &Entry,
                      Instruction &Exit);

  static DirectiveScopeExit *BuildDirectiveScope(StringRef Tag,
                                                 ArrayRef<Value *> Operands,
                                                 Instruction &Entry,
                                                 Instruction &Exit);
};

class DirectiveScopeExit : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::directive_scope_exit;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  DirectiveScopeEntry *getEntry() const {
    return cast<DirectiveScopeEntry>(getArgOperand(0));
  }
};

#define DEFINE_DIRECTIVE_SCOPE(Name, Tag)                                      \
  class Name##Exit;                                                            \
  class Name##Entry : public DirectiveScopeEntry {                             \
  public:                                                                      \
    static inline bool classof(const DirectiveScopeEntry *I) {                 \
      return I->getOperandBundle(#Tag) != None;                                \
    }                                                                          \
                                                                               \
    static inline bool classof(const Value *V) {                               \
      return isa<DirectiveScopeEntry>(V) &&                                    \
             classof(cast<DirectiveScopeEntry>(V));                            \
    }                                                                          \
                                                                               \
    ArrayRef<Use> getScopeAttrs() const {                                      \
      return getOperandBundle(#Tag).getValue().Inputs;                         \
    }                                                                          \
    template <typename T> T *getScopeAttrs(unsigned i) const {                 \
      auto Attr = getScopeAttrs();                                             \
      if (Attr.size() <= i)                                                    \
        return nullptr;                                                        \
      return dyn_cast<T>(Attr[i]);                                             \
    }                                                                          \
                                                                               \
    static Name##Exit *Build##Name(Instruction &Entry, Instruction &Exit,      \
                                   ArrayRef<Value *> Operands = None) {        \
      return cast<Name##Exit>(DirectiveScopeEntry::BuildDirectiveScope(        \
          #Tag, Operands, Entry, Exit));                                       \
    }                                                                          \
    static bool compatible(const OperandBundleDef &D) {                        \
      return D.getTag() == #Tag;                                               \
    }                                                                          \
    static const char *tag() { return #Tag; }                                  \
  };                                                                           \
                                                                               \
  class Name##Exit : public DirectiveScopeExit {                               \
  public:                                                                      \
    static inline bool classof(const DirectiveScopeExit *I) {                  \
      return isa<Name##Entry>(I->getEntry());                                  \
    }                                                                          \
                                                                               \
    static inline bool classof(const Value *V) {                               \
      return isa<DirectiveScopeExit>(V) &&                                     \
             classof(cast<DirectiveScopeExit>(V));                             \
    }                                                                          \
                                                                               \
    Name##Entry *getEntry() const {                                            \
      return cast<Name##Entry>(DirectiveScopeExit::getEntry());                \
    }                                                                          \
  };

DEFINE_DIRECTIVE_SCOPE(SingleWorkItem, xcl_single_workitem)
DEFINE_DIRECTIVE_SCOPE(UnrollWorkItem, xcl_unroll_workitems)
DEFINE_DIRECTIVE_SCOPE(PipelineWorkItem, xcl_pipeline_workitems)
DEFINE_DIRECTIVE_SCOPE(ImplicitBarrier, implicit_barrier)

DEFINE_DIRECTIVE_SCOPE(PipelineStage, pipeline_stage)
DEFINE_DIRECTIVE_SCOPE(OutlineRegion, xcl_outline)
DEFINE_DIRECTIVE_SCOPE(LatencyRegion, xcl_latency)
DEFINE_DIRECTIVE_SCOPE(ExprBalanceRegion, xlx_expr_balance)
DEFINE_DIRECTIVE_SCOPE(InlineRegion, xcl_inline)
DEFINE_DIRECTIVE_SCOPE(OccurrenceRegion, xlx_occurrence)
DEFINE_DIRECTIVE_SCOPE(ProtocolRegion, xlx_protocol)
DEFINE_DIRECTIVE_SCOPE(LoopMergeRegion, xlx_merge_loop)

DEFINE_DIRECTIVE_SCOPE(ResourceRegion, fpga_resource_hint)
DEFINE_DIRECTIVE_SCOPE(ResourceLimitRegion, fpga_resource_limit_hint)
DEFINE_DIRECTIVE_SCOPE(XlxFunctionAllocationRegion, xlx_function_allocation)
DEFINE_DIRECTIVE_SCOPE(ComputeRegion, fpga_compute_region)

class SSACopyInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::ssa_copy;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  Value *getValue() const { return getArgOperand(0); }
  void fold() { replaceAllUsesWith(getValue()); }
};

#define DEFINE_SSA_ATTRIBUTE(Name, Tag)                                        \
  class Name##Attr : public SSACopyInst {                                      \
  public:                                                                      \
    static inline bool classof(const SSACopyInst *I) {                         \
      return I->getOperandBundle(#Tag) != None;                                \
    }                                                                          \
                                                                               \
    static inline bool classof(const Value *V) {                               \
      return isa<SSACopyInst>(V) && classof(cast<SSACopyInst>(V));             \
    }                                                                          \
    ArrayRef<Use> getAttrs() const {                                           \
      return getOperandBundle(#Tag).getValue().Inputs;                         \
    }                                                                          \
    template <typename T> T *getAttrs(unsigned i) const {                      \
      auto Attr = getAttrs();                                                  \
      if (Attr.size() <= i)                                                    \
        return nullptr;                                                        \
      return dyn_cast<T>(Attr[i]);                                             \
    }                                                                          \
  };

DEFINE_SSA_ATTRIBUTE(ArrayGeometry, xcl_array_geometry)
DEFINE_SSA_ATTRIBUTE(ArrayView, xcl_array_view)
DEFINE_SSA_ATTRIBUTE(ReadOnly, xcl_read_only)
DEFINE_SSA_ATTRIBUTE(WriteOnly, xcl_write_only)

class SetStreamDepthInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::fpga_set_stream_depth;
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  int getDepth() const {
    ConstantInt *Depth = cast<ConstantInt>(getArgOperand(1));
    return (int)Depth->getSExtValue();
  }

  Value *getStreamObject() const { return getArgOperand(0); }
};

// Intrinsics for pragmas
class PragmaInst : public IntrinsicInst {
public:
  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::sideeffect &&
           I->hasOperandBundles();
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  // Create a PragmaInst and insert it before InsertBefore.
  // If InsertBefore is null, create PragmaInst but don't insert it. In this
  // case, Module M is necessary because we need it to get sideeffect
  // intrinsic function.
  template <typename PragmaInstType>
  static PragmaInstType *Create(ArrayRef<Value *> Options,
                                Instruction *InsertBefore = nullptr,
                                Module *M = nullptr) {
    Function *SideEffectF = Intrinsic::getDeclaration(
        M ? M : InsertBefore->getParent()->getParent()->getParent(),
        Intrinsic::sideeffect);

    CallInst *Call = CallInst::Create(
        SideEffectF, None,
        OperandBundleDef(PragmaInstType::BundleTagName, Options), "",
        InsertBefore);
    Call->setOnlyAccessesInaccessibleMemory();
    Call->setDoesNotThrow();
    if (InsertBefore)
      Call->setDebugLoc(InsertBefore->getDebugLoc());
    return cast<PragmaInstType>(Call);
  }

  template <typename PragmaInstType>
  static PragmaInstType *Create(ArrayRef<Value *> Options,
                                BasicBlock *InsertAtEnd) {
    Function *SideEffectF = Intrinsic::getDeclaration(
        InsertAtEnd->getParent()->getParent(), Intrinsic::sideeffect);

    CallInst *Call = CallInst::Create(
        SideEffectF, None,
        OperandBundleDef(PragmaInstType::BundleTagName, Options), "",
        InsertAtEnd);
    Call->setOnlyAccessesInaccessibleMemory();
    Call->setDoesNotThrow();
    return cast<PragmaInstType>(Call);
  }

  // Check every bundle operands to see if this Pragma is for
  // some specified Value.
  bool isForSpecifiedValue() {
    for (unsigned i = 0, e = getNumOperandBundles(); i != e; ++i) {
      OperandBundleUse B = getOperandBundleAt(i);
      for (const Value *V : B.Inputs) {
        if (!isa<Constant>(V) || V->getType()->isPointerTy())
          return true;
      }
    }
    return false;
  }

  Value *getVariable() const;

  static void getAllPramgas(Value *V, SmallPtrSetImpl<PragmaInst *> &PSet) {
    return get(V, PSet, true);
  }

  static void getAllPragmas(const Value *V,
                            SmallPtrSetImpl<const PragmaInst *> &PSet) {
    return get(V, PSet, true);
  }

  static void getDirectPramgas(Value *V, SmallPtrSetImpl<PragmaInst *> &PSet) {
    return get(V, PSet, false);
  }

  static void getDirectPragmas(const Value *V,
                            SmallPtrSetImpl<const PragmaInst *> &PSet) {
    return get(V, PSet, false);
  }

  static PragmaInst *getAnyPragma(Value *V) { return get<PragmaInst>(V, true); }

  static const PragmaInst *getAnyPragma(const Value *V) {
    return get<PragmaInst>(V, true);
  }

  // Return true if this pragma should be applied on variable declaration site.
  bool ShouldBeOnDeclaration();

protected:
  template <typename ValueT, typename PragmaInstType>
  static void get(ValueT *V, SmallPtrSetImpl<PragmaInstType *> &PSet,
                  bool PopulateGEP) {
    for (auto *U : V->users()) {
      if (auto *BC = dyn_cast<BitCastOperator>(U)) {
        get(BC, PSet, PopulateGEP);
      } else if (auto *GEP = dyn_cast<GEPOperator>(U)) {
        if (PopulateGEP)
          get(GEP, PSet, PopulateGEP);
      } else if (auto *PI = dyn_cast<PragmaInstType>(U)) {
        PSet.insert(PI);
      }
    }
  }

  template <typename PragmaInstType>
  static const PragmaInstType *get(const Value *V, bool PopulateGEP) {
    SmallPtrSet<const PragmaInstType *, 4> PSet;
    get(V, PSet, PopulateGEP);
    return PSet.empty() ? nullptr : *PSet.begin();
  }

  template <typename PragmaInstType>
  static PragmaInstType *get(Value *V, bool PopulateGEP) {
    SmallPtrSet<PragmaInstType *, 4> PSet;
    get(V, PSet, PopulateGEP);
    return PSet.empty() ? nullptr : *PSet.begin();
  }
};

class DependenceInst : public PragmaInst {
public:
  enum class Direction { NODIR = -1, RAW = 0, WAR = 1, WAW = 2 };
  enum class DepType { INTRA, INTER };

  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  void getOptions(SmallVectorImpl<Value *> &Options) {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal dependence intrinsic");
    for (auto &U : Bundle.getValue().Inputs) {
      Options.push_back(U);
    }
  }

  Value *getVariable() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal dependence intrinsic");
    Value *V = Bundle.getValue().Inputs[0];
    // A valid variable in dependence pragma must have pointer type
    return V->getType()->isPointerTy() ? V : nullptr;
  }

  bool isEnforced() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal dependence intrinsic");
    auto *isEnforced = cast<ConstantInt>(Bundle.getValue().Inputs[2]);
    return isEnforced->getZExtValue();
  }

  Direction getDirection() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal dependence intrinsic");
    auto *Dir = cast<ConstantInt>(Bundle.getValue().Inputs[3]);
    int64_t DirCode = Dir->getSExtValue();
    assert((DirCode >= -1) && (DirCode <= 2) &&
            "unexpected dependence pragma direction!");
    return static_cast<Direction>(DirCode);
  }

  int64_t getDistance() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal dependence intrinsic");
    return cast<ConstantInt>(Bundle.getValue().Inputs[4])->getSExtValue();
  }

  DepType getType() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal dependence intrinsic");
    auto *Ty = cast<ConstantInt>(Bundle.getValue().Inputs[5]);
    uint64_t DepTypeCode = Ty->getSExtValue();
    assert((DepTypeCode >= 0) && (DepTypeCode <= 1) &&
            "unexpected dependence pragma type!");
    return static_cast<DepType>(DepTypeCode);
  }

  static DependenceInst *get(Value *V) {
    return PragmaInst::get<DependenceInst>(V, true);
  }

  static const DependenceInst *get(const Value *V) {
    return PragmaInst::get<DependenceInst>(V, true);
  }
};

class CrossDependenceInst : public PragmaInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  void getOptions(SmallVectorImpl<Value *> &Options) {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal dependence intrinsic");
    for (auto &U : Bundle.getValue().Inputs) {
      Options.push_back(U);
    }
  }

  void getVariables(SmallVectorImpl<Value *> &Vars) const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal dependence intrinsic");
    for (Value *V : Bundle.getValue().Inputs) {
      if (V->getType()->isPointerTy())
        Vars.push_back(V);
    }
  }
};

class StableInst : public PragmaInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  static inline unsigned getConstValueNum() { return 0; }

  void getStables(SmallVectorImpl<Value *> &Stables) {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal stable intrinsic");
    for (auto &U : Bundle.getValue().Inputs) {
      Stables.push_back(U);
    }
  }
};

class StableContentInst : public PragmaInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  static inline unsigned getConstValueNum() { return 0; }

  void getStableContents(SmallVectorImpl<Value *> &StableContents) {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal stable content intrinsic");
    for (auto &U : Bundle.getValue().Inputs) {
      StableContents.push_back(U);
    }
  }
};

class SharedInst : public PragmaInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  void getSharedVals(SmallVectorImpl<Value *> &SharedVals) {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal shared intrinsic");
    for (auto &U : Bundle.getValue().Inputs) {
      SharedVals.push_back(U);
    }
  }

  static SharedInst *get(Value *V) {
    return PragmaInst::get<SharedInst>(V, true);
  }

  static const SharedInst *get(const Value *V) {
    return PragmaInst::get<SharedInst>(V, true);
  }
};

class DisaggrInst : public PragmaInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  void getDisaggrVals(SmallVectorImpl<Value *> &DisaggrVals) {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal disaggregate intrinsic");
    for (auto &U : Bundle.getValue().Inputs) {
      DisaggrVals.push_back(U);
    }
  }

  static DisaggrInst *get(Value *V) {
    return PragmaInst::get<DisaggrInst>(V, true);
  }

  static const DisaggrInst *get(const Value *V) {
    return PragmaInst::get<DisaggrInst>(V, true);
  }
};

class AggregateInst : public PragmaInst {
public:
  static const std::string BundleTagName;

public:
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  Value* getVariable() {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal aggregate intrinsic");
    return Bundle.getValue().Inputs[0];
  }
  // 0: none
  // 1: bit
  // 2: byte
  // 3: default
  int64_t getCompact() { 
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal aggregate intrinsic");
    if (Bundle.getValue().Inputs.size() <= 1)
      return 3; // default mode
    assert(isa<ConstantInt>(Bundle.getValue().Inputs[1]));
    return cast<ConstantInt>(Bundle.getValue().Inputs[1])->getSExtValue();
  }

  static AggregateInst *get(Value *V) {
    return PragmaInst::get<AggregateInst>(V, true);
  }

  static const AggregateInst *get(const Value *V) {
    return PragmaInst::get<AggregateInst>(V, true);
  }
};

// CRTP
// TODO: allow more than one variable in the same operand bundle
template <class SpecificXFromInst> class ArrayXFormInst : public PragmaInst {
public:
  enum XFormMode { Cyclic = 0, Block = 1, Complete = 2 };

  static inline bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::sideeffect &&
           I->getOperandBundle(SpecificXFromInst::BundleTagName);
  }

  static inline bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  static inline unsigned getConstValueNum() { return 3; }

  StringRef getMode() const {
    Optional<OperandBundleUse> Bundle =
        getOperandBundle(SpecificXFromInst::BundleTagName);
    assert(Bundle && "Illegal array transform intrinsic");
    ConstantInt *Type = cast<ConstantInt>(Bundle.getValue().Inputs[1]);
    switch (Type->getZExtValue()) {
    case Cyclic:
      return "cyclic";
    case Block:
      return "block";
    case Complete:
      return "complete";
    }
    llvm_unreachable("unexpected array transfrom type!");
    return "";
  }

  int getFactor() const {
    Optional<OperandBundleUse> Bundle =
        getOperandBundle(SpecificXFromInst::BundleTagName);
    assert(Bundle && "Illegal array transform intrinsic");
    ConstantInt *Factor = cast<ConstantInt>(Bundle.getValue().Inputs[2]);
    return Factor->getZExtValue();
  }

  int getDim() const {
    Optional<OperandBundleUse> Bundle =
        getOperandBundle(SpecificXFromInst::BundleTagName);
    assert(Bundle && "Illegal array transform intrinsic");
    ConstantInt *dim = cast<ConstantInt>(Bundle.getValue().Inputs[3]);
    return dim->getZExtValue();
  }
};

class ArrayPartitionInst : public ArrayXFormInst<ArrayPartitionInst> {
public:
  static const std::string BundleTagName;

  static void get(Value *V, SmallPtrSetImpl<ArrayPartitionInst *> &PSet) {
    return PragmaInst::get(V, PSet, true);
  }

  static void get(const Value *V,
                  SmallPtrSetImpl<const ArrayPartitionInst *> &PSet) {
    return PragmaInst::get(V, PSet, true);
  }

  static ArrayPartitionInst *get(Value *V) {
    return PragmaInst::get<ArrayPartitionInst>(V, true);
  }

  static const ArrayPartitionInst *get(const Value *V) {
    return PragmaInst::get<ArrayPartitionInst>(V, true);
  }
};

class ArrayReshapeInst : public ArrayXFormInst<ArrayReshapeInst> {
public:
  static const std::string BundleTagName;

  static void get(Value *V, SmallPtrSetImpl<ArrayReshapeInst *> &PSet) {
    return PragmaInst::get(V, PSet, true);
  }

  static void get(const Value *V,
                  SmallPtrSetImpl<const ArrayReshapeInst *> &PSet) {
    return PragmaInst::get(V, PSet, true);
  }

  static ArrayReshapeInst *get(Value *V) {
    return PragmaInst::get<ArrayReshapeInst>(V, true);
  }

  static const ArrayReshapeInst *get(const Value *V) {
    return PragmaInst::get<ArrayReshapeInst>(V, true);
  }
};

class StreamPragmaInst : public PragmaInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  static inline unsigned getConstValueNum() { return 1; }

  int32_t getDepth() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal aggregate intrinsic");
    ConstantInt *Depth = cast<ConstantInt>(Bundle.getValue().Inputs[1]);
    return (int32_t)Depth->getSExtValue();
  }

  Value *getStream() const { return getVariable(); }

  static void get(Value *V, SmallPtrSetImpl<StreamPragmaInst *> &PSet) {
    return PragmaInst::get(V, PSet, false);
  }

  static void get(const Value *V,
                  SmallPtrSetImpl<const StreamPragmaInst *> &PSet) {
    return PragmaInst::get(V, PSet, false);
  }

  static StreamPragmaInst *get(Value *V) {
    return PragmaInst::get<StreamPragmaInst>(V, false);
  }

  static const StreamPragmaInst *get(const Value *V) {
    return PragmaInst::get<StreamPragmaInst>(V, false);
  }
};

class PipoPragmaInst : public PragmaInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  static inline unsigned getConstValueNum() { return 1; }

  Value *getPipo() const { return getVariable(); }
  int getDepth() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal aggregate intrinsic");
    ConstantInt *Depth = cast<ConstantInt>(Bundle.getValue().Inputs[1]);
    return (int)Depth->getSExtValue();
  }

  static void get(Value *V, SmallPtrSetImpl<PipoPragmaInst *> &PSet) {
    return PragmaInst::get(V, PSet, false);
  }

  static void get(const Value *V,
                  SmallPtrSetImpl<const PipoPragmaInst *> &PSet) {
    return PragmaInst::get(V, PSet, false);
  }

  static PipoPragmaInst *get(Value *V) {
    return PragmaInst::get<PipoPragmaInst>(V, false);
  }

  static const PipoPragmaInst *get(const Value *V) {
    return PragmaInst::get<PipoPragmaInst>(V, false);
  }
};

class BindOpPragmaInst : public PragmaInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }
  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }
  static inline const BindOpPragmaInst *get(Value *V) {
    return PragmaInst::get<BindOpPragmaInst>(V, false);
  }

  static inline const BindOpPragmaInst *get(const Value *V) {
    return PragmaInst::get<BindOpPragmaInst>(V, false);
  }
  static void get(Value *V, SmallPtrSetImpl<BindOpPragmaInst *> &PSet) {
    return PragmaInst::get(V, PSet, false);
  }

  static void get(const Value *V,
                  SmallPtrSetImpl<const BindOpPragmaInst *> &PSet) {
    return PragmaInst::get(V, PSet, false);
  }

  static inline unsigned getConstValueNum() { return 3; }

  platform::PlatformBasic::OP_TYPE getOp() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal aggregate intrinsic");
    ConstantInt *Op = cast<ConstantInt>(Bundle.getValue().Inputs[1]);
    return (platform::PlatformBasic::OP_TYPE)Op->getSExtValue();
  }
  platform::PlatformBasic::IMPL_TYPE getImpl() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal aggregate intrinsic");
    ConstantInt *Impl = cast<ConstantInt>(Bundle.getValue().Inputs[2]);
    return (platform::PlatformBasic::IMPL_TYPE)Impl->getSExtValue();
  }
  int getLatency() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal aggregate intrinsic");
    ConstantInt *Latency = cast<ConstantInt>(Bundle.getValue().Inputs[3]);
    return Latency->getSExtValue();
  }
};

class BindStoragePragmaInst : public PragmaInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }
  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }
  static inline const BindStoragePragmaInst *get(Value *V) {
    return PragmaInst::get<BindStoragePragmaInst>(V, false);
  }

  static inline const BindStoragePragmaInst *get(const Value *V) {
    return PragmaInst::get<BindStoragePragmaInst>(V, false);
  }
  static void get(Value *V, SmallPtrSetImpl<BindStoragePragmaInst *> &PSet) {
    return PragmaInst::get(V, PSet, false);
  }

  static void get(const Value *V,
                  SmallPtrSetImpl<const BindStoragePragmaInst *> &PSet) {
    return PragmaInst::get(V, PSet, false);
  }

  static inline unsigned getConstValueNum() { return 3; }

  platform::PlatformBasic::OP_TYPE getOp() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal aggregate intrinsic");
    ConstantInt *Op = cast<ConstantInt>(Bundle.getValue().Inputs[1]);
    return (platform::PlatformBasic::OP_TYPE)Op->getSExtValue();
  }
  platform::PlatformBasic::IMPL_TYPE getImpl() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal aggregate intrinsic");
    ConstantInt *Impl = cast<ConstantInt>(Bundle.getValue().Inputs[2]);
    return (platform::PlatformBasic::IMPL_TYPE)Impl->getSExtValue();
  }
  int getLatency() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert(Bundle && "Illegal aggregate intrinsic");
    ConstantInt *Latency = cast<ConstantInt>(Bundle.getValue().Inputs[3]);
    return Latency->getSExtValue();
  }

  bool supportByteEnable() const {
    auto *XilinxPlatform = platform::PlatformBasic::getInstance();
    platform::PlatformBasic::CoreBasic *Core = 
        XilinxPlatform->getCoreFromOpImpl(getOp(), getImpl());
    return !Core || Core->supportByteEnable();
  }

  bool isInitializable() const {
    auto *XilinxPlatform = platform::PlatformBasic::getInstance();
    platform::PlatformBasic::CoreBasic *Core = 
        XilinxPlatform->getCoreFromOpImpl(getOp(), getImpl());
    return !Core || Core->isInitializable();
  }

  bool isInitializableByAllZeros() const {
    auto *XilinxPlatform = platform::PlatformBasic::getInstance();
    platform::PlatformBasic::CoreBasic *Core = 
        XilinxPlatform->getCoreFromOpImpl(getOp(), getImpl());
    return !Core || Core->isInitializableByAllZeros();
  }
};

class ConstSpecInst : public PragmaInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  static ConstSpecInst *get(Value *V) {
    return PragmaInst::get<ConstSpecInst>(V, false);
  }

  static const ConstSpecInst *get(const Value *V) {
    return PragmaInst::get<ConstSpecInst>(V, false);
  }
};
class FPGAResourceLimitInst: public PragmaInst{ 
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }
  StringRef getInstanceName() const{ 
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert( Bundle && "Illegal ConstSpec intrinsic" );
    StringRef instanceName = dyn_cast<ConstantDataArray>(Bundle.getValue().Inputs[0])->getAsString();
    return instanceName;
  }
  int getInstanceType() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert( Bundle && "Illegal ConstSpec intrinsic" );
    StringRef InstanceType = dyn_cast<ConstantDataArray>(Bundle.getValue().Inputs[1])->getAsString();
    if (InstanceType.equals_lower("operation")) { 
      return 0;
    }
    else if (InstanceType.equals_lower("core")) { 
      return 1;
    }
    else { 
      llvm_unreachable("unexpected Allocation type" );
    }
  }
  int getLimit() const { 
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert( Bundle && "Illegal ConstSpec intrinsic" );
    ConstantInt* limit = dyn_cast<ConstantInt>(Bundle.getValue().Inputs[2]);
    return limit->getSExtValue();
  }
};

class XlxFunctionAllocationInst: public PragmaInst { 
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }
  Function* getFunction() const{ 
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert( Bundle && "Illegal ConstSpec intrinsic" );
    Function* func = dyn_cast<Function>(Bundle.getValue().Inputs[0]);
    return func;
  }
  int getLimit() const { 
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    assert( Bundle && "Illegal ConstSpec intrinsic" );
    ConstantInt* limit = dyn_cast<ConstantInt>( Bundle.getValue().Inputs[2]);
    return limit->getSExtValue();
  }
};

////////////////////////////////////////
// Interface Intrinsic
////////////////////////////////////////
class InterfaceInst : public PragmaInst {
public:
  static bool classof(const PragmaInst *I);
  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }
};

class SAXIInst : public InterfaceInst {
public:
  static const std::string BundleTagName;
  static bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  StringRef getBundleName() const {
    if (!isValidInst())
      assert(0 && "Illegal s_axilite intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    Value *V = Bundle.getValue().Inputs[1];
    if (auto *GV = dyn_cast<GlobalVariable>(V))
      V = GV->getInitializer();
    if (auto *CDS = dyn_cast<ConstantDataSequential>(V))
      return CDS->getRawDataValues();
    return StringRef();
  }

  int64_t getOffset() const {
    if (!isValidInst())
      assert(0 && "Illegal s_axilite intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Offset = cast<ConstantInt>(Bundle.getValue().Inputs[2]);
    return Offset->getSExtValue();
  }

  bool hasRegister() const {
    if (!isValidInst())
      assert(0 && "Illegal s_axilite intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Reg = cast<ConstantInt>(Bundle.getValue().Inputs[3]);
    return Reg->isOne();
  }

  StringRef getSignalName() const {
    if (!isValidInst())
      assert(0 && "Illegal s_axilite intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    Value *V = Bundle.getValue().Inputs[4];
    if (isa<GlobalVariable>(V))
      V = cast<GlobalVariable>(V)->getInitializer();
    if (isa<ConstantAggregateZero>(V))
      return "";
    else
      return cast<ConstantDataSequential>(V)->getRawDataValues();
  }

private:
  unsigned getNumArgs() const {
    return 5;
  }

  bool isValidInst() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    if (Bundle && Bundle.getValue().Inputs.size() == getNumArgs())
      return true;
    else
      return false;
  }
};

class MaxiInst : public InterfaceInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  StringRef getBundleName() const {
    if (!isValidInst())
      assert(0 && "Illegal m_axi intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    Value *V = Bundle.getValue().Inputs[1];
    if (isa<GlobalVariable>(V))
      V = cast<GlobalVariable>(V)->getInitializer();
    if (isa<ConstantAggregateZero>(V))
      return "";
    else
      return cast<ConstantDataSequential>(V)->getRawDataValues();
  }

  int64_t getDepth() const {
    if (!isValidInst())
      assert(0 && "Illegal m_axi intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Depth = cast<ConstantInt>(Bundle.getValue().Inputs[2]);
    return Depth->getSExtValue();
  }

  StringRef getOffset() const {
    if (!isValidInst())
      assert(0 && "Illegal m_axi intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    Value *V = Bundle.getValue().Inputs[3];
    if (isa<GlobalVariable>(V))
      V = cast<GlobalVariable>(V)->getInitializer();
    if (isa<ConstantAggregateZero>(V))
      return "";
    else
      return cast<ConstantDataSequential>(V)->getRawDataValues();
  }

  StringRef getSignalName() const {
    if (!isValidInst())
      assert(0 && "Illegal m_axi intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    Value *V = Bundle.getValue().Inputs[4];
    if (isa<GlobalVariable>(V))
      V = cast<GlobalVariable>(V)->getInitializer();
    if (isa<ConstantAggregateZero>(V))
      return "";
    else
      return cast<ConstantDataSequential>(V)->getRawDataValues();
  }

  int64_t getNumReadOutstanding() const {
    if (!isValidInst())
      assert(0 && "Illegal m_axi intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *V = cast<ConstantInt>(Bundle.getValue().Inputs[5]);
    return V->getSExtValue();
  }

  int64_t getNumWriteOutstanding() const {
    if (!isValidInst())
      assert(0 && "Illegal m_axi intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *V = cast<ConstantInt>(Bundle.getValue().Inputs[6]);
    return V->getSExtValue();
  }

  int64_t getMaxReadBurstLen() const {
    if (!isValidInst())
      assert(0 && "Illegal m_axi intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *V = cast<ConstantInt>(Bundle.getValue().Inputs[7]);
    return V->getSExtValue();
  }

  int64_t getMaxWriteBurstLen() const {
    if (!isValidInst())
      assert(0 && "Illegal m_axi intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *V = cast<ConstantInt>(Bundle.getValue().Inputs[8]);
    return V->getSExtValue();
  }

  int64_t getLatency() const {
    if (!isValidInst())
      assert(0 && "Illegal m_axi intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *V = cast<ConstantInt>(Bundle.getValue().Inputs[9]);
    return V->getSExtValue();
  }

  int64_t getMaxWidenBitwidth() const {
    if (!isValidInst())
      assert(0 && "Illegal m_axi intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *V = cast<ConstantInt>(Bundle.getValue().Inputs[10]);
    return V->getSExtValue();
  }

private:
  unsigned getNumArgs() const {
    return 11;
  }

  bool isValidInst() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    if (Bundle && Bundle.getValue().Inputs.size() == getNumArgs())
      return true;
    else
      return false;
  }
};


class AxiSInst : public InterfaceInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  bool hasRegister() const {
    if (!isValidInst())
      assert(0 && "Illegal axis intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Reg = cast<ConstantInt>(Bundle.getValue().Inputs[1]);
    return Reg->isOne();
  }


  int64_t getRegisterMode() const {
    if (!isValidInst())
      assert(0 && "Illegal axis intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Depth = cast<ConstantInt>(Bundle.getValue().Inputs[2]);
    return Depth->getSExtValue();
  }

  int64_t getDepth() const {
    if (!isValidInst())
      assert(0 && "Illegal axis intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Depth = cast<ConstantInt>(Bundle.getValue().Inputs[3]);
    return Depth->getSExtValue();
  }


  StringRef getSignalName() const {
    if (!isValidInst())
      assert(0 && "Illegal axis intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    Value *V = Bundle.getValue().Inputs[4];
    if (isa<GlobalVariable>(V))
      V = cast<GlobalVariable>(V)->getInitializer();
    if (isa<ConstantAggregateZero>(V))
      return "";
    else
      return cast<ConstantDataSequential>(V)->getRawDataValues();
  }

private:
  unsigned getNumArgs() const {
    return 5;
  }

  bool isValidInst() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    if (Bundle && Bundle.getValue().Inputs.size() == getNumArgs())
      return true;
    else
      return false;
  }
};

class ApFifoInst : public InterfaceInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  bool hasRegister() const {
    if (!isValidInst())
      assert(0 && "Illegal ap_fifo intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Reg = cast<ConstantInt>(Bundle.getValue().Inputs[1]);
    return Reg->isOne();
  }

  StringRef getSignalName() const {
    if (!isValidInst())
      assert(0 && "Illegal ap_fifo intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    Value *V = Bundle.getValue().Inputs[2];
    if (isa<GlobalVariable>(V))
      V = cast<GlobalVariable>(V)->getInitializer();
    if (isa<ConstantAggregateZero>(V))
      return "";
    else
      return cast<ConstantDataSequential>(V)->getRawDataValues();
  }

  int64_t getDepth() const {
    if (!isValidInst())
      assert(0 && "Illegal ap_fifo intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Depth = cast<ConstantInt>(Bundle.getValue().Inputs[3]);
    return Depth->getSExtValue();
  }

private:
  unsigned getNumArgs() const {
    return 4;
  }

  bool isValidInst() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    if (Bundle && Bundle.getValue().Inputs.size() == getNumArgs())
      return true;
    else
      return false;
  }
};

class ApMemoryInst : public InterfaceInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  int64_t getStorageType() const {
    if (!isValidInst())
      assert(0 && "Illegal ap_memory intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Depth = cast<ConstantInt>(Bundle.getValue().Inputs[1]);
    return Depth->getSExtValue();
  }

  int64_t getImplType() const {
    if (!isValidInst())
      assert(0 && "Illegal ap_memory intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Depth = cast<ConstantInt>(Bundle.getValue().Inputs[2]);
    return Depth->getSExtValue();
  }

  int64_t getLatency() const {
    if (!isValidInst())
      assert(0 && "Illegal ap_memory intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Depth = cast<ConstantInt>(Bundle.getValue().Inputs[3]);
    return Depth->getSExtValue();
  }

  StringRef getSignalName() const {
    if (!isValidInst())
      assert(0 && "Illegal ap_memory intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    Value *V = Bundle.getValue().Inputs[4];
    if (isa<GlobalVariable>(V))
      V = cast<GlobalVariable>(V)->getInitializer();
    if (isa<ConstantAggregateZero>(V))
      return "";
    else
      return cast<ConstantDataSequential>(V)->getRawDataValues();
  }

private:
  unsigned getNumArgs() const {
    return 5;
  }

  bool isValidInst() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    if (Bundle && Bundle.getValue().Inputs.size() == getNumArgs())
      return true;
    else
      return false;
  }
};

class BRAMInst : public InterfaceInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  int64_t getStorageType() const {
    if (!isValidInst())
      assert(0 && "Illegal bram intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Depth = cast<ConstantInt>(Bundle.getValue().Inputs[1]);
    return Depth->getSExtValue();
  }

  int64_t getImplType() const {
    if (!isValidInst())
      assert(0 && "Illegal bram intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Depth = cast<ConstantInt>(Bundle.getValue().Inputs[2]);
    return Depth->getSExtValue();
  }

  int64_t getLatency() const {
    if (!isValidInst())
      assert(0 && "Illegal bram intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Depth = cast<ConstantInt>(Bundle.getValue().Inputs[3]);
    return Depth->getSExtValue();
  }

  StringRef getSignalName() const {
    if (!isValidInst())
      assert(0 && "Illegal bram intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    Value *V = Bundle.getValue().Inputs[4];
    if (isa<GlobalVariable>(V))
      V = cast<GlobalVariable>(V)->getInitializer();
    if (isa<ConstantAggregateZero>(V))
      return "";
    else
      return cast<ConstantDataSequential>(V)->getRawDataValues();
  }

private:
  unsigned getNumArgs() const {
    return 5;
  }

  bool isValidInst() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    if (Bundle && Bundle.getValue().Inputs.size() == getNumArgs())
      return true;
    else
      return false;
  }
};

class ApStableInst : public InterfaceInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  bool hasRegister() const {
    if (!isValidInst())
      assert(0 && "Illegal ap_stable intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Reg = cast<ConstantInt>(Bundle.getValue().Inputs[1]);
    return Reg->isOne();
  }

  StringRef getSignalName() const {
    if (!isValidInst())
      assert(0 && "Illegal ap_stable intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    Value *V = Bundle.getValue().Inputs[2];
    if (isa<GlobalVariable>(V))
      V = cast<GlobalVariable>(V)->getInitializer();
    if (isa<ConstantAggregateZero>(V))
      return "";
    else
      return cast<ConstantDataSequential>(V)->getRawDataValues();
  }

private:
  unsigned getNumArgs() const {
    return 3;
  }

  bool isValidInst() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    if (Bundle && Bundle.getValue().Inputs.size() == getNumArgs())
      return true;
    else
      return false;
  }
};

class ApNoneInst : public InterfaceInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  bool hasRegister() const {
    if (!isValidInst())
      assert(0 && "Illegal ap_none intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Reg = cast<ConstantInt>(Bundle.getValue().Inputs[1]);
    return Reg->isOne();
  }

  StringRef getSignalName() const {
    if (!isValidInst())
      assert(0 && "Illegal ap_none intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    Value *V = Bundle.getValue().Inputs[2];
    if (isa<GlobalVariable>(V))
      V = cast<GlobalVariable>(V)->getInitializer();
    if (isa<ConstantAggregateZero>(V))
      return "";
    else
      return cast<ConstantDataSequential>(V)->getRawDataValues();
  }

private:
  unsigned getNumArgs() const {
    return 3;
  }

  bool isValidInst() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    if (Bundle && Bundle.getValue().Inputs.size() == getNumArgs())
      return true;
    else
      return false;
  }
};

class ApAckInst : public InterfaceInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  bool hasRegister() const {
    if (!isValidInst())
      assert(0 && "Illegal ap_ack intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Reg = cast<ConstantInt>(Bundle.getValue().Inputs[1]);
    return Reg->isOne();
  }

  StringRef getSignalName() const {
    if (!isValidInst())
      assert(0 && "Illegal ap_ack intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    Value *V = Bundle.getValue().Inputs[2];
    if (isa<GlobalVariable>(V))
      V = cast<GlobalVariable>(V)->getInitializer();
    if (isa<ConstantAggregateZero>(V))
      return "";
    else
      return cast<ConstantDataSequential>(V)->getRawDataValues();
  }

private:
  unsigned getNumArgs() const {
    return 3;
  }

  bool isValidInst() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    if (Bundle && Bundle.getValue().Inputs.size() == getNumArgs())
      return true;
    else
      return false;
  }
};

class ApVldInst : public InterfaceInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  bool hasRegister() const {
    if (!isValidInst())
      assert(0 && "Illegal ap_vld intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Reg = cast<ConstantInt>(Bundle.getValue().Inputs[1]);
    return Reg->isOne();
  }

  StringRef getSignalName() const {
    if (!isValidInst())
      assert(0 && "Illegal ap_vld intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    Value *V = Bundle.getValue().Inputs[2];
    if (isa<GlobalVariable>(V))
      V = cast<GlobalVariable>(V)->getInitializer();
    if (isa<ConstantAggregateZero>(V))
      return "";
    else
      return cast<ConstantDataSequential>(V)->getRawDataValues();
  }

private:
  unsigned getNumArgs() const {
    return 3;
  }

  bool isValidInst() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    if (Bundle && Bundle.getValue().Inputs.size() == getNumArgs())
      return true;
    else
      return false;
  }
};

class ApOvldInst : public InterfaceInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  bool hasRegister() const {
    if (!isValidInst())
      assert(0 && "Illegal ap_ovld intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Reg = cast<ConstantInt>(Bundle.getValue().Inputs[1]);
    return Reg->isOne();
  }

  StringRef getSignalName() const {
    if (!isValidInst())
      assert(0 && "Illegal ap_ovld intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    Value *V = Bundle.getValue().Inputs[2];
    if (isa<GlobalVariable>(V))
      V = cast<GlobalVariable>(V)->getInitializer();
    if (isa<ConstantAggregateZero>(V))
      return "";
    else
      return cast<ConstantDataSequential>(V)->getRawDataValues();
  }

private:
  unsigned getNumArgs() const {
    return 3;
  }

  bool isValidInst() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    if (Bundle && Bundle.getValue().Inputs.size() == getNumArgs())
      return true;
    else
      return false;
  }
};

class ApHsInst : public InterfaceInst {
public:
  static const std::string BundleTagName;
  static inline bool classof(const PragmaInst *I) {
    return I->getOperandBundle(BundleTagName).hasValue();
  }

  static inline bool classof(const Value *V) {
    return isa<PragmaInst>(V) && classof(cast<PragmaInst>(V));
  }

  bool hasRegister() const {
    if (!isValidInst())
      assert(0 && "Illegal ap_hs intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    auto *Reg = cast<ConstantInt>(Bundle.getValue().Inputs[1]);
    return Reg->isOne();
  }

  StringRef getSignalName() const {
    if (!isValidInst())
      assert(0 && "Illegal ap_hs intrinsic");
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    Value *V = Bundle.getValue().Inputs[2];
    if (isa<GlobalVariable>(V))
      V = cast<GlobalVariable>(V)->getInitializer();
    if (isa<ConstantAggregateZero>(V))
      return "";
    else
      return cast<ConstantDataSequential>(V)->getRawDataValues();
  }

private:
  unsigned getNumArgs() const {
    return 3;
  }

  bool isValidInst() const {
    Optional<OperandBundleUse> Bundle = getOperandBundle(BundleTagName);
    if (Bundle && Bundle.getValue().Inputs.size() == getNumArgs())
      return true;
    else
      return false;
  }
};

} // namespace llvm

#endif // REFLOW_SPIR_INTRINSICINST_H
