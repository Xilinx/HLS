//===---- CGLoopInfo.cpp - LLVM CodeGen for loop metadata -*- C++ -*-------===//
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

#include "CGLoopInfo.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/Sema/LoopHint.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "CodeGenFunction.h"
#include "CGValue.h"
using namespace clang::CodeGen;
using namespace llvm;

static int EvaluateInteger(clang::Expr *E, clang::ASTContext &Ctx, int Default = -1) {
  if (!E)
    return Default;

  llvm::APSInt Value = E->EvaluateKnownConstInt(Ctx);
  return Value.getSExtValue();
}

static MDNode *createMetadata(LLVMContext &Ctx, const LoopAttributes &Attrs,
                              const llvm::DebugLoc &StartLoc,
                              const llvm::DebugLoc &EndLoc) {

  if (!Attrs.IsParallel && Attrs.VectorizeWidth == 0 &&
      Attrs.InterleaveCount == 0 && Attrs.UnrollCount == 0 &&
      Attrs.UnrollWithoutCheck == -1 &&
      Attrs.VectorizeEnable == LoopAttributes::Unspecified &&
      Attrs.UnrollEnable == LoopAttributes::Unspecified &&
      Attrs.DistributeEnable == LoopAttributes::Unspecified &&
      Attrs.FlattenEnable == LoopAttributes::Unspecified &&
      !Attrs.PipelineII.hasValue() && 
      !Attrs.Rewind && Attrs.TripCount.empty() &&
      Attrs.MinMax.empty() && Attrs.LoopName.empty() && !Attrs.IsDataflow &&
      !StartLoc && !EndLoc)
    return nullptr;

  SmallVector<Metadata *, 4> Args;
  // Reserve operand 0 for loop id self reference.
  auto TempNode = MDNode::getTemporary(Ctx, None);
  Args.push_back(TempNode.get());

  // If we have a valid start debug location for the loop, add it.
  if (StartLoc) {
    Args.push_back(StartLoc.getAsMDNode());

    // If we also have a valid end debug location for the loop, add it.
    if (EndLoc)
      Args.push_back(EndLoc.getAsMDNode());
  }

  // Setting vectorize.width
  if (Attrs.VectorizeWidth > 0) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.vectorize.width"),
                        ConstantAsMetadata::get(ConstantInt::get(
                            Type::getInt32Ty(Ctx), Attrs.VectorizeWidth))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  // Setting interleave.count
  if (Attrs.InterleaveCount > 0) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.interleave.count"),
                        ConstantAsMetadata::get(ConstantInt::get(
                            Type::getInt32Ty(Ctx), Attrs.InterleaveCount))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  // Setting interleave.count
  if (Attrs.UnrollCount > 0) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.unroll.count"),
                        ConstantAsMetadata::get(ConstantInt::get(
                            Type::getInt32Ty(Ctx), Attrs.UnrollCount))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  // UnrollNoCheckCount = 0 means full unroll
  if (Attrs.UnrollWithoutCheck != -1) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.unroll.withoutcheck"),
                        ConstantAsMetadata::get(ConstantInt::get(
                            Type::getInt32Ty(Ctx), Attrs.UnrollWithoutCheck))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  // Setting vectorize.enable
  if (Attrs.VectorizeEnable != LoopAttributes::Unspecified) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.vectorize.enable"),
                        ConstantAsMetadata::get(ConstantInt::get(
                            Type::getInt1Ty(Ctx), (Attrs.VectorizeEnable ==
                                                   LoopAttributes::Enable)))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  // Setting unroll.full or unroll.disable
  if (Attrs.UnrollEnable != LoopAttributes::Unspecified) {
    std::string Name;
    if (Attrs.UnrollEnable == LoopAttributes::Enable)
      Name = "llvm.loop.unroll.enable";
    else if (Attrs.UnrollEnable == LoopAttributes::Full)
      Name = "llvm.loop.unroll.full";
    else
      Name = "llvm.loop.unroll.disable";
    Metadata *Vals[] = {MDString::get(Ctx, Name)};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  if (Attrs.PipelineII) {
    Metadata *Vals[] = {
        MDString::get(Ctx, "llvm.loop.pipeline.enable"),
        ConstantAsMetadata::get(ConstantInt::get(Type::getInt32Ty(Ctx),
                                                 Attrs.PipelineII.getValue())),
        ConstantAsMetadata::get(
            ConstantInt::get(Type::getInt1Ty(Ctx), Attrs.Rewind)),
        ConstantAsMetadata::get(
            ConstantInt::get(Type::getInt8Ty(Ctx), Attrs.PipelineStyle))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  if (!Attrs.LoopName.empty()) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.name"),
                        MDString::get(Ctx, Attrs.LoopName)};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  if (Attrs.DistributeEnable != LoopAttributes::Unspecified) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.distribute.enable"),
                        ConstantAsMetadata::get(ConstantInt::get(
                            Type::getInt1Ty(Ctx), (Attrs.DistributeEnable ==
                                                   LoopAttributes::Enable)))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  if (Attrs.FlattenEnable != LoopAttributes::Unspecified) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.flatten.enable"),
                        ConstantAsMetadata::get(ConstantInt::get(
                            Type::getInt1Ty(Ctx),
                            (Attrs.FlattenEnable == LoopAttributes::Enable)))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  if (Attrs.MinMax.size() == 2) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.latency"),
                        ConstantAsMetadata::get(ConstantInt::get(
                            Type::getInt32Ty(Ctx), Attrs.MinMax[0])),
                        ConstantAsMetadata::get(ConstantInt::get(
                            Type::getInt32Ty(Ctx), Attrs.MinMax[1]))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  if (Attrs.TripCount.size() == 3) {
    Metadata *Vals[] = {
        MDString::get(Ctx, "llvm.loop.tripcount"),
        ConstantAsMetadata::get(
            ConstantInt::get(Type::getInt32Ty(Ctx), Attrs.TripCount[0])),
        ConstantAsMetadata::get(
            ConstantInt::get(Type::getInt32Ty(Ctx), Attrs.TripCount[1])),
        ConstantAsMetadata::get(
            ConstantInt::get(Type::getInt32Ty(Ctx), Attrs.TripCount[2])),
    };
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  if (Attrs.IsDataflow) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.dataflow.enable"),
                        ConstantAsMetadata::get(ConstantInt::get(
                            Type::getInt1Ty(Ctx), Attrs.DisableDFPropagation))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  // Set the first operand to itself.
  MDNode *LoopID = MDNode::get(Ctx, Args);
  LoopID->replaceOperandWith(0, LoopID);
  return LoopID;
}

LoopAttributes::LoopAttributes(bool IsParallel)
    : IsParallel(IsParallel), VectorizeEnable(LoopAttributes::Unspecified),
      UnrollEnable(LoopAttributes::Unspecified), VectorizeWidth(0),
      InterleaveCount(0), UnrollCount(0), UnrollWithoutCheck(-1),
      DistributeEnable(LoopAttributes::Unspecified),
      FlattenEnable(LoopAttributes::Unspecified), Rewind(false), PipelineStyle(-1),
      IsDataflow(false), DisableDFPropagation(true) {}

void LoopAttributes::clear() {
  IsParallel = false;
  IsDataflow = false;
  DisableDFPropagation = true;
  VectorizeWidth = 0;
  InterleaveCount = 0;
  UnrollCount = 0;
  UnrollWithoutCheck = -1;
  VectorizeEnable = LoopAttributes::Unspecified;
  UnrollEnable = LoopAttributes::Unspecified;
  DistributeEnable = LoopAttributes::Unspecified;
  FlattenEnable = LoopAttributes::Unspecified;
  PipelineII.reset();
  PipelineStyle = -1;
  Rewind = false;
  TripCount.clear();
  MinMax.clear();
  LoopName = "";
}

LoopInfo::LoopInfo(BasicBlock *Header, const LoopAttributes &Attrs,
                   const llvm::DebugLoc &StartLoc, const llvm::DebugLoc &EndLoc)
    : LoopID(nullptr), Header(Header), Attrs(Attrs) {
  LoopID = createMetadata(Header->getContext(), Attrs, StartLoc, EndLoc);
}

void LoopInfoStack::push(BasicBlock *Header, const llvm::DebugLoc &StartLoc,
                         const llvm::DebugLoc &EndLoc) {
  Active.push_back(LoopInfo(Header, StagedAttrs, StartLoc, EndLoc));
  // Clear the attributes so nested loops do not inherit them.
  StagedAttrs.clear();
}

//FIXME, following code is very ugly, TODO, trim it 
void LoopInfoStack::push(CodeGenFunction* CGF, BasicBlock *Header, clang::ASTContext &Ctx,
                         ArrayRef<const clang::Attr *> Attrs,
                         const llvm::DebugLoc &StartLoc,
                         const llvm::DebugLoc &EndLoc) {

  // Identify loop hint attributes from Attrs.
  for (const auto *Attr : Attrs) {
    if (auto *N = dyn_cast<XCLRegionNameAttr>(Attr)) {
      setLoopName(N->getName());
      continue;
    }

    if (isa<XlxPipelineAttr>(Attr) && dyn_cast<XlxPipelineAttr>(Attr)->getRewind()) {
      setRewind();
    }

    const LoopHintAttr *LH = dyn_cast<LoopHintAttr>(Attr);
    const OpenCLUnrollHintAttr *OpenCLHint =
        dyn_cast<OpenCLUnrollHintAttr>(Attr);

    const XlxUnrollHintAttr *XlxUnrollHint = 
        dyn_cast<XlxUnrollHintAttr>(Attr);

    //now, XCLPipelineLoop is different with XlxlPipeline
    //XCLPipelineLoopAttr is used by OpenCL kernel 
    //while, XlxPipelineAttr is for HLS kernel 
    const XCLPipelineLoopAttr *XCLPipeline =
        dyn_cast<XCLPipelineLoopAttr>(Attr);

    const XlxPipelineAttr *XlxPipeline = 
        dyn_cast<XlxPipelineAttr>(Attr);

    const XCLFlattenLoopAttr *XCLFlatten = dyn_cast<XCLFlattenLoopAttr>(Attr);

    const XCLLoopTripCountAttr *XCLTripCount =
        dyn_cast<XCLLoopTripCountAttr>(Attr);

    const XlxLoopTripCountAttr *XlxTripCount = 
        dyn_cast<XlxLoopTripCountAttr>(Attr);

    const XCLDataFlowAttr *XCLDataflow = dyn_cast<XCLDataFlowAttr>(Attr);

    const XCLLatencyAttr *XCLLatency = dyn_cast<XCLLatencyAttr>(Attr);

    const XlxDependenceAttr* XLXDependence = 
        dyn_cast<XlxDependenceAttr>( Attr );

    // Skip non loop hint attributes
    if (!LH && !OpenCLHint && !XlxUnrollHint && !XCLPipeline && !XlxPipeline && !XCLFlatten && !XCLTripCount && 
        !XlxTripCount && !XCLDataflow && !XCLLatency && !XLXDependence) {
      continue;
    }

    LoopHintAttr::OptionType Option = LoopHintAttr::Unroll;
    LoopHintAttr::LoopHintState State = LoopHintAttr::Disable;
    unsigned ValueInt = 1;
    int PipelineInt = 0;
    int PipelineStyleInt = -1;
    bool DisableDFPropagation = false;
    SmallVector<int, 3> TripInt = {0, 0, 0};
    SmallVector<int, 2> MinMaxInt = {0, 0};
    // Translate opencl_unroll_hint attribute argument to
    // equivalent LoopHintAttr enums.
    // OpenCL v2.0 s6.11.5:  
    // 0 - full unroll (no argument).
    // 1 - disable unroll.
    // other positive integer n - unroll by n.
    if (OpenCLHint) {
      ValueInt = EvaluateInteger(OpenCLHint->getUnrollHint(), Ctx,
                                 /*Default*/ 0);
      bool ExitCheck = OpenCLHint->getSkipExitCheck();
      if (!ExitCheck) {
        if (ValueInt == 0) {
          State = LoopHintAttr::Full;
        } else if (ValueInt != 1) {
          Option = LoopHintAttr::UnrollCount;
          State = LoopHintAttr::Numeric;
        }
      } else {
        if (ValueInt != 1) {
          Option = LoopHintAttr::UnrollWithoutCheck;
          State = LoopHintAttr::Numeric;
        }
      }
    } 
    else if (XlxUnrollHint) { 
      ValueInt = EvaluateInteger(XlxUnrollHint->getFactor(), Ctx,
                                 /*Default*/ 0);
      bool ExitCheck = XlxUnrollHint->getSkipExitCheck();
      if (!ExitCheck) {
        if (ValueInt == 0) {
          State = LoopHintAttr::Full;
        } else if (ValueInt != 1) {
          Option = LoopHintAttr::UnrollCount;
          State = LoopHintAttr::Numeric;
        }
      } else {
        if (ValueInt != 1) {
          Option = LoopHintAttr::UnrollWithoutCheck;
          State = LoopHintAttr::Numeric;
        }
      }
    }
    else if (XlxPipeline) { 
      PipelineInt = -1;
      PipelineStyleInt = XlxPipeline->getStyle();
      auto *ValueExpr = XlxPipeline->getII();
      if (ValueExpr) {
        llvm::APSInt ValueAPS = ValueExpr->EvaluateKnownConstInt(Ctx);
        PipelineInt = ValueAPS.getSExtValue();
      }
      Option = LoopHintAttr::Pipeline;
      State = LoopHintAttr::Numeric;
    } else if (XCLPipeline) {
      PipelineInt = -1;
      auto *ValueExpr = XCLPipeline->getII();
      if (ValueExpr) {
        llvm::APSInt ValueAPS = ValueExpr->EvaluateKnownConstInt(Ctx);
        PipelineInt = ValueAPS.getSExtValue();
      }
      Option = LoopHintAttr::Pipeline;
      State = LoopHintAttr::Numeric;
    } else if (XCLFlatten) {
      Option = LoopHintAttr::Flatten;
      State = XCLFlatten->getEnable() ? LoopHintAttr::Enable
                                      : LoopHintAttr::Disable;
    } else if (XCLTripCount) {
      int Min = EvaluateInteger(XCLTripCount->getMin(), Ctx, /*Default*/ 0);
      int Max = EvaluateInteger(XCLTripCount->getMax(), Ctx, /*Default*/ -1);
      // FIXME: potential integer overflow.
      int Avg = EvaluateInteger(XCLTripCount->getAvg(), Ctx,
                                /*Default*/ (Min + Max)/2);
      TripInt = {Min, Max, Avg};
      Option = LoopHintAttr::TripCount;
      State = LoopHintAttr::Numeric;
    } else if (XlxTripCount) {
      int Min = EvaluateInteger(XlxTripCount->getMin(), Ctx, /*Default*/ 0);
      int Max = EvaluateInteger(XlxTripCount->getMax(), Ctx, /*Default*/ -1);
      // FIXME: potential integer overflow.
      int Avg = EvaluateInteger(XlxTripCount->getAvg(), Ctx,
                                /*Default*/ (Min + Max)/2);
      TripInt = {Min, Max, Avg};
      Option = LoopHintAttr::TripCount;
      State = LoopHintAttr::Numeric;
    } else if (XCLLatency) {
      int Min = EvaluateInteger(XCLLatency->getMin(), Ctx, /*Default*/ 0);
      int Max = EvaluateInteger(XCLLatency->getMax(), Ctx, /*Default*/ 65535);
      MinMaxInt = {Min, Max};
      Option = LoopHintAttr::Latency;
      State = LoopHintAttr::Numeric;
    } else if (XCLDataflow) {
      DisableDFPropagation = XCLDataflow->getPropagation();
      Option = LoopHintAttr::DataFlow;
      State = LoopHintAttr::Enable;
    } else if (LH) {
      auto *ValueExpr = LH->getValue();
      if (ValueExpr) {
        llvm::APSInt ValueAPS = ValueExpr->EvaluateKnownConstInt(Ctx);
        ValueInt = ValueAPS.getSExtValue();
      }

      Option = LH->getOption();
      State = LH->getState();
    }

    switch (State) {
    case LoopHintAttr::Disable:
      switch (Option) {
      case LoopHintAttr::Vectorize:
        // Disable vectorization by specifying a width of 1.
        setVectorizeWidth(1);
        break;
      case LoopHintAttr::Interleave:
        // Disable interleaving by speciyfing a count of 1.
        setInterleaveCount(1);
        break;
      case LoopHintAttr::Unroll:
        setUnrollState(LoopAttributes::Disable);
        break;
      case LoopHintAttr::Distribute:
        setDistributeState(false);
        break;
      case LoopHintAttr::Flatten:
        setFlattenState(false);
        break;
      case LoopHintAttr::DataFlow:
      case LoopHintAttr::TripCount:
      case LoopHintAttr::Latency:
      case LoopHintAttr::Pipeline:
      case LoopHintAttr::UnrollCount:
      case LoopHintAttr::UnrollWithoutCheck:
      case LoopHintAttr::VectorizeWidth:
      case LoopHintAttr::InterleaveCount:
        llvm_unreachable("Options cannot be disabled.");
        break;
      }
      break;
    case LoopHintAttr::Enable:
      switch (Option) {
      case LoopHintAttr::Vectorize:
      case LoopHintAttr::Interleave:
        setVectorizeEnable(true);
        break;
      case LoopHintAttr::Unroll:
        setUnrollState(LoopAttributes::Enable);
        break;
      case LoopHintAttr::Distribute:
        setDistributeState(true);
        break;
      case LoopHintAttr::Flatten:
        setFlattenState(true);
        break;
      case LoopHintAttr::DataFlow:
        setDataflow(true, DisableDFPropagation);

        break;
      case LoopHintAttr::TripCount:
      case LoopHintAttr::Latency:
      case LoopHintAttr::Pipeline:
      case LoopHintAttr::UnrollCount:
      case LoopHintAttr::UnrollWithoutCheck:
      case LoopHintAttr::VectorizeWidth:
      case LoopHintAttr::InterleaveCount:
        llvm_unreachable("Options cannot enabled.");
        break;
      }
      break;
    case LoopHintAttr::AssumeSafety:
      switch (Option) {
      case LoopHintAttr::Vectorize:
      case LoopHintAttr::Interleave:
        // Apply "llvm.mem.parallel_loop_access" metadata to load/stores.
        setParallel(true);
        setVectorizeEnable(true);
        break;
      case LoopHintAttr::Unroll:
      case LoopHintAttr::UnrollCount:
      case LoopHintAttr::UnrollWithoutCheck:
      case LoopHintAttr::VectorizeWidth:
      case LoopHintAttr::InterleaveCount:
      case LoopHintAttr::Distribute:
      case LoopHintAttr::Flatten:
      case LoopHintAttr::Pipeline:
      case LoopHintAttr::TripCount:
      case LoopHintAttr::DataFlow:
      case LoopHintAttr::Latency:
        llvm_unreachable("Options cannot be used to assume mem safety.");
        break;
      }
      break;
    case LoopHintAttr::Full:
      switch (Option) {
      case LoopHintAttr::Unroll:
        setUnrollState(LoopAttributes::Full);
        break;
      case LoopHintAttr::Vectorize:
      case LoopHintAttr::Interleave:
      case LoopHintAttr::UnrollCount:
      case LoopHintAttr::UnrollWithoutCheck:
      case LoopHintAttr::VectorizeWidth:
      case LoopHintAttr::InterleaveCount:
      case LoopHintAttr::Distribute:
      case LoopHintAttr::Flatten:
      case LoopHintAttr::Pipeline:
      case LoopHintAttr::TripCount:
      case LoopHintAttr::DataFlow:
      case LoopHintAttr::Latency:
        llvm_unreachable("Options cannot be used with 'full' hint.");
        break;
      }
      break;
    case LoopHintAttr::Numeric:
      switch (Option) {
      case LoopHintAttr::VectorizeWidth:
        setVectorizeWidth(ValueInt);
        break;
      case LoopHintAttr::InterleaveCount:
        setInterleaveCount(ValueInt);
        break;
      case LoopHintAttr::UnrollCount:
        setUnrollCount(ValueInt);
        break;
      case LoopHintAttr::UnrollWithoutCheck:
        setUnrollWithoutCheck(ValueInt);
        break;
      case LoopHintAttr::Pipeline:
        setPipelineII(PipelineInt);
        setPipelineStyle(PipelineStyleInt);
        break;
      case LoopHintAttr::TripCount:
        setTripCount(TripInt);
        break;
      case LoopHintAttr::Latency:
        setMinMax(MinMaxInt);
        break;
      case LoopHintAttr::Unroll:
      case LoopHintAttr::Vectorize:
      case LoopHintAttr::Interleave:
      case LoopHintAttr::Distribute:
      case LoopHintAttr::Flatten:
      case LoopHintAttr::DataFlow:
        llvm_unreachable("Options cannot be assigned a value.");
        break;
      }
      break;
    }
  }

  /// Stage the attributes.
  push(Header, StartLoc, EndLoc);
}

void LoopInfoStack::pop() {
  assert(!Active.empty() && "No active loops to pop");
  Active.pop_back();
}

void LoopInfoStack::InsertHelper(Instruction *I) const {
  if (!hasInfo())
    return;

  const LoopInfo &L = getInfo();
  if (!L.getLoopID())
    return;

  if (TerminatorInst *TI = dyn_cast<TerminatorInst>(I)) {
    for (unsigned i = 0, ie = TI->getNumSuccessors(); i < ie; ++i)
      if (TI->getSuccessor(i) == L.getHeader()) {
        TI->setMetadata(llvm::LLVMContext::MD_loop, L.getLoopID());
        break;
      }
    return;
  }

  if (L.getAttributes().IsParallel && I->mayReadOrWriteMemory())
    I->setMetadata("llvm.mem.parallel_loop_access", L.getLoopID());
}
