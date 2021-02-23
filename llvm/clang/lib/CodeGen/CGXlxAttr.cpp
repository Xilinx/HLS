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
// This contains code to emit Xilinx specific attributes
//
//===----------------------------------------------------------------------===//

#include "CGRecordLayout.h"
#include "CodeGenFunction.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "clang/Basic/HLSDiagnostic.h"

#include "llvm/ADT/StringSwitch.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Support/XILINXFPGAPlatformBasic.h"

using namespace clang;
using namespace sema;
using namespace CodeGen;

/// ExtractAttrInteger - Extract the integer value from the attribute argument.
/// \param Attr, The attribute we are trying to extract the integer value.
/// \param E, The expression from the attribute argument.
/// \param LB, The lower bound for the integer value which is included.
/// \param UB, the upper bound for the integer value which is included.
/// returns the extracted integer or \param Default value.
static int ExtractAttrInteger(const Attr *Attr, Expr *E,
                              DiagnosticsEngine &Diag, const ASTContext &Ctx,
                              int LB, int UB, int Default = -1) {
  if (!E)
    return Default;

  llvm::APSInt ValueAPS = E->EvaluateKnownConstInt(Ctx);
  int64_t Ret = ValueAPS.getSExtValue();

  if (LB <= UB && (Ret < LB || Ret > UB)) {
    Diag.Report(Attr->getLocation(), diag::err_attribute_argument_outof_range)
        << Attr->getSpelling() << LB << UB << E->getSourceRange();
    return Default;
  }

  if (LB == 1 && Ret < 1) {
    Diag.Report(Attr->getLocation(),
                diag::err_attribute_requires_positive_integer)
        << Attr->getSpelling() << E->getSourceRange();
    return Default;
  }

  if (LB == 0 && Ret < 0) {
    Diag.Report(Attr->getLocation(), diag::err_attribute_argument_out_of_bounds)
        << Attr->getSpelling() << 0 << E->getSourceRange();
    return Default;
  }

  return Ret;
}

static int EvaluateInteger(Expr *E, const ASTContext &Ctx, int Default = -1) {
  if (!E)
    return Default;

  llvm::APSInt Value = E->EvaluateKnownConstInt(Ctx);
  return Value.getSExtValue();
}

template <unsigned N>
static llvm::ConstantAsMetadata *CreateIntMeatadata(llvm::LLVMContext &Ctx,
                                                    uint64_t V) {
  return llvm::ConstantAsMetadata::get(
      llvm::ConstantInt::get(llvm::IntegerType::get(Ctx, N), V));
}

static void EmitXCLArrayXFormAttr(XCLArrayXFormAttr *A, llvm::LLVMContext &Ctx,
                                  SmallVectorImpl<llvm::Metadata *> &MDs,
                                  const ASTContext &ASTCtx) {
  auto Dim = EvaluateInteger(A->getDim(), ASTCtx, /*Default*/ 1);
  auto Factor = EvaluateInteger(A->getFactor(), ASTCtx, /*Default*/ 0);
  MDs.push_back(llvm::MDNode::get(
      Ctx, {
               llvm::MDString::get(Ctx, A->getSpelling()),
               llvm::MDString::get(
                   Ctx, XCLArrayXFormAttr::ConvertXCLArrayXFormTypeToStr(
                            A->getType())),
               CreateIntMeatadata<32>(Ctx, Dim),
               CreateIntMeatadata<32>(Ctx, Factor),
           }));
}

static void EmitXCLVisibilityAttr(XCLVisibilityAttr *A, llvm::LLVMContext &Ctx,
                                  SmallVectorImpl<llvm::Metadata *> &MDs) {
  MDs.push_back(llvm::MDNode::get(
      Ctx, {llvm::MDString::get(Ctx, A->getSpelling()),
            llvm::MDString::get(
                Ctx, XCLVisibilityAttr::ConvertXCLVisibilityTypeToStr(
                         A->getVisibility()))}));
}

static void EmitXCLReqdPipeDepthAttr(XCLReqdPipeDepthAttr *A,
                                     llvm::LLVMContext &Ctx,
                                     SmallVectorImpl<llvm::Metadata *> &MDs,
                                     const ASTContext &ASTCtx) {
  if (A->getOff()) {
    auto Depth = EvaluateInteger(A->getDepth(), ASTCtx, /*Default*/ 1);
    MDs.push_back(
        llvm::MDNode::get(Ctx, {llvm::MDString::get(Ctx, "xlx_fpga_pipo_depth"),
                                CreateIntMeatadata<32>(Ctx, Depth)}));
  } else {
    auto Depth = EvaluateInteger(A->getDepth(), ASTCtx, /*Default*/ 1);
    MDs.push_back(
        llvm::MDNode::get(Ctx, {llvm::MDString::get(Ctx, A->getSpelling()),
                                CreateIntMeatadata<32>(Ctx, Depth)}));
  }
}

static void EmitXlxReqdPipeDepthAttr(XlxReqdPipeDepthAttr *A,
                                     llvm::LLVMContext &Ctx,
                                     SmallVectorImpl<llvm::Metadata *> &MDs,
                                     const ASTContext &ASTCtx) {
  if (A->getOff()) {
    auto Depth = EvaluateInteger(A->getDepth(), ASTCtx, /*Default*/ 1);
    MDs.push_back(
        llvm::MDNode::get(Ctx, {llvm::MDString::get(Ctx, "xlx_fpga_pipo_depth"),
                                CreateIntMeatadata<32>(Ctx, Depth)}));
  } else {
    auto Depth = EvaluateInteger(A->getDepth(), ASTCtx, /*Default*/ 1);
    // to be compatible with Reflow's handler, using "xcl_reqd_pipe_depth"
    // instead of "xlx_read_pipe_depth"
    MDs.push_back(
        llvm::MDNode::get(Ctx, {llvm::MDString::get(Ctx, "xcl_reqd_pipe_depth"),
                                CreateIntMeatadata<32>(Ctx, Depth)}));
  }
}

template <typename AttrType>
static void EmitVarEnableAttr(AttrType *A, llvm::LLVMContext &Ctx,
                              SmallVectorImpl<llvm::Metadata *> &MDs) {
  MDs.push_back(
      llvm::MDNode::get(Ctx, {llvm::MDString::get(Ctx, A->getSpelling()),
                              CreateIntMeatadata<32>(Ctx, A->getEnabled())}));
}

static bool IsPhysicalInterfaceType(const RecordDecl *D) {
  auto *NS = dyn_cast_or_null<NamespaceDecl>(D->getDeclContext());
  if (!NS) {
    return llvm::StringSwitch<bool>(D->getName())
        .Case("ap_shift_reg", true)
        .Default(false);
  }

  if (!NS->getName().equals("hls"))
    return false;
  auto *ParentCtx = NS->getDeclContext();
  if (!ParentCtx || !isa<TranslationUnitDecl>(ParentCtx))
    return false;

  return llvm::StringSwitch<bool>(D->getName())
      .Case("stream", true)
      .Default(false);
}

static void CollectBundles(const RecordType *RT, CodeGenTypes &CGT,
                           llvm::Constant *Base,
                           SmallVectorImpl<llvm::Value *> &Indicies,
                           SmallVectorImpl<llvm::Metadata *> &GEPs) {
  auto *D = RT->getDecl();
  if (IsPhysicalInterfaceType(D)) {
    auto *ET = Base->getType()->getPointerElementType();
    auto *P = llvm::ConstantExpr::getInBoundsGetElementPtr(ET, Base, Indicies);
    auto &Ctx = P->getContext();
    GEPs.push_back(
        llvm::MDTuple::get(Ctx, {llvm::MDString::get(Ctx, D->getName()),
                                 llvm::ConstantAsMetadata::get(P)}));
    return;
  }

  auto *I32T = Indicies.front()->getType();
  Indicies.push_back(Indicies.front());
  for (auto *F : D->fields()) {
    if (F->isBitField())
      continue;

    unsigned int Idx = CGT.getCGRecordLayout(D).getLLVMFieldNo(F);
    Indicies.back() = llvm::ConstantInt::get(I32T, Idx);
    if (auto *T = dyn_cast<RecordType>(F->getType().getCanonicalType()))
      CollectBundles(T, CGT, Base, Indicies, GEPs);
  }
  Indicies.pop_back();
}

static void EmitBundleAttr(const VarDecl *D, CodeGenTypes &CGT,
                           llvm::LLVMContext &Ctx,
                           SmallVectorImpl<llvm::Metadata *> &MDs) {
  auto *RT = dyn_cast<RecordType>(D->getType().getCanonicalType());
  if (!RT)
    return;

  llvm::Type *T = CGT.ConvertType(D->getType());
  auto *Base = llvm::ConstantPointerNull::get(T->getPointerTo());

  auto *I32T = llvm::Type::getInt32Ty(Base->getContext());
  auto *Zero = llvm::ConstantInt::get(I32T, 0);
  SmallVector<llvm::Value *, 4> Indicies(1, Zero);
  SmallVector<llvm::Metadata *, 4> GEPs;
  CollectBundles(RT, CGT, Base, Indicies, GEPs);

  if (GEPs.empty())
    return;

  MDs.push_back(
      llvm::MDNode::get(Ctx, {llvm::MDString::get(Ctx, "discrete.components"),
                              llvm::MDTuple::get(Ctx, GEPs)}));
}

// emit attribute for variable decl
static void EmitXlxAttributesImpl(const VarDecl *D, CodeGenTypes &CGT,
                                  llvm::Value *V, llvm::LLVMContext &Ctx,
                                  const ASTContext &ASTCtx,
                                  DiagnosticsEngine &Diag) {
  SmallVector<llvm::Metadata *, 8> MDs;

  for (auto *A : D->specific_attrs<XCLArrayXFormAttr>())
    EmitXCLArrayXFormAttr(A, Ctx, MDs, ASTCtx);

  for (auto *A : D->specific_attrs<XCLVisibilityAttr>())
    EmitXCLVisibilityAttr(A, Ctx, MDs);

  for (auto *A : D->specific_attrs<XlxVarResetAttr>())
    EmitVarEnableAttr<XlxVarResetAttr>(A, Ctx, MDs);

  for (auto *A : D->specific_attrs<XCLReqdPipeDepthAttr>()) {
    if (!D->getType()->isPipeType()) {
      for (auto *A : D->specific_attrs<XCLReqdPipeDepthAttr>())
        EmitXCLReqdPipeDepthAttr(A, Ctx, MDs, ASTCtx);
    }
  }

  for (auto *A : D->specific_attrs<XlxReqdPipeDepthAttr>()) {
    if (!D->getType()->isPipeType()) {
      for (auto *A : D->specific_attrs<XlxReqdPipeDepthAttr>())
        EmitXlxReqdPipeDepthAttr(A, Ctx, MDs, ASTCtx);
    }
  }

  EmitBundleAttr(D, CGT, Ctx, MDs);

  if (MDs.empty())
    return;

  auto *N = llvm::MDNode::get(Ctx, MDs);
  if (auto *AI = dyn_cast<llvm::AllocaInst>(V)) {
    AI->setMetadata("xilinx.attributes", N);
    return;
  }

  if (auto *GV = dyn_cast<llvm::GlobalVariable>(V)) {
    GV->setMetadata("xilinx.attributes", N);
    return;
  }
}

void CodeGenFunction::EmitStopPoint(SourceLocation Loc) {
  if (CGDebugInfo *DI = getDebugInfo()) {
    DI->EmitLocation(Builder, Loc);
    LastStopPoint = Loc;
  }
}

void CodeGenFunction::EmitXlxAttributes(const VarDecl *D, llvm::Value *V) {
  if (getLangOpts().HLSExt) {
    EmitXlxAttributesImpl(D, CGM.getTypes(), V, getLLVMContext(), getContext(),
                          CGM.getDiags());
    // if the VarDecl is static local const
  }
}

void CodeGenModule::SetXlxAttributes(const VarDecl *D,
                                     llvm::GlobalVariable *GV) {
  if (getLangOpts().HLSExt)
    EmitXlxAttributesImpl(D, getTypes(), GV, getLLVMContext(), getContext(),
                          getDiags());
}

static std::string SynthesisAttr(FPGAFunctionCtrlInterfaceAttr *A) {
  std::string S = A->getMode().str() + "." + std::to_string(A->getRegistered());
  auto Adaptor = A->getName();
  if (!Adaptor.empty()) {
    S += ".";
    S += Adaptor.str();
  }

  return S;
}

static std::string SynthesisAttr(FPGAScalarInterfaceAttr *A) {
  SmallString<64> S = A->getMode();
  auto Adaptor = A->getAdaptor();
  if (!Adaptor.empty()) {
    S += ".";
    S += Adaptor;
  }

  return S.str();
}

static std::string SynthesisAttr(FPGAScalarInterfaceWrapperAttr *A,
                                 ASTContext &Ctx) {
  SmallString<64> S = A->getMode();
  auto Adaptor = A->getAdaptor();
  auto *OffsetExpr = A->getOffset();
  if (!Adaptor.empty()) {
    S += ".";
    S += Adaptor;
    // Offset should be available only if the Adaptor is specified.
    if (OffsetExpr) {
      auto OffsetInt = EvaluateInteger(OffsetExpr, Ctx);
      if (OffsetInt != -1) {
        const auto &Offset = llvm::utostr(OffsetInt);
        S += ".";
        S += Offset;
      }
    }
  }

  return S.str();
}

static std::string SynthesisAttr(FPGAAddressInterfaceAttr *A,
                                 bool hasInterfaceWrapper = false) {
  SmallString<64> S = A->getMode();

  auto Adaptor = A->getAdaptor();
  if (!Adaptor.empty()) {
    S += ".";
    S += Adaptor;
  }

  auto CurrentMode = A->getMode();
  auto OffsetMode = A->getOffsetMode();
  if (OffsetMode != FPGAAddressInterfaceAttr::Default &&
      OffsetMode != FPGAAddressInterfaceAttr::Off) {
    // Not only maxi mode
    S += ".";
    S += FPGAAddressInterfaceAttr::ConvertOffsetModeTypeToStr(OffsetMode);
  } else if (CurrentMode == "m_axi" &&
             OffsetMode != FPGAAddressInterfaceAttr::Default) {
    // Only maxi mode, if user sets offset value, no matter s_axilite or not,
    // dump safely to LLVM IR
    S += ".";
    S += FPGAAddressInterfaceAttr::ConvertOffsetModeTypeToStr(OffsetMode);
  } else {
    // If no offset (default), do nothing on LLVM IR
  }

  return S.str();
}

void CodeGenFunction::EmitXlxParamAttributes(const ParmVarDecl *D,
                                             llvm::Value *V) {
  auto *Parm = dyn_cast<llvm::Argument>(V);
  if (!Parm) {
    assert(false &&
           "seriouse bug, some parameter can not emit llvm argument correctly");
    return;
  }
  auto &Ctx = getLLVMContext();
  SmallVector<llvm::OperandBundleDef, 4> ScopeAttrs;

  // strip decayed and typedef type
  QualType DeclType = D->getOriginalType().getCanonicalType();

  if (auto *A = D->getAttr<XCLArrayGeometryAttr>()) {
    SmallVector<llvm::Value *, 4> Dims;
    for (auto Dim : A->dims()) {
      ApplyDebugLocation DL(*this, Dim);
      Dims.push_back(EmitScalarExpr(Dim));
    }

    ScopeAttrs.emplace_back(A->getSpelling(), Dims);
  }

  if (auto *AT = dyn_cast<ConstantArrayType>(DeclType)) {
    // Annotate the dim from the original type
    const auto &Depth = llvm::utostr(AT->getSize().getZExtValue());
    Parm->addAttr(llvm::Attribute::get(Ctx, "fpga.decayed.dim.hint", Depth));
  }

  if (auto *A = D->getAttr<XCLReqdPipeDepthAttr>()) {
    if (A->getOff()) {
      auto DepthInt =
          EvaluateInteger(A->getDepth(), getContext(), /*Default*/ 1);
      const auto &Depth = llvm::utostr(DepthInt);
      Parm->addAttr(llvm::Attribute::get(Ctx, "xlx.fpga.pipo.depth", Depth));
    } else {
      auto DepthInt =
          EvaluateInteger(A->getDepth(), getContext(), /*Default*/ 1);
      const auto &Depth = llvm::utostr(DepthInt);
      Parm->addAttr(llvm::Attribute::get(Ctx, "xcl.reqd.pipe.depth", Depth));
    }
  }

  if (auto *A = D->getAttr<XlxReqdPipeDepthAttr>()) {
    if (A->getOff()) {
      auto DepthInt =
          EvaluateInteger(A->getDepth(), getContext(), /*Default*/ 1);
      const auto &Depth = llvm::utostr(DepthInt);
      Parm->addAttr(llvm::Attribute::get(Ctx, "xlx.fpga.pipo.depth", Depth));
    } else {
      auto DepthInt =
          EvaluateInteger(A->getDepth(), getContext(), /*Default*/ 1);
      const auto &Depth = llvm::utostr(DepthInt);
      Parm->addAttr(llvm::Attribute::get(Ctx, "xcl.reqd.pipe.depth", Depth));
    }
  }

  if (D->hasAttr<FPGARegisterAttr>())
    Parm->addAttr(llvm::Attribute::get(Ctx, "fpga.register"));

  if (auto *A = D->getAttr<FPGADataFootPrintHintAttr>()) {
    auto DepthInt = EvaluateInteger(A->getDepth(), getContext(), /*Default*/ 0);
    const auto &Depth = llvm::utostr(DepthInt);
    Parm->addAttr(llvm::Attribute::get(Ctx, "fpga.data.footprint.hint", Depth));
  }

  if (auto *A = D->getAttr<FPGASignalNameAttr>()) {
    auto Name = A->getName();
    Parm->addAttr(llvm::Attribute::get(Ctx, "fpga.signal.name", Name));
  }

  if (auto *A = D->getAttr<FPGAMaxiMaxWidenBitwidthAttr>()) {
    auto MaxWidenBitwidthInt =
        EvaluateInteger(A->getMaxWidenBitwidth(), getContext(), /*Default*/ 0);
    const auto &MaxWidenBitwidth = llvm::utostr(MaxWidenBitwidthInt);
    Parm->addAttr(llvm::Attribute::get(Ctx, "fpga.maxi.max_widen_bitwidth",
                                       MaxWidenBitwidth));
  }

  if (auto *A = D->getAttr<FPGAMaxiLatencyAttr>()) {
    auto LatencyInt =
        EvaluateInteger(A->getLatency(), getContext(), /*Default*/ 0);
    const auto &Latency = llvm::utostr(LatencyInt);
    Parm->addAttr(llvm::Attribute::get(Ctx, "fpga.maxi.latency", Latency));
  }

  if (auto *A = D->getAttr<FPGAMaxiNumRdOutstandAttr>()) {
    auto NumRdOutstandInt =
        EvaluateInteger(A->getNumRdOutstand(), getContext(), /*Default*/ 0);
    const auto &NumRdOutstand = llvm::utostr(NumRdOutstandInt);
    Parm->addAttr(llvm::Attribute::get(Ctx, "fpga.maxi.num_read_outstanding",
                                       NumRdOutstand));
  }

  if (auto *A = D->getAttr<FPGAMaxiNumWtOutstandAttr>()) {
    auto NumWtOutstandInt =
        EvaluateInteger(A->getNumWtOutstand(), getContext(), /*Default*/ 0);
    const auto &NumWtOutstand = llvm::utostr(NumWtOutstandInt);
    Parm->addAttr(llvm::Attribute::get(Ctx, "fpga.maxi.num_write_outstanding",
                                       NumWtOutstand));
  }

  if (auto *A = D->getAttr<FPGAMaxiRdBurstLenAttr>()) {
    auto RdBurstLenInt =
        EvaluateInteger(A->getRdBurstLen(), getContext(), /*Default*/ 0);
    const auto &RdBurstLen = llvm::utostr(RdBurstLenInt);
    Parm->addAttr(llvm::Attribute::get(Ctx, "fpga.maxi.max_read_burst_length",
                                       RdBurstLen));
  }

  if (auto *A = D->getAttr<FPGAMaxiWtBurstLenAttr>()) {
    auto WtBurstLenInt =
        EvaluateInteger(A->getWtBurstLen(), getContext(), /*Default*/ 0);
    const auto &WtBurstLen = llvm::utostr(WtBurstLenInt);
    Parm->addAttr(llvm::Attribute::get(Ctx, "fpga.maxi.max_write_burst_length",
                                       WtBurstLen));
  }

  if (auto *A = D->getAttr<FPGAScalarInterfaceAttr>()) {
    const std::string &Name = "fpga.scalar.interface";
    const std::string &Attr = SynthesisAttr(A);
    Parm->addAttr(llvm::Attribute::get(Ctx, Name, Attr));
  }

  if (auto *A = D->getAttr<FPGAScalarInterfaceWrapperAttr>()) {
    const std::string &Name = "fpga.interface.wrapper";
    const std::string &Attr = SynthesisAttr(A, getContext());
    Parm->addAttr(llvm::Attribute::get(Ctx, Name, Attr));
  }

  if (auto *A = D->getAttr<FPGAAddressInterfaceAttr>()) {
    bool hasInterfaceWrapper = D->hasAttr<FPGAScalarInterfaceWrapperAttr>();

    const std::string &Name = "fpga.address.interface";
    const std::string &Attr = SynthesisAttr(A, hasInterfaceWrapper);
    Parm->addAttr(llvm::Attribute::get(Ctx, Name, Attr));

    // check whether set s_axilite interface for maxi slave mode
    // add default s_axilite for maxi slave mode
    StringRef OffsetMode = FPGAAddressInterfaceAttr::ConvertOffsetModeTypeToStr(
        A->getOffsetMode());
    if (OffsetMode.equals_lower("slave") && !hasInterfaceWrapper) {
      // add "fpga.interface.wrapper"="s_axilite.0" for parmdecl
      Parm->addAttr(
          llvm::Attribute::get(Ctx, "fpga.interface.wrapper", "s_axilite.0"));
      // add !fpga.adaptor.saxi.control for function
      llvm::MDBuilder MDB(Ctx);
      auto *Fn = Parm->getParent();
      const StringRef SAXILite = "fpga.adaptor.saxi.0";
      if (Fn->getMetadata(SAXILite) == nullptr) {
        auto *Node = llvm::MDNode::get(Ctx, {MDB.createString("")});
        Parm->getParent()->addMetadata(SAXILite, *Node);
      }
    }
  }

  if (auto *A = D->getAttr<MAXIAliasAttr>()) { 
    int offset = ExtractAttrInteger(A, A->getOffset(), CGM.getDiags(), getContext(),
                                /*LB*/ 0, /*UB*/ INT32_MAX, /*Default*/ -1);
    Parm->addAttr(llvm::Attribute::get(Ctx, "alias.offset", std::to_string(offset)));
    Parm->addAttr(llvm::Attribute::get(Ctx, "alias.group", std::to_string(A->getGroup())));
  }

  if (D->hasAttr<XlxFuncInstantiateAttr>())
    Parm->addAttr(llvm::Attribute::get(Ctx, "fpga.func.instantiate"));

  // Return nullptr if we didn't annotated anything
  if (ScopeAttrs.empty())
    return;

  auto lv = MakeAddrLValue(GetAddrOfLocalVar(D), D->getType());

  auto *SSACopy = llvm::Intrinsic::getDeclaration(
      CurFn->getParent(), llvm::Intrinsic::ssa_copy, Parm->getType());

  auto *Copy = Builder.CreateCall(SSACopy, Parm, ScopeAttrs, Parm->getName());
  EmitStoreOfScalar(Copy, lv);
}

void CodeGenFunction::EmitXlxFunctionAttributes(const FunctionDecl *FD,
                                                llvm::Function *Fn) {
  if (getLangOpts().CPlusPlus)
    if (auto *NameII = FD->getIdentifier())
      Fn->addFnAttr("fpga.demangled.name", NameII->getName());

  if (auto DownwardInline = FD->getAttr<XCLInlineAttr>()) {
    if (DownwardInline->getRecursive())
      Fn->addFnAttr("fpga.recursive.inline");
    else
      Fn->addFnAttr("fpga.region.inline");
  }

  if (auto *A = FD->getAttr<XCLDataFlowAttr>()) {
    Fn->addFnAttr("fpga.dataflow.func", std::to_string(A->getPropagation()));
    // Override the existing inline attributes
    // TODO: warning message in Sema
    Fn->removeFnAttr(llvm::Attribute::AlwaysInline);
    Fn->removeFnAttr(llvm::Attribute::InlineHint);
    // Make sure dataflow function is never inlined
    Fn->addFnAttr(llvm::Attribute::NoInline);
  }

  // according Xlnx's document, OpenCL 's xcl_pipeline_workitems can not apply
  // on function, but , there are some XCL case using  XCLPipelineWorkItems on
  // function , so handle it here,  TODO,  clean it in future;
  if (auto *A = FD->getAttr<XCLPipelineWorkitemsAttr>()) {
    int II = ExtractAttrInteger(A, A->getII(), CGM.getDiags(), getContext(),
                                /*LB*/ -1, /*UB*/ INT32_MAX, /*Default*/ -1);

    std::string args = std::to_string(II) + "." + std::to_string(0);

    Fn->addFnAttr("fpga.static.pipeline", args);
  }

  if (auto *A = FD->getAttr<XlxPipelineAttr>()) {
    int II = ExtractAttrInteger(A, A->getII(), CGM.getDiags(), getContext(),
                                /*LB*/ -1, /*UB*/ INT32_MAX, /*Default*/ -1);
    int Style = A->getStyle();

    std::string args = std::to_string(II) + "." + std::to_string(Style);

    Fn->addFnAttr("fpga.static.pipeline", args);
  }

  if (auto *A = FD->getAttr<XlxExprBalanceAttr>())
    Fn->addFnAttr("fpga.exprbalance.func", std::to_string(A->getEnabled()));

  if (auto *A = FD->getAttr<XlxMergeLoopAttr>())
    Fn->addFnAttr("fpga.mergeloop", std::to_string(A->getForce()));

  if (auto *A = FD->getAttr<SDxKernelAttr>())
    Fn->addFnAttr("fpga.top.func", A->getRTLName());

  auto &Ctx = getLLVMContext();
  llvm::MDBuilder MDB(Ctx);

  for (auto *A : FD->specific_attrs<MAXIAdaptorAttr>()) {
    const std::string &Prefix = "fpga.adaptor.maxi.";
    const std::string &Name = A->getName();

    auto ROSInt = EvaluateInteger(A->getNumReadOutstanding(), getContext(),
                                  /*Default*/ 0);
    auto WOSInt = EvaluateInteger(A->getNumWriteOutstanding(), getContext(),
                                  /*Default*/ 0);
    auto RBLInt = EvaluateInteger(A->getMaxReadBurstLength(), getContext(),
                                  /*Default*/ 0);
    auto WBLInt = EvaluateInteger(A->getMaxWriteBurstLength(), getContext(),
                                  /*Default*/ 0);
    auto Latency = EvaluateInteger(A->getLatency(), getContext(),
                                   /*Default*/ 0);
    auto *Node =
        llvm::MDNode::get(Ctx, {MDB.createConstant(Builder.getInt32(ROSInt)),
                                MDB.createConstant(Builder.getInt32(WOSInt)),
                                MDB.createConstant(Builder.getInt32(RBLInt)),
                                MDB.createConstant(Builder.getInt32(WBLInt)),
                                MDB.createConstant(Builder.getInt32(Latency))});

    Fn->addMetadata(Prefix + Name, *Node);
  }

  for (auto *A : FD->specific_attrs<BRAMAdaptorAttr>()) {
    const std::string &Prefix = "fpga.adaptor.bram.";
    const std::string &Name = A->getName();
    auto Latency = EvaluateInteger(A->getLatency(), getContext(),
                                   /*Default*/ 1);

    auto RAMType = EvaluateInteger(A->getRAMType(), getContext(), -1);
    auto RAMImpl = EvaluateInteger(A->getRAMImpl(), getContext(), -1);
    if (RAMType != platform::PlatformBasic::OP_UNSUPPORTED) {
      const platform::PlatformBasic *XilinxPlatform =
          platform::PlatformBasic::getInstance();
      auto range = XilinxPlatform->verifyLatency(
          (platform::PlatformBasic::OP_TYPE)RAMType,
          (platform::PlatformBasic::IMPL_TYPE)RAMImpl);
      if (Latency != -1 && (Latency < range.first || Latency > range.second)) {
        CGM.getDiags().Report(A->getLocation(),
                              diag::err_latency_value_is_out_of_range)
            << Latency
            << "[" + std::to_string(range.first) + ", " +
                   std::to_string(range.second) + "]";
      }
    }

    auto *Node = llvm::MDNode::get(
        Ctx, {MDB.createString(Name),
              MDB.createString(A->ConvertModeTypeToStr(A->getMode())),
              MDB.createConstant(Builder.getInt32(RAMType)),
              MDB.createConstant(Builder.getInt32(RAMImpl)),
              MDB.createConstant(Builder.getInt32(Latency))});

    Fn->addMetadata(Prefix + Name, *Node);
  }

  for (auto *A : FD->specific_attrs<SAXIAdaptorAttr>()) {
    const std::string &Prefix = "fpga.adaptor.saxi.";
    const std::string &Name = A->getName();
    auto *Node = llvm::MDNode::get(Ctx, {MDB.createString(A->getClock())});

    Fn->addMetadata(Prefix + Name, *Node);
  }

  for (auto *A : FD->specific_attrs<AXISAdaptorAttr>()) {
    const std::string &Prefix = "fpga.adaptor.axis.";
    const std::string &Name = A->getName();
    auto *Node = llvm::MDNode::get(
        Ctx,
        {
            MDB.createString(
                A->ConvertAXISRegisterModeTypeToStr(A->getRegisterMode())),
            MDB.createString(A->ConvertDirectionTypeToStr(A->getDirection())),
        });

    Fn->addMetadata(Prefix + Name, *Node);
  }

  // Return attributes
  if (auto *A = FD->getAttr<FPGAScalarInterfaceAttr>()) {
    const std::string &Name = "fpga.scalar.interface";
    const std::string &Attr = SynthesisAttr(A);
    Fn->addAttribute(llvm::AttributeList::ReturnIndex,
                     llvm::Attribute::get(Ctx, Name, Attr));
  }

  // Return attributes with interface wrapper
  if (auto *A = FD->getAttr<FPGAScalarInterfaceWrapperAttr>()) {
    const std::string &Name = "fpga.interface.wrapper";
    const std::string &Attr = SynthesisAttr(A, getContext());
    Fn->addAttribute(llvm::AttributeList::ReturnIndex,
                     llvm::Attribute::get(Ctx, Name, Attr));
  }

  // Function ctrl interface attributes
  if (auto *A = FD->getAttr<FPGAFunctionCtrlInterfaceAttr>()) {
    const std::string Name = "fpga.handshake.mode";
    const std::string &Attr = SynthesisAttr(A);
    Fn->addFnAttr(Name, Attr);
  }

  for (auto *A : FD->specific_attrs<XCLLatencyAttr>()) {
    int Min = EvaluateInteger(A->getMin(), getContext(), /*Default*/ 0);
    int Max = EvaluateInteger(A->getMax(), getContext(), /*Default*/ 65535);
    Fn->addFnAttr("fpga.latency",
                  std::to_string(Min) + "." + std::to_string(Max));
  }

  for (auto *A : FD->specific_attrs<XlxResourceIPCoreAttr>()) {
    Fn->addFnAttr("fpga.resource.hint", A->getIP()->getName().str() + "." +
                                            A->getCore()->getName().str() +
                                            "." + std::to_string(-1));
  }

  for (auto *A : FD->specific_attrs<HLSPreserveAttr>()) {
    Fn->addFnAttr("hls_preserve");
  }
}

static Expr *StripImplicitCast(Expr *FE) {
  if (auto *Cast = dyn_cast<ImplicitCastExpr>(FE))
    return Cast->getSubExpr();

  return FE;
}

static DeclRefExpr *GetAssignmentLHS(Expr *FE) {
  if (auto *Bin = dyn_cast<BinaryOperator>(FE)) {
    if (!Bin->isAssignmentOp())
      return nullptr;

    auto *E = StripImplicitCast(Bin->getLHS());
    return dyn_cast<DeclRefExpr>(E);
  }

  if (auto *Cop = dyn_cast<CXXOperatorCallExpr>(FE)) {
    if (!Cop->isAssignmentOp())
      return nullptr;

    auto *E = StripImplicitCast(Cop->getArg(0));
    return dyn_cast<DeclRefExpr>(E);
  }

  if (auto *CleanUp = dyn_cast<ExprWithCleanups>(FE))
    return GetAssignmentLHS(CleanUp->getSubExpr());

  return nullptr;
}

void CodeGenFunction::BundleBindOpAttr(const XlxBindOpExprAttr *bindOp, SmallVectorImpl<llvm::OperandBundleDef> &BundleList) 
{
  auto var_expr = bindOp->getVariable();
  auto op_enum = (platform::PlatformBasic::OP_TYPE)EvaluateInteger(
      bindOp->getOp(), getContext(), -1);
  auto impl_enum = (platform::PlatformBasic::IMPL_TYPE)EvaluateInteger(
      bindOp->getImpl(), getContext(), -1);

  // strip null terminator
  auto latency =
      EvaluateInteger(bindOp->getLatency(), getContext(), /*Default*/ -1);

  // TODO, use  enum encoding insteading of "string"

  llvm::Value *args[] = {Builder.getInt32(op_enum),
                         Builder.getInt32(impl_enum),
                         Builder.getInt32(latency)};

  BundleList.emplace_back("fpga_resource_hint", args);
}

void CodeGenFunction::LowerBindOpScope(
    Stmt *&stmt, SmallVector<const XlxBindOpAttr *, 4> BindOpAttrs) {

  switch (stmt->getStmtClass()) {
  case Stmt::NullStmtClass: {
    break;
  }
#define STMT(Type, Base)
#define ABSTRACT_STMT(Op)
#define EXPR(Type, Base) case Stmt::Type##Class:
#include "clang/AST/StmtNodes.inc"
    {
      SmallVector<const Attr *, 4> attrs;
      for (auto attr : BindOpAttrs) {
        auto var_expr = attr->getVariable();
        auto var_ref = dyn_cast<DeclRefExpr>(var_expr);

        auto *declRef = GetAssignmentLHS(dyn_cast<Expr>(stmt));
        if (declRef && declRef->getDecl() == var_ref->getDecl()) {
          auto new_attr = XlxBindOpExprAttr::CreateImplicit(
              getContext(), attr->getVariable(), attr->getOp(), attr->getImpl(),
              attr->getLatency(), attr->getRange());
          attrs.push_back(new_attr);
        }
      }
      if (attrs.size()) {

        stmt = AttributedStmt::Create(getContext(), stmt->getLocStart(), attrs,
                                      stmt);
      }
      break;
    }
  case Stmt::CompoundStmtClass: {
    auto *compound = dyn_cast<CompoundStmt>(stmt);
    for (auto &iter : compound->body()) {
      LowerBindOpScope(iter, BindOpAttrs);
    }
    break;
  }
  case Stmt::DeclStmtClass: {
    auto *declStmt = dyn_cast<DeclStmt>(stmt);
    auto declGroup = declStmt->getDeclGroup();
    for (auto decl : declGroup) { 
      if(!isa<VarDecl>(decl)) { 
        continue;
      }
      auto var_decl = dyn_cast<VarDecl>(decl);
      auto decl_init = var_decl->getInit();
      //skip check, if there is no initialization expr
      if (!decl_init)
        continue;

      if (isTrivialInitializer(decl_init) || isNoCtorCXXConstructExpr(decl_init, *var_decl))
        continue;

      /* for following decl : 
      ap_fixed a = b + c
    will produce following  AST tree, we need scan the "CXXConstructor" 
                     
  | | `-VarDecl 0xaf309a8 <col:5, col:23> col:14 used kk 'ap_fixed' cinit
  | |   `-ExprWithCleanups 0xaf30d78 <col:19, col:23> 'ap_fixed'
  | |     `-CXXConstructExpr 0xaf30d40 <col:19, col:23> 'ap_fixed' 'void (ap_fixed &&) noexcept' elidable
  | |       `-MaterializeTemporaryExpr 0xaf30d28 <col:19, col:23> 'ap_fixed' xvalue
  | |         `-CXXOperatorCallExpr 0xaf30ce0 <col:19, col:23> 'ap_fixed'
  | |           |-ImplicitCastExpr 0xaf30cc8 <col:21> 'ap_fixed (*)(const ap_fixed)' <FunctionToPointerDecay>
  | |           | `-DeclRefExpr 0xaf30c40 <col:21> 'ap_fixed (const ap_fixed)' lvalue CXXMethod 0xaf14700 'operator+' 'ap_fixed (const ap_fixed)'
  | |           |-DeclRefExpr 0xaf30a08 <col:19> 'ap_fixed' lvalue Var 0xaf30728 'm' 'ap_fixed'
  | |           `-CXXConstructExpr 0xaf30c08 <col:23> 'ap_fixed' 'void (const ap_fixed &) noexcept'
  | |             `-ImplicitCastExpr 0xaf30a58 <col:23> 'const ap_fixed' lvalue <NoOp>
  | |               `-DeclRefExpr 0xaf30a30 <col:23> 'ap_fixed' lvalue Var 0xaf30868 'n' 'ap_fixed'
  | |-NullStmt 0xaf30e50 <line:17:5>

      if (const ExprWithCleanups *Cleanup = dyn_cast<ExprWithCleanups>(decl_init))
        if(isa<CXXConstructExpr>(Cleanup->getSubExpr())) continue;
      */

      for (auto bind_op: BindOpAttrs) { 
        auto var_expr = bind_op->getVariable();
        if (!isa<DeclRefExpr>(var_expr)) { 
          //the variable for bind_op is possible  MemberExpr, skip check it
          continue;
        }
        auto var_ref = static_cast<DeclRefExpr*>(var_expr);

        if (decl == var_ref->getDecl() && 
            !var_decl->isDirectInit()) {
          auto new_attr = XlxBindOpExprAttr::CreateImplicit(
              getContext(), bind_op->getVariable(), bind_op->getOp(), bind_op->getImpl(),
              bind_op->getLatency(), bind_op->getRange());
          decl->addAttr(new_attr);
        }
      }
    }
    break;
  }
  case Stmt::LabelStmtClass: {
    LabelStmt *labelStmt = dyn_cast<LabelStmt>(stmt);
    Stmt *subStmt = labelStmt->getSubStmt();
    LowerBindOpScope(subStmt, BindOpAttrs);
    labelStmt->setSubStmt(subStmt);
    break;
  }
  case Stmt::AttributedStmtClass: {
    AttributedStmt *attributedStmt = dyn_cast<AttributedStmt>(stmt);
    Stmt *subStmt = attributedStmt->getSubStmt();
    auto attrs = attributedStmt->getAttrs();
    for (auto attr : attrs) {
      if (isa<XlxBindOpAttr>(attr)) {

        auto bindOp = dyn_cast<XlxBindOpAttr>(attr);
        BindOpAttrs.push_back(bindOp);
        // strip null terminator
      }
    }
    LowerBindOpScope(subStmt, BindOpAttrs);
    attributedStmt->setSubStmt(subStmt);
    break;
  }
  case Stmt::DefaultStmtClass: {
    auto caseStmt = dyn_cast<DefaultStmt>(stmt);
    auto subStmt = caseStmt->getSubStmt();
    LowerBindOpScope(subStmt, BindOpAttrs);
    caseStmt->setSubStmt(subStmt);
    break;
  }
  case Stmt::CaseStmtClass: {
    auto caseStmt = dyn_cast<CaseStmt>(stmt);
    auto subStmt = caseStmt->getSubStmt();
    LowerBindOpScope(subStmt, BindOpAttrs);
    caseStmt->setSubStmt(subStmt);
    break;
  }
  case Stmt::SwitchStmtClass: { 
    auto switchStmt = dyn_cast<SwitchStmt>(stmt);
    auto body = switchStmt->getBody();
    LowerBindOpScope(body, BindOpAttrs);
    switchStmt->setBody(body);
    break;
  }
  case Stmt::IfStmtClass: {
    auto ifStmt = dyn_cast<IfStmt>(stmt);
    auto thenStmt = ifStmt->getThen();
    auto elseStmt = ifStmt->getElse();
    if (thenStmt) {
      LowerBindOpScope(thenStmt, BindOpAttrs);
      ifStmt->setThen(thenStmt);
    }

    if (elseStmt) {
      LowerBindOpScope(elseStmt, BindOpAttrs);
      ifStmt->setElse(elseStmt);
    }
    break;
  }
  case Stmt::WhileStmtClass: {
    auto whileStmt = dyn_cast<WhileStmt>(stmt);
    auto body = whileStmt->getBody();
    LowerBindOpScope(body, BindOpAttrs);
    whileStmt->setBody(body);
    break;
  }
  case Stmt::DoStmtClass: {
    auto doStmt = dyn_cast<DoStmt>(stmt);
    auto body = doStmt->getBody();
    LowerBindOpScope(body, BindOpAttrs);
    break;
  }
  case Stmt::ForStmtClass: {
    auto forStmt = dyn_cast<ForStmt>(stmt);
    Stmt *body = forStmt->getBody();
    LowerBindOpScope(body, BindOpAttrs);
    forStmt->setBody(body);
    break;
  }
  case Stmt::CXXForRangeStmtClass: { 
    auto forStmt = dyn_cast<CXXForRangeStmt>(stmt);
    Stmt *body = forStmt->getBody();
    LowerBindOpScope(body, BindOpAttrs);
    forStmt->setBody(body);
    break;
  }
  default:
    break;
  }
}

void CodeGenFunction::EmitBundleForScope(
    const Stmt *SubStmt, ArrayRef<const Attr *> Attrs,
    SmallVectorImpl<llvm::OperandBundleDef> &BundleList) {
  // auto &Ctx = Builder.getContext();
  SmallVector<llvm::Value*, 4> ComputeRegionPtrs;
  if (isa<NullStmt>(SubStmt)) { 
    return ;
  }

  for (auto *A : Attrs) {
    switch (A->getKind()) {
    default:
      break;
    case attr::XCLSingleWorkitem:
      BundleList.emplace_back(A->getSpelling(), None);
      break;
    case attr::XCLPipelineWorkitems: {
      // xilinx "xcl_pipeline_workitems" support workitems
      int II = ExtractAttrInteger(A, cast<XCLPipelineWorkitemsAttr>(A)->getII(),
                                  CGM.getDiags(), getContext(), /*LB*/ -1,
                                  /*UB*/ INT32_MAX, /*Default*/ -1);

      BundleList.emplace_back(A->getSpelling(), Builder.getInt32(II));
      break;
    }
    case attr::XCLUnrollWorkitems:
      BundleList.emplace_back(
          A->getSpelling(),
          Builder.getInt32(cast<XCLUnrollWorkitemsAttr>(A)->getUnrollHint()));
      break;
    case attr::XCLArrayView: {
      auto *View = cast<XCLArrayViewAttr>(A);
      auto LV = EmitLValue(View->getArray());
      SmallVector<llvm::Value *, 4> Shape;
      for (auto *E : View->shape())
        Shape.push_back(EmitScalarExpr(E));

      auto ArrayPtr = EmitLoadOfLValue(LV, SourceLocation());
      SmallVector<llvm::OperandBundleDef, 4> ScopeAttrs;
      ScopeAttrs.emplace_back(View->getSpelling(), Shape);
      switch (View->getAccessMode()) {
      case XCLArrayViewAttr::Readonly:
        ScopeAttrs.emplace_back("xcl_read_only", None);
        break;
      case XCLArrayViewAttr::Writeonly:
        ScopeAttrs.emplace_back("xcl_write_only", None);
        break;
      case XCLArrayViewAttr::ReadWrite:
        break;
      }

      auto *V = ArrayPtr.getScalarVal();
      auto *SSACopy = llvm::Intrinsic::getDeclaration(
          CurFn->getParent(), llvm::Intrinsic::ssa_copy, V->getType());

      auto *Copy = Builder.CreateCall(SSACopy, V, ScopeAttrs, V->getName());
      ComputeRegionPtrs.push_back(Copy);
      break;
    }
    case attr::XCLOutline: {
      // auto Name = cast<XCLOutlineAttr>(A)->getName();
      // auto *Str = llvm::MDString::get(Ctx, Name);
      BundleList.emplace_back(A->getSpelling(), None);
      break;
    }
    case attr::XCLInline: {
      auto Recursive = cast<XCLInlineAttr>(A)->getRecursive();
      BundleList.emplace_back(A->getSpelling(), Builder.getInt32(Recursive));
      break;
    }
    case attr::XlxExprBalance: {
      auto Enabled = cast<XlxExprBalanceAttr>(A)->getEnabled();
      BundleList.emplace_back(A->getSpelling(), Builder.getInt32(Enabled));
      break;
    }
    case attr::XlxMergeLoop: {
      auto Force = cast<XlxMergeLoopAttr>(A)->getForce();
      BundleList.emplace_back(A->getSpelling(), Builder.getInt32(Force));
      break;
    }
    case attr::FPGAResourceLimitHint: {
      auto resourceLimit = dyn_cast<FPGAResourceLimitHintAttr>(A);
      auto Limit = resourceLimit->getLimit();
      auto LimitInt = EvaluateInteger(Limit, getContext(), /*Default*/ 0);
      auto InstanceType = resourceLimit->getInstanceType()->getName();
      auto Name = resourceLimit->getInstanceName()->getName();
      llvm::Value *Args[] = {llvm::ConstantDataArray::getString(getLLVMContext(),Name),
                             llvm::ConstantDataArray::getString(getLLVMContext(), InstanceType),
                             Builder.getInt32(LimitInt)

      };
      SmallVector<llvm::OperandBundleDef, 1> bundleDefs;
      BundleList.emplace_back(A->getSpelling(), Args);
      break;
    }
    case attr::XlxFunctionAllocation: {
      auto functionAlloc = dyn_cast<XlxFunctionAllocationAttr>(A);
      auto func_pointer = functionAlloc->getFunction();

      if (isa<UnaryOperator>(func_pointer) &&
            dyn_cast<UnaryOperator>(func_pointer)->getOpcode() == UO_AddrOf) {
          UnaryOperator *up = dyn_cast<UnaryOperator>(func_pointer);
          func_pointer = up->getSubExpr();
      }

      FunctionDecl *FD = nullptr;
      if (isa<DeclRefExpr>(func_pointer)) {
        auto value_decl = dyn_cast<DeclRefExpr>(func_pointer)->getDecl();
        assert(isa<FunctionDecl>(value_decl) && "unexpected,  Semantic check should have checked the Decl type");
        FD = dyn_cast<FunctionDecl>(value_decl);
      }
      else if(isa<MemberExpr>(func_pointer)){
        auto member_func = dyn_cast<MemberExpr>(func_pointer);
        NamedDecl *ND = member_func->getMemberDecl();
        assert(isa<FunctionDecl>(ND) && "unexpected, no member function ");
        FD = dyn_cast<FunctionDecl>(ND);
      }
      else { 
        CGM.getDiags().Report(func_pointer->getLocStart(),
               diag::warn_xlx_attribute_ignore_because_invalid_option)
            << "ALLOCATION"
            << "Instances value is not valid function pointer expression";
        return ;
      }
      assert( FD && "unexpected, instances is not function, Sematic checker should had reported it "); 

      if (FD->getAttr<AlwaysInlineAttr>()) { 
        CGM.getDiags().Report(functionAlloc->getLocation(),
                          diag::warn_allocation_conflict)
                      << "inline" ;
        break;
      }
      auto Limit = functionAlloc->getLimit();
      auto LimitInt = EvaluateInteger(Limit, getContext(), /*Default*/ 0);
      llvm::Value *Args[] = {CGM.GetAddrOfGlobal(FD),
                             llvm::ConstantDataArray::getString(getLLVMContext(), "function"),
                             Builder.getInt32(LimitInt)
      };

      BundleList.emplace_back(A->getSpelling(), Args);
      break;
    }
    case attr::XlxOccurrence: {
      auto Cycle = cast<XlxOccurrenceAttr>(A)->getCycle();
      auto CycleInt = EvaluateInteger(Cycle, getContext(), /*Default*/ 1);
      BundleList.emplace_back(A->getSpelling(), Builder.getInt32(CycleInt));
    }; break;
    case attr::XlxProtocol:
      BundleList.emplace_back(
          A->getSpelling(),
          Builder.getInt32(cast<XlxProtocolAttr>(A)->getProtocolMode()));
      break;
    case attr::XCLLatency: {
      int Min = EvaluateInteger(cast<XCLLatencyAttr>(A)->getMin(), getContext(),
                                /*Default*/ 0);
      int Max = EvaluateInteger(cast<XCLLatencyAttr>(A)->getMax(), getContext(),
                                /*Default*/ 65535);

      llvm::Value *LatencyArray[] = {Builder.getInt32(Min),
                                     Builder.getInt32(Max)};
      BundleList.emplace_back(A->getSpelling(), LatencyArray);
      break;
    }
    case attr::XlxBindOpExpr: {
      auto *bindOp = dyn_cast<XlxBindOpExprAttr>(A);
      BundleBindOpAttr(bindOp, BundleList);
      break;
    }
    }
  }

  if (!ComputeRegionPtrs.empty())
    BundleList.emplace_back("fpga_compute_region", ComputeRegionPtrs);
}

void CodeGenFunction::EmitStableIntrinsic(const XlxStableAttr *A) {
  llvm::Value *addr = nullptr;
  Expr *expr = A->getVariable();
  DeclRefExpr *t = dyn_cast<DeclRefExpr>(expr);
  VarDecl *stable_var = nullptr;
  stable_var = dyn_cast<VarDecl>(t->getDecl());
  if (stable_var != nullptr) {
    if (stable_var->isLocalVarDecl()) {
      // local variable
      addr = GetAddrOfLocalVar(stable_var).getPointer();
    } else if (stable_var->getKind() == Decl::ParmVar) {
      auto *parm = dyn_cast<ParmVarDecl>(stable_var);
      int idx = parm->getFunctionScopeIndex();
      llvm::Argument *args = CurFn->arg_begin();
      assert(idx < (int)CurFn->arg_size() &&
             "paramVarDecl is can not find in llvm::function");
      addr = &args[idx];
    } else {
      // global variable
      addr = CGM.GetAddrOfGlobalVar(stable_var);
    }
  }
  llvm::Value *args[] = {
      addr,
  };

  SmallVector<llvm::OperandBundleDef, 6> bundleDefs;
  bundleDefs.emplace_back("stable", args);

  auto *stableDecl = llvm::Intrinsic::getDeclaration(
      &(CGM.getModule()), llvm::Intrinsic::sideeffect);

  auto *call = Builder.CreateCall(stableDecl, None, bundleDefs);
  call->setOnlyAccessesInaccessibleMemory();
  call->setDoesNotThrow();
}

void CodeGenFunction::EmitStableContentIntrinsic(
    const XlxStableContentAttr *A) {
  llvm::Value *addr = nullptr;
  Expr *expr = A->getVariable();
  DeclRefExpr *t = dyn_cast<DeclRefExpr>(expr);
  VarDecl *stable_var = nullptr;
  stable_var = dyn_cast<VarDecl>(t->getDecl());
  if (stable_var != nullptr) {
    if (stable_var->isLocalVarDecl()) {
      // local variable
      addr = GetAddrOfLocalVar(stable_var).getPointer();
    } else if (stable_var->getKind() == Decl::ParmVar) {
      auto *parm = dyn_cast<ParmVarDecl>(stable_var);
      int idx = parm->getFunctionScopeIndex();
      llvm::Argument *args = CurFn->arg_begin();
      assert(idx < (int)CurFn->arg_size() &&
             "paramVarDecl is can not find in llvm::function");
      addr = &args[idx];
    } else {
      // global variable
      addr = CGM.GetAddrOfGlobalVar(stable_var);
    }
  }
  llvm::Value *args[] = {
      addr,
  };

  SmallVector<llvm::OperandBundleDef, 6> bundleDefs;
  bundleDefs.emplace_back("stable_content", args);

  auto *stableDecl = llvm::Intrinsic::getDeclaration(
      &(CGM.getModule()), llvm::Intrinsic::sideeffect);

  auto *call = Builder.CreateCall(stableDecl, None, bundleDefs);
  call->setOnlyAccessesInaccessibleMemory();
  call->setDoesNotThrow();
}

void CodeGenFunction::EmitSharedIntrinsic(const XlxSharedAttr *A) {
  llvm::Value *V = nullptr;
  Expr *E = A->getVariable();
  DeclRefExpr *t = dyn_cast<DeclRefExpr>(E);
  VarDecl *SharedV = nullptr;
  SharedV = dyn_cast<VarDecl>(t->getDecl());
  if (SharedV) {
    if (SharedV->isLocalVarDecl()) {
      // local variable
      V = GetAddrOfLocalVar(SharedV).getPointer();
    } else if (SharedV->getKind() == Decl::ParmVar) {
      auto *Parm = dyn_cast<ParmVarDecl>(SharedV);
      int idx = Parm->getFunctionScopeIndex();
      llvm::Argument *Args = CurFn->arg_begin();
      assert(idx < (int)CurFn->arg_size() &&
             "paramVarDecl cannot be foundd in llvm::function");
      V = &Args[idx];
    } else {
      // global variable
      V = CGM.GetAddrOfGlobalVar(SharedV);
    }
  }
  llvm::Value *Args[] = {V};

  SmallVector<llvm::OperandBundleDef, 6> bundleDefs;
  bundleDefs.emplace_back("shared", Args);

  auto *SharedDecl = llvm::Intrinsic::getDeclaration(
      &(CGM.getModule()), llvm::Intrinsic::sideeffect);

  auto *CI = Builder.CreateCall(SharedDecl, None, bundleDefs);
  CI->setOnlyAccessesInaccessibleMemory();
  CI->setDoesNotThrow();
}

void CodeGenFunction::EmitBindStorageIntrinsic(
    const XlxBindStorageAttr *bindStorage) {
  llvm::Value *v = nullptr;
  Expr *E = bindStorage->getVariable();
  llvm::Value *pointer = nullptr;

  if (E->getType()->isPointerType()) {
    // pointer variable
    pointer = EmitScalarExpr(E);
  } else {
    // array variable, or struct with only one array field
    LValue lvalue = EmitLValue(E);
    pointer = lvalue.getPointer();
  }

  auto latency =
      EvaluateInteger(bindStorage->getLatency(), getContext(), /*Default*/ -1);
  auto impl = (platform::PlatformBasic::IMPL_TYPE)EvaluateInteger(
      bindStorage->getImpl(), getContext(), -1);
  const platform::PlatformBasic *XilinxPlatform =
      platform::PlatformBasic::getInstance();

  auto range =
      XilinxPlatform->verifyLatency(platform::PlatformBasic::OP_MEMORY, impl);
  if (latency != -1 && (latency < range.first || latency > range.second)) {
    CGM.getDiags().Report(bindStorage->getLocation(),
                          diag::err_latency_value_is_out_of_range)
        << latency
        << "[" + std::to_string(range.first) + ", " +
               std::to_string(range.second) + "]";
  }

  llvm::Value *Args[] = {pointer,
                         Builder.getInt32(platform::PlatformBasic::OP_MEMORY),
                         Builder.getInt32(impl), Builder.getInt32(latency)};

  SmallVector<llvm::OperandBundleDef, 6> bundleDefs;
  bundleDefs.emplace_back("xlx_bind_storage", Args);

  auto *bindStorageDecl = llvm::Intrinsic::getDeclaration(
      &(CGM.getModule()), llvm::Intrinsic::sideeffect);
  auto *CI = Builder.CreateCall(bindStorageDecl, None, bundleDefs);
  CI->setOnlyAccessesInaccessibleMemory();
  CI->setDoesNotThrow();
}

// TODO, need good name, applyReqdPipeDepthAttr ?
void CodeGenFunction::EmitReqdPipeDepthIntrinsic(
    const XlxReqdPipeDepthAttr *stream) {

  // it is about memory attribute, s
  // TODO, error out , if variable value  is not varRefExpr
  llvm::Value *V = nullptr;
  Expr *E = stream->getVariable();

  if (E->getType()->isPointerType()) {
    // pointer variable
    V = EmitScalarExpr(E);
  } else {
    // array variable, or struct with only one array field
    LValue lvalue = EmitLValue(E);
    V = lvalue.getPointer();
  }
  auto Depth = EvaluateInteger(stream->getDepth(), getContext(), /*Default*/ 1);
  llvm::Value *Args[] = {V, Builder.getInt32(Depth)};

  SmallVector<llvm::OperandBundleDef, 6> bundleDefs;
  if (stream->getOff()) {
    bundleDefs.emplace_back("xcl_fpga_pipo_depth", Args);
  } else {
    bundleDefs.emplace_back(stream->getSpelling(), Args);
  }

  auto *setStreamDepth = llvm::Intrinsic::getDeclaration(
      &(CGM.getModule()), llvm::Intrinsic::sideeffect);

  auto *CI = Builder.CreateCall(setStreamDepth, None, bundleDefs);
  CI->setOnlyAccessesInaccessibleMemory();
  CI->setDoesNotThrow();
}

// xlx_array_partition, xlx_array_reshape
void CodeGenFunction::EmitArrayXFormIntrinsic(const XlxArrayXFormAttr *A) {
  llvm::Value *V = nullptr;
  Expr *E = A->getVariable();
  if (E->getType()->isPointerType()) {
    // pointer variable, it most propbaly is function argument
    V = EmitScalarExpr(E);
  } else {
    // array variable, or struct with only one array field
    LValue lvalue = EmitLValue(E);
    V = lvalue.getPointer();
    llvm::Value *const0 = Builder.getInt32(0);
    llvm::Value *idxs[] = {const0, const0};
    V = Builder.CreateInBoundsGEP(V, idxs);
  }
  Expr *DimE = A->getDim();
  Expr *FactorE = A->getFactor();
  /*
    enum XlxArrayXFromType {
      Cyclic,
      Block,
      Complete
    };
  */
  XlxArrayXFormAttr::XlxArrayXFormType xtype = A->getType();

  llvm::Value *Args[] = {V, Builder.getInt32(xtype), EmitScalarExpr(FactorE),
                         EmitScalarExpr(DimE)};

  SmallVector<llvm::OperandBundleDef, 6> bundleDefs;
  bundleDefs.emplace_back(A->getSpelling(), Args);

  auto *arrayXFormDecl = llvm::Intrinsic::getDeclaration(
      &(CGM.getModule()), llvm::Intrinsic::sideeffect);

  auto *CI = Builder.CreateCall(arrayXFormDecl, None, bundleDefs);
  CI->setOnlyAccessesInaccessibleMemory();
  CI->setDoesNotThrow();
}


//for TopFunction use CC_FPGAAccel ccalling conversion , this need 
//the 
void CodeGenFunction::EmitDisaggrIntrinsic(const XlxDisaggrAttr *A) {
  llvm::Value* V = nullptr;
  Expr *E = A->getVariable();
  if (E->getType()->isPointerType()) {
    // pointer variable, it most propbaly is function argument
    V = EmitScalarExpr(E);
  } 
  else {
    Expr *base = nullptr;
    while (auto *Cast = dyn_cast<ImplicitCastExpr>(E)){
      E = Cast->getSubExpr();
    }

    if(isa<DeclRefExpr>(E)) { 
      //for expr : variable = var
      base = E; 
    }
    else if (isa<MemberExpr>(E)) { 
      //for expr, var.filed1.field2
      Expr* t = E;
      while(isa<MemberExpr>(t)) { 
        t = cast<MemberExpr>(t)->getBase();
      }
      base = t;
    }
    else { 
      //unexpected, emit diagnostic message
      CGM.getDiags().Report(E->getExprLoc(),
           diag::warn_invalid_variable_expr);
      return;
    }

    if (base->getType()->isPointerType()) { 
      //it is not Struct TopArgument , and use Pointer for ParmDecl
      V = EmitLValue(E).getPointer();
    }
    else{ 
      if (!isa<DeclRefExpr>(base)) {
        //unexpected variable-expression , emit diagnostic message
        CGM.getDiags().Report(base->getExprLoc(),
           diag::warn_invalid_variable_expr);
        return ;
      }

      VarDecl *DisaggrV = nullptr;
      DisaggrV = dyn_cast<VarDecl>(cast<DeclRefExpr>(base)->getDecl());
      if (!DisaggrV) { 
        //unexpected variable-expression , emit diagnostic message
        CGM.getDiags().Report(base->getExprLoc(),
           diag::warn_invalid_variable_expr);
        return ;
      }

      if (DisaggrV->isLocalVarDecl()) {
       // local variable
       V = EmitLValue(E).getPointer();
      } else if (DisaggrV->getKind() == Decl::ParmVar) {
        auto *Parm = cast<ParmVarDecl>(DisaggrV);
        int idx = Parm->getFunctionScopeIndex();
        llvm::Argument *Args = CurFn->arg_begin();
        assert(idx < (int)CurFn->arg_size() &&
               "paramVarDecl cannot be foundd in llvm::function");
        V = &Args[idx];

        if (V->getType()->isPointerTy()) { 
          //it is not Struct TopArgument , and use Pointer for ParmDecl
          V = EmitLValue(E).getPointer();
        }
        else { 
          //struct TopArgument
          Expr* t = E;
          SmallVector<FieldDecl*, 4> fields;
          while( isa<MemberExpr>(t)) { 
            ValueDecl *sub = cast<MemberExpr>(t)->getMemberDecl();
            if (isa<FieldDecl>(sub)) { 
              fields.push_back(cast<FieldDecl>(sub));
            }
            else{ 
              CGM.getDiags().Report(E->getExprLoc(),
                  diag::warn_invalid_variable_expr);
              return ;
            }
            t = cast<MemberExpr>(t)->getBase();
          }
  
          SmallVector<unsigned int, 4> idxs;
          std::string name = V->getName().str();
          while (!fields.empty()) { 
            FieldDecl * field = fields.back();
            int idx = field->getFieldIndex();
            idxs.push_back(idx);
            name = name + "." + field->getName().str();
            fields.pop_back();
          }
          if (idxs.size() > 0) { 
            V = Builder.CreateExtractValue(V, makeArrayRef(idxs), name);
          }
        }
      }
      else {
        V = EmitLValue(E).getPointer();
      }
    }
  }

  llvm::Value *Args[] = {V};

  SmallVector<llvm::OperandBundleDef, 6> bundleDefs;
  bundleDefs.emplace_back("disaggr", Args);

  auto *DisaggrDecl = llvm::Intrinsic::getDeclaration(
      &(CGM.getModule()), llvm::Intrinsic::sideeffect);

  auto *CI = Builder.CreateCall(DisaggrDecl, None, bundleDefs);
  CI->setOnlyAccessesInaccessibleMemory();
  CI->setDoesNotThrow();
}

void CodeGenFunction::EmitAggregateIntrinsic(const XlxAggregateAttr *A) {
  llvm::Value *V = nullptr;
  Expr *E = A->getVariable();
  if (E->getType()->isPointerType()) {
    V = EmitScalarExpr(E);
  } else {
    LValue lvalue = EmitLValue(E);
    V = lvalue.getPointer();
  }

  auto compact = A->getCompact();
  llvm::Value * compact_v = Builder.getInt64(compact);
  llvm::Value *Args[] = {V, compact_v };

  SmallVector<llvm::OperandBundleDef, 6> bundleDefs;
  bundleDefs.emplace_back("aggregate", Args);

  auto *sideeffect = llvm::Intrinsic::getDeclaration(&(CGM.getModule()),
                                                   llvm::Intrinsic::sideeffect);

  auto *CI = Builder.CreateCall(sideeffect, None, bundleDefs);
  CI->setOnlyAccessesInaccessibleMemory();
  CI->setDoesNotThrow();
}
void CodeGenFunction::EmitDataPackIntrinsic(const XlxDataPackAttr* A) 
{
  llvm::Value *V = nullptr;
  Expr *E = A->getVariable();
  if (E->getType()->isPointerType()) {
    V = EmitScalarExpr(E);
  } else {
    LValue lvalue = EmitLValue(E);
    V = lvalue.getPointer();
  }

  int aggregate_compact = (int)XlxAggregateAttr::bit;
  auto level = A->getBytePadLevel();
  if (level == XlxDataPackAttr::field_level) { 
    //translate to "XlxAggregateAttr::byte"
    aggregate_compact = (int)XlxAggregateAttr::byte;
  }
  llvm::Value * level_v = Builder.getInt64(aggregate_compact);
  llvm::Value *Args[] = {V, level_v };

  SmallVector<llvm::OperandBundleDef, 6> bundleDefs;
  bundleDefs.emplace_back("aggregate", Args);

  auto *sideeffect = llvm::Intrinsic::getDeclaration(&(CGM.getModule()),
                                                   llvm::Intrinsic::sideeffect);

  auto *CI = Builder.CreateCall(sideeffect, None, bundleDefs);
  CI->setOnlyAccessesInaccessibleMemory();
  CI->setDoesNotThrow();

}

void CodeGenFunction::EmitHLSConstIntrinsic(llvm::Value *var) {
  llvm::Value *Args[] = {var};

  SmallVector<llvm::OperandBundleDef, 1> bundleDefs;
  bundleDefs.emplace_back("const", Args);

  auto *hls_const = llvm::Intrinsic::getDeclaration(
      &(CGM.getModule()), llvm::Intrinsic::sideeffect);

  auto *CI = Builder.CreateCall(hls_const, None, bundleDefs);
  CI->setOnlyAccessesInaccessibleMemory();
  CI->setDoesNotThrow();
}

void CodeGenFunction::EmitXlxCrossDependenceIntrinsic(const XlxCrossDependenceAttr *attr) { 
  Expr * var0 = attr->getCrossVar0();
  Expr * var1 = attr->getCrossVar1();
  llvm::Value* addr0 = nullptr;
  llvm::Value* addr1 = nullptr;
  if( var0->getType()->isPointerType()){
    //pointer variable
    addr0 = EmitScalarExpr(var0);
  }
  else {
    //array variable, or struct with only one array field 
    LValue lvalue = EmitLValue(var0);
    addr0 = lvalue.getPointer();
  }

  if( var1->getType()->isPointerType()){
    //pointer variable
    addr1 = EmitScalarExpr(var1);
  }
  else {
    //array variable, or struct with only one array field 
    LValue lvalue = EmitLValue(var1);
    addr1 = lvalue.getPointer();
  }
  
  
  auto dep_class = attr->getXClass();
  auto dep_compel = attr->getCompel();
  auto dep_type = attr->getType();
  auto dep_direction = attr->getDirection();
  auto dep_distance = attr->getDistance();
  auto *dependence = llvm::Intrinsic::getDeclaration(
        CurFn->getParent(), llvm::Intrinsic::sideeffect);

  llvm::Value* args[] = {
       addr0,
       addr1,
       Builder.getInt32(dep_class),
       Builder.getInt32(dep_compel), 
       Builder.getInt32(dep_direction -1 ), 
       Builder.getInt32(dep_distance),
       Builder.getInt32(dep_type)
  };

  SmallVector<llvm::OperandBundleDef, 6> bundleDefs;
  bundleDefs.emplace_back( "fpga.cross.dependence", args );

  auto* call = Builder.CreateCall(dependence, None, bundleDefs );
  call->setOnlyAccessesInaccessibleMemory();
  call->setDoesNotThrow();
}

// this is step 2 , will be commit after step1 that is  clang parser part
void CodeGenFunction::EmitResourceLimitIntrinsic(const FPGAResourceLimitHintAttr* resourceLimit) 
{
  auto Limit = resourceLimit->getLimit();
  auto LimitInt = EvaluateInteger(Limit, getContext(), /*Default*/ 0);
  auto InstanceType = resourceLimit->getInstanceType()->getName();
  auto Name = resourceLimit->getInstanceName()->getName();
  llvm::Value *Args[] = {llvm::ConstantDataArray::getString(getLLVMContext(), Name),
                         llvm::ConstantDataArray::getString(getLLVMContext(), InstanceType),
                         Builder.getInt32(LimitInt)

  };
  SmallVector<llvm::OperandBundleDef, 1> bundleDefs;
  bundleDefs.emplace_back(resourceLimit->getSpelling(), Args);

  auto *hls_resource_limit = llvm::Intrinsic::getDeclaration(&(CGM.getModule()),
                                                   llvm::Intrinsic::sideeffect);

  auto *CI = Builder.CreateCall(hls_resource_limit, None, bundleDefs);
  CI->setOnlyAccessesInaccessibleMemory();
  CI->setDoesNotThrow();
}

void CodeGenFunction::EmitFunctionAllocationIntrinsic(const XlxFunctionAllocationAttr* functionAlloc) 
{
  auto func_pointer = functionAlloc->getFunction();
  if (isa<UnaryOperator>(func_pointer) &&
        dyn_cast<UnaryOperator>(func_pointer)->getOpcode() == UO_AddrOf) {
      UnaryOperator *up = dyn_cast<UnaryOperator>(func_pointer);
      func_pointer = up->getSubExpr();
  }

  FunctionDecl * FD = nullptr;
  if (isa<DeclRefExpr>(func_pointer)) {
    auto value_decl = dyn_cast<DeclRefExpr>(func_pointer)->getDecl();
    assert(isa<FunctionDecl>(value_decl) && "unexpected,  Semantic check should have checked the Decl type");
    FD = dyn_cast<FunctionDecl>(value_decl);
  }
  else if(isa<MemberExpr>(func_pointer)){
    auto member_func = dyn_cast<MemberExpr>(func_pointer);
    NamedDecl *ND = member_func->getMemberDecl();
    assert(isa<FunctionDecl>(ND) && "unexpected, no member function ");
    FD = dyn_cast<FunctionDecl>(ND);
  }
  else { 
    CGM.getDiags().Report(func_pointer->getLocStart(),
           diag::warn_xlx_attribute_ignore_because_invalid_option)
        << "ALLOCATION"
        << "Instances value is not valid function pointer expression";
    return ;
  }
  assert( FD && "unexpected, function allocation 's instances option is not an function pointer");
  if (FD->getAttr<AlwaysInlineAttr>()) { 
    CGM.getDiags().Report(functionAlloc->getLocation(),
                          diag::warn_allocation_conflict)
        << "inline" ;
    return;
  }

  auto Limit = functionAlloc->getLimit();
  auto LimitInt = EvaluateInteger(Limit, getContext(), /*Default*/ 0);

  assert(FD && "unexpected, instances options is not function");

  llvm::Value* llvm_func = CGM.GetAddrOfGlobal(FD);

  llvm::Value *Args[] = { llvm_func,
                         llvm::ConstantDataArray::getString(getLLVMContext(), "function"),
                         Builder.getInt32(LimitInt) 
  };
  SmallVector<llvm::OperandBundleDef, 1> bundleDefs;
  bundleDefs.emplace_back(functionAlloc->getSpelling(), Args);

  auto *hls_func_alloc = llvm::Intrinsic::getDeclaration(&(CGM.getModule()),
                                                   llvm::Intrinsic::sideeffect);

  auto *CI = Builder.CreateCall(hls_func_alloc, None, bundleDefs);
  CI->setOnlyAccessesInaccessibleMemory();
  CI->setDoesNotThrow();
}

void CodeGenFunction::EmitSAXILITEOffsetIntrinsic( const SAXILITEOffsetInterfaceAttr *interface) 
{ 
  if (!CurFuncDecl->hasAttr<SDxKernelAttr>()) { 
    return ;
  }
  Expr *port = interface->getPort();
  auto lvalue = EmitLValue(port);
  auto port_var = lvalue.getPointer();
  auto bundName = llvm::ConstantDataArray::getString(getLLVMContext(), interface->getBundleName(), false);
  auto offset = Builder.getInt64(EvaluateInteger(interface->getOffset(), getContext(), -1));
  auto isRegister = Builder.getInt1(interface->getIsRegister());
  auto signalName = llvm::ConstantDataArray::getString(getLLVMContext(), interface->getSignalName(), false);

  llvm::Value *Args[] = { port_var, bundName, offset, isRegister, signalName };
  SmallVector<llvm::OperandBundleDef, 1> bundleDefs;
  bundleDefs.emplace_back("xlx_s_axilite", Args);

  auto *sideeffect = llvm::Intrinsic::getDeclaration(&CGM.getModule(), llvm::Intrinsic::sideeffect);
  auto *CI = Builder.CreateCall(sideeffect, None, bundleDefs);
  CI->setOnlyAccessesInaccessibleMemory();
  CI->setDoesNotThrow();
}

void CodeGenFunction::EmitMAXIInterfaceIntrinsic( const MAXIInterfaceAttr *interface) 
{
  if (!CurFuncDecl->hasAttr<SDxKernelAttr>()) { 
    return ;
  }
  Expr *port = interface->getPort();
  auto lvalue = EmitLValue(port);
  auto port_var = lvalue.getPointer();
  StringRef bundleName = interface->getBundleName();
  int depth = EvaluateInteger(interface->getDepth(), getContext(), -1);
  StringRef offsetType = interface->getOffset();
  StringRef signalName = interface->getSignalName();
  int num_read_outstanding = EvaluateInteger(interface->getNumReadOutstanding(), getContext());
  int num_write_outstanding = EvaluateInteger(interface->getNumWriteOutstanding(), getContext());
  int max_read_burst_length = EvaluateInteger(interface->getMaxReadBurstLength(), getContext());
  int max_write_burst_length = EvaluateInteger(interface->getMaxWriteBurstLength(), getContext());
  int latency = EvaluateInteger(interface->getLatency(), getContext());
  int max_widen_bitwidth = EvaluateInteger(interface->getMaxWidenBitWidth(), getContext());
  llvm::Value *Args[] = {
    port_var, 
    llvm::ConstantDataArray::getString(getLLVMContext(), bundleName, false), 
    Builder.getInt64(depth), 
    llvm::ConstantDataArray::getString(getLLVMContext(), offsetType, false), 
    llvm::ConstantDataArray::getString(getLLVMContext(), signalName, false),
    Builder.getInt64(num_read_outstanding),
    Builder.getInt64(num_write_outstanding),
    Builder.getInt64(max_read_burst_length),
    Builder.getInt64(max_write_burst_length),
    Builder.getInt64(latency),
    Builder.getInt64(max_widen_bitwidth) 
  };
                    
  SmallVector<llvm::OperandBundleDef, 1> bundleDefs;
  bundleDefs.emplace_back("xlx_m_axi", Args);

  auto *sideeffect = llvm::Intrinsic::getDeclaration(&CGM.getModule(), llvm::Intrinsic::sideeffect);
  auto *CI = Builder.CreateCall(sideeffect, None, bundleDefs);
  CI->setOnlyAccessesInaccessibleMemory();
  CI->setDoesNotThrow();

}

void CodeGenFunction::EmitAXIStreamInterfaceIntrinsic( const AXIStreamInterfaceAttr* interface)
{
  if (!CurFuncDecl->hasAttr<SDxKernelAttr>()) { 
    return ;
  }
  Expr *port = interface->getPort();
  auto lvalue = EmitLValue(port);
  auto port_var = lvalue.getPointer();
  bool isRegister = interface->getIsRegister();
  int registerMode = (int)interface->getRegisterMode();
  int depth = EvaluateInteger(interface->getDepth(), getContext());
  StringRef signalName = interface->getSignalName();

  llvm::Value* Args[] = {
    port_var,
    Builder.getInt1(isRegister),
    Builder.getInt64(registerMode), 
    Builder.getInt64(depth), 
    llvm::ConstantDataArray::getString(getLLVMContext(), signalName, false)
  };
  SmallVector<llvm::OperandBundleDef, 1> bundleDefs;
  bundleDefs.emplace_back("xlx_axis", Args);

  auto *sideeffect = llvm::Intrinsic::getDeclaration(&CGM.getModule(), llvm::Intrinsic::sideeffect);
  auto *CI = Builder.CreateCall(sideeffect, None, bundleDefs);
  CI->setOnlyAccessesInaccessibleMemory();
  CI->setDoesNotThrow();
}

void CodeGenFunction::EmitMemoryInterfaceIntrinsic( const MemoryInterfaceAttr *interface) 
{
  if (!CurFuncDecl->hasAttr<SDxKernelAttr>()) { 
    return ;
  }
  Expr *port = interface->getPort();
  auto lvalue = EmitLValue(port);
  auto port_var = lvalue.getPointer();

  StringRef storageTypeStr = interface->getStorageType();
  int storage_type_op = platform::PlatformBasic::OP_UNSUPPORTED;
  int storage_type_impl = platform::PlatformBasic::UNSUPPORTED;
  if (storageTypeStr != "") { 
    const platform::PlatformBasic *xilinxPlatform =
        platform::PlatformBasic::getInstance();
    std::pair<platform::PlatformBasic::OP_TYPE,
              platform::PlatformBasic::IMPL_TYPE>
        mem_impl_type;
    if (!xilinxPlatform->verifyInterfaceStorage(
            storageTypeStr.str(), &mem_impl_type)) {
      CGM.getDiags().Report(interface->getLocation(), diag::err_xlx_attribute_invalid_option)
          << storageTypeStr
          << "interface " +interface->getMode().str() + "'s option 'storage_type'";
      return;
    }
    storage_type_impl = (int)mem_impl_type.second;
    storage_type_op = (int)mem_impl_type.first;
  }

  int latency = EvaluateInteger(interface->getLatency(), getContext());
  StringRef signalName = interface->getSignalName();



  llvm::Value* Args[] = {
    port_var,
    Builder.getInt64(storage_type_op),
    Builder.getInt64(storage_type_impl), 
    Builder.getInt64(latency), 
    llvm::ConstantDataArray::getString(getLLVMContext(), signalName)
  };
  SmallVector<llvm::OperandBundleDef, 1> bundleDefs;
  if (interface->getMode() == "ap_memory") { 
    bundleDefs.emplace_back("xlx_ap_memory", Args);
  }
  else if (interface->getMode() == "bram"){ 
    bundleDefs.emplace_back("xlx_bram", Args);
  }

  auto *sideeffect = llvm::Intrinsic::getDeclaration(&CGM.getModule(), llvm::Intrinsic::sideeffect);
  auto *CI = Builder.CreateCall(sideeffect, None, bundleDefs);
  CI->setOnlyAccessesInaccessibleMemory();
  CI->setDoesNotThrow();
}


void CodeGenFunction::EmitAPFifoInterfaceIntrinsic( const APFifoInterfaceAttr *interface) 
{
  if (!CurFuncDecl->hasAttr<SDxKernelAttr>()) { 
    return ;
  }
  Expr *port = interface->getPort();
  auto lvalue = EmitLValue(port);
  auto port_var = lvalue.getPointer();

  bool isRegsiter = interface->getIsRegister();
  StringRef signalName = interface->getSignalName();
  int depth = EvaluateInteger(interface->getDepth(), getContext());
  llvm::Value *Args [] = { 
    port_var,
    Builder.getInt1(isRegsiter), 
    llvm::ConstantDataArray::getString(getLLVMContext(), signalName, false), 
    Builder.getInt64(depth)
  };
  SmallVector<llvm::OperandBundleDef, 1> bundleDefs;
  bundleDefs.emplace_back("xlx_ap_fifo", Args);

  auto *sideeffect = llvm::Intrinsic::getDeclaration(&CGM.getModule(), llvm::Intrinsic::sideeffect);
  auto *CI = Builder.CreateCall(sideeffect, None, bundleDefs);
  CI->setOnlyAccessesInaccessibleMemory();
  CI->setDoesNotThrow();
}

void CodeGenFunction::EmitAPScalarInterfaceIntrinsic(const APScalarInterfaceAttr *interface) 
{
  if (!CurFuncDecl->hasAttr<SDxKernelAttr>()) { 
    return ;
  }
  Expr *port = interface->getPort();
  auto lvalue = EmitLValue(port);
  auto port_var = lvalue.getPointer();

  bool isRegister = interface->getIsRegister();
  StringRef signalName = interface->getSignalName();

  llvm::Value* Args[] = { 
    port_var,
    Builder.getInt1(isRegister), 
    llvm::ConstantDataArray::getString(getLLVMContext(), signalName, false), 
  };
  SmallVector<llvm::OperandBundleDef, 1> bundleDefs;
  if (interface->getMode() == "ap_none") { 
    bundleDefs.emplace_back("xlx_ap_none", Args);
  }
  else if (interface->getMode() == "ap_ack") { 
    bundleDefs.emplace_back("xlx_ap_ack", Args);
  }
  else if (interface->getMode() == "ap_vld") { 
    bundleDefs.emplace_back("xlx_ap_vld", Args);
  }
  else if (interface->getMode() == "ap_ovld") { 
    bundleDefs.emplace_back("xlx_ap_ovld", Args);
  }
  else if (interface->getMode() == "ap_hs") { 
    bundleDefs.emplace_back("xlx_ap_hs", Args);
  }
  else if (interface->getMode() == "ap_stable") { 
    bundleDefs.emplace_back("xlx_ap_stable", Args);
  }
  else { 
    assert( false && "unexected");
  }


  auto *sideeffect = llvm::Intrinsic::getDeclaration(&CGM.getModule(), llvm::Intrinsic::sideeffect);
  auto *CI = Builder.CreateCall(sideeffect, None, bundleDefs);
  CI->setOnlyAccessesInaccessibleMemory();
  CI->setDoesNotThrow();
}



