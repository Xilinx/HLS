//===---- CGLoopInfo.h - LLVM CodeGen for loop metadata -*- C++ -*---------===//
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
//
// This is the internal state used for llvm translation for loop statement
// metadata.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CGLOOPINFO_H
#define LLVM_CLANG_LIB_CODEGEN_CGLOOPINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Optional.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class BasicBlock;
class Instruction;
class MDNode;
} // end namespace llvm

namespace clang {
class Attr;
class ASTContext;
namespace CodeGen {
class CodeGenFunction;

/// \brief Attributes that may be specified on loops.
struct LoopAttributes {
  explicit LoopAttributes(bool IsParallel = false);
  void clear();

  /// \brief Generate llvm.loop.parallel metadata for loads and stores.
  bool IsParallel;

  /// \brief Generate llvm.loop.dataflow metadata for loop.
  bool IsDataflow;
  bool DisableDFPropagation;

  /// \brief State of loop vectorization or unrolling.
  enum LVEnableState { Unspecified, Enable, Disable, Full };

  /// \brief Value for llvm.loop.vectorize.enable metadata.
  LVEnableState VectorizeEnable;

  /// \brief Value for llvm.loop.unroll.* metadata (enable, disable, or full).
  LVEnableState UnrollEnable;

  /// \brief Value for llvm.loop.vectorize.width metadata.
  unsigned VectorizeWidth;

  /// \brief Value for llvm.loop.interleave.count metadata.
  unsigned InterleaveCount;

  /// \brief llvm.unroll.
  unsigned UnrollCount;

  int UnrollWithoutCheck;

  /// \brief Value for llvm.loop.distribute.enable metadata.
  LVEnableState DistributeEnable;

  /// \brief Value for llvm.loop.flatten.enable metadata.
  LVEnableState FlattenEnable;

  /// \brief Value for llvm.loop.pipeline metadata.
  llvm::Optional<int> PipelineII;

  /// \brief Value for llvm.loop.pipeline metadata.
  int PipelineStyle;

  bool Rewind;

  /// \brief Value for llvm.loop.tripcount metadata.
  llvm::SmallVector<int, 3> TripCount;

  /// \brief Value for llvm.loop.latency metadata.
  llvm::SmallVector<int, 2> MinMax;

  /// \brief Value for llvm.loop.name metadata.
  llvm::StringRef LoopName;
};

/// \brief Information used when generating a structured loop.
class LoopInfo {
public:
  /// \brief Construct a new LoopInfo for the loop with entry Header.
  LoopInfo(llvm::BasicBlock *Header, const LoopAttributes &Attrs,
           const llvm::DebugLoc &StartLoc, const llvm::DebugLoc &EndLoc);

  /// \brief Get the loop id metadata for this loop.
  llvm::MDNode *getLoopID() const { return LoopID; }

  /// \brief Get the header block of this loop.
  llvm::BasicBlock *getHeader() const { return Header; }

  /// \brief Get the set of attributes active for this loop.
  const LoopAttributes &getAttributes() const { return Attrs; }

private:
  /// \brief Loop ID metadata.
  llvm::MDNode *LoopID;
  /// \brief Header block of this loop.
  llvm::BasicBlock *Header;
  /// \brief The attributes for this loop.
  LoopAttributes Attrs;
};

/// \brief A stack of loop information corresponding to loop nesting levels.
/// This stack can be used to prepare attributes which are applied when a loop
/// is emitted.
class LoopInfoStack {
  LoopInfoStack(const LoopInfoStack &) = delete;
  void operator=(const LoopInfoStack &) = delete;

public:
  LoopInfoStack() {}

  /// \brief Begin a new structured loop. The set of staged attributes will be
  /// applied to the loop and then cleared.
  void push(llvm::BasicBlock *Header, const llvm::DebugLoc &StartLoc,
            const llvm::DebugLoc &EndLoc);

  /// \brief Begin a new structured loop. Stage attributes from the Attrs list.
  /// The staged attributes are applied to the loop and then cleared.
  void push(CodeGenFunction* CGF, llvm::BasicBlock *Header, clang::ASTContext &Ctx,
            llvm::ArrayRef<const Attr *> Attrs, const llvm::DebugLoc &StartLoc,
            const llvm::DebugLoc &EndLoc);

  /// \brief End the current loop.
  void pop();

  /// \brief Return the top loop id metadata.
  llvm::MDNode *getCurLoopID() const { return getInfo().getLoopID(); }

  /// \brief Return true if the top loop is parallel.
  bool getCurLoopParallel() const {
    return hasInfo() ? getInfo().getAttributes().IsParallel : false;
  }

  /// \brief Function called by the CodeGenFunction when an instruction is
  /// created.
  void InsertHelper(llvm::Instruction *I) const;

  /// \brief Set the next pushed loop as dafaflow.
  void setDataflow(bool Enable = true, bool DisableDFPropagation = false) {
    StagedAttrs.IsDataflow = Enable;
    StagedAttrs.DisableDFPropagation = DisableDFPropagation;
  }

  /// \brief Set the next pushed loop as parallel.
  void setParallel(bool Enable = true) { StagedAttrs.IsParallel = Enable; }

  /// \brief Set the next pushed loop 'vectorize.enable'
  void setVectorizeEnable(bool Enable = true) {
    StagedAttrs.VectorizeEnable =
        Enable ? LoopAttributes::Enable : LoopAttributes::Disable;
  }

  /// \brief Set the next pushed loop as a distribution candidate.
  void setDistributeState(bool Enable = true) {
    StagedAttrs.DistributeEnable =
        Enable ? LoopAttributes::Enable : LoopAttributes::Disable;
  }

  /// \brief Set the next pushed loop as a flatten candidate.
  void setFlattenState(bool Enable = true) {
    StagedAttrs.FlattenEnable =
        Enable ? LoopAttributes::Enable : LoopAttributes::Disable;
  }

  /// \brief Set the next pushed loop as a pipeline candidate.
  void setPipelineII(int C) { StagedAttrs.PipelineII = C; }

  void setPipelineStyle(int C = -1) { StagedAttrs.PipelineStyle = C; }

  void setRewind(bool Enable = true) { StagedAttrs.Rewind = Enable; }

  void setTripCount(llvm::SmallVector<int, 3> C) { StagedAttrs.TripCount = C; }

  /// Set the Min and Max arguments for the attribute.
  void setMinMax(llvm::SmallVector<int, 2> C) { StagedAttrs.MinMax = C; }

  /// \brief Set the next pushed loop's name.
  void setLoopName(llvm::StringRef Name) { StagedAttrs.LoopName = Name; }

  /// \brief Set the next pushed loop unroll state.
  void setUnrollState(const LoopAttributes::LVEnableState &State) {
    StagedAttrs.UnrollEnable = State;
  }

  /// \brief Set the vectorize width for the next loop pushed.
  void setVectorizeWidth(unsigned W) { StagedAttrs.VectorizeWidth = W; }

  /// \brief Set the interleave count for the next loop pushed.
  void setInterleaveCount(unsigned C) { StagedAttrs.InterleaveCount = C; }

  /// \brief Set the unroll count for the next loop pushed.
  void setUnrollCount(unsigned C) { StagedAttrs.UnrollCount = C; }

  void setUnrollWithoutCheck(int C) { StagedAttrs.UnrollWithoutCheck = C; }
private:
  /// \brief Returns true if there is LoopInfo on the stack.
  bool hasInfo() const { return !Active.empty(); }
  /// \brief Return the LoopInfo for the current loop. HasInfo should be called
  /// first to ensure LoopInfo is present.
  const LoopInfo &getInfo() const { return Active.back(); }
  /// \brief The set of attributes that will be applied to the next pushed loop.
  LoopAttributes StagedAttrs;
  /// \brief Stack of active loops.
  llvm::SmallVector<LoopInfo, 4> Active;
};

} // end namespace CodeGen
} // end namespace clang

#endif
