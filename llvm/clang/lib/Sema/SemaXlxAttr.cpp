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
//  This file implements xilinx's stmt-related attribute processing.
//
//===----------------------------------------------------------------------===//
#include "TreeTransform.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Sema/DelayedDiagnostic.h"
#include "clang/Sema/LoopHint.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Basic/HLSDiagnostic.h"

#include "llvm/ADT/StringExtras.h"

#include "llvm/Support/XILINXFPGAPlatformBasic.h"

using namespace clang;
using namespace sema;

static std::string str(const Twine &Name) { return Name.str(); }

/// \brief Diagnose mutually exclusive attributes when present on a given
/// declaration. Returns true if diagnosed.
template <typename AttrTy>
static bool checkAttrMutualExclusion(Sema &S, Decl *D, SourceRange Range,
                                     IdentifierInfo *Ident,
                                     bool Warning = false) {
  if (AttrTy *A = D->getAttr<AttrTy>()) {
    S.Diag(Range.getBegin(), Warning ? diag::warn_attributes_are_not_compatible
                                     : diag::err_attributes_are_not_compatible)
        << Ident << A;
    S.Diag(A->getLocation(), diag::note_conflicting_attribute);
    return true;
  }
  return false;
}

static IntegerLiteral *
createIntegerLiteral(int64_t i, Sema &S,
                     SourceLocation Loc = SourceLocation()) {
  auto &Ctx = S.getASTContext();
  auto IntTy = Ctx.IntTy;
  auto Width = Ctx.getIntWidth(IntTy);
  auto Int = llvm::APInt(Width, i);
  return IntegerLiteral::Create(Ctx, Int, IntTy, Loc);
}

static bool isTemplateDependent(const Expr *E) {
  if (E->isTypeDependent() || E->isValueDependent())
    return true;
  return false;
}

/// \param E, The expression from the attribute argument.
/// \param LB, The lower bound for the integer value which is included.
/// \param UB, the upper bound for the integer value which is included.
/// returns the extracted integer or \param Default value.
static llvm::Optional<int64_t> EvaluateInteger(Sema &S, Expr *E, unsigned Idx,
                                               StringRef Name,
                                               SourceLocation Loc, int LB,
                                               int UB) {
  llvm::APSInt Int(32);
  assert(LB <= UB && "it is not expected that LB is smaller than UB ");

  auto ICE = S.HLSVerifyIntegerConstantExpression(E, &Int);
  if (ICE.isInvalid()) {
    return None;
  }

  int64_t Ret = Int.getSExtValue();
  if ((Ret < LB || Ret > UB)) {
    S.Diag(Loc, diag::err_attribute_argument_out_of_bounds)
        << str("'" + Name + "'") << Idx << LB << UB << E->getSourceRange();
    return None;
  }

  return Ret;
}

static llvm::Optional<int64_t> ExtractInteger(Sema &S,
                                              const AttributeList &Attr,
                                              unsigned Idx, int LB, int UB) {
  Expr *E = Attr.getArgAsExpr(Idx);

  if (E->isTypeDependent() || E->isValueDependent()) {
    S.Diag(Attr.getLoc(), diag::warn_attribute_argument_n_type)
        << Attr.getName() << Idx << AANT_ArgumentIntegerConstant
        << E->getSourceRange();
    return None;
  }

  llvm::APSInt Int(32);

  return EvaluateInteger(S, E, Idx, Attr.getName()->getName(), Attr.getLoc(),
                         LB, UB);
}

static void handleXCLArrayXForm(Sema &S, Decl *D, const AttributeList &Attr) {
  assert(Attr.getKind() == AttributeList::AT_XCLArrayXForm);

  if (!Attr.isArgIdent(0)) {
    S.Diag(Attr.getLoc(), diag::err_attribute_argument_n_type)
        << Attr.getName() << 1 << AANT_ArgumentIdentifier;
    return;
  }

  IdentifierInfo *II = Attr.getArgAsIdent(0)->Ident;

  XCLArrayXFormAttr::XCLArrayXFormType T;

  if (!XCLArrayXFormAttr::ConvertStrToXCLArrayXFormType(II->getName(), T)) {
    S.Diag(Attr.getLoc(), diag::err_xcl_array_unknown_xform_type)
        << Attr.getAttributeSpellingListIndex() << II->getName();
    return;
  }

  Expr *Dim = nullptr;
  Expr *Factor = nullptr;

  // The XlxArrayXFormAttr has a weird definition when the type of
  // partition/reshape is not "complete" e.g. xcl_array_partition(block, 2, 1),
  // the factor is the second one while dim becomes the third one.
  if (T == XCLArrayXFormAttr::Cyclic || T == XCLArrayXFormAttr::Block) {
    if (Attr.getNumArgs() != 3) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments) << 3;
      return;
    }
    Factor = Attr.getArgAsExpr(1);
    Dim = Attr.getArgAsExpr(2);
  } else {
    // When the partition type is complete, Factor is not necessary.
    if (Attr.getNumArgs() != 2) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments) << 2;
      return;
    }
    Dim = Attr.getArgAsExpr(1);
  }

  if (Factor == nullptr)
    Factor = createIntegerLiteral(/*Default*/ 0, S, Attr.getLoc());

  D->addAttr(::new (S.Context)
                 XCLArrayXFormAttr(Attr.getRange(), S.Context, T, Factor, Dim,
                                   Attr.getAttributeSpellingListIndex()));
}

static Attr *handleXlxArrayXForm(Sema &S, Stmt *stmt, const AttributeList &Attr,
                                 SourceRange Range) {
  assert(Attr.getKind() == AttributeList::AT_XlxArrayXForm);

  if (!Attr.isArgIdent(1)) {
    S.Diag(Attr.getLoc(), diag::err_attribute_argument_n_type)
        << Attr.getName() << 1 << AANT_ArgumentIdentifier;
    return nullptr;
  }
  Expr *Variable = Attr.getArgAsExpr(0);

  IdentifierInfo *II = Attr.getArgAsIdent(1)->Ident;

  XlxArrayXFormAttr::XlxArrayXFormType T;

  if (!XlxArrayXFormAttr::ConvertStrToXlxArrayXFormType(II->getName(), T)) {
    S.Diag(Attr.getLoc(), diag::err_xcl_array_unknown_xform_type)
        << Attr.getAttributeSpellingListIndex() << II->getName();
    return nullptr;
  }

  Expr *Dim = nullptr;
  Expr *Factor = nullptr;

  // The XlxArrayXFormAttr has a weird definition when the type of
  // partition/reshape is not "complete" e.g. xcl_array_partition(block, 2, 1),
  // the factor is the second one while dim becomes the third one.
  if (T == XlxArrayXFormAttr::Cyclic || T == XlxArrayXFormAttr::Block) {
    if (Attr.getNumArgs() != 4) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments) << 3;
      return nullptr;
    }
    Factor = Attr.getArgAsExpr(2);
  } else {
    // When the partition type is complete, Factor is not necessary.
    if (Attr.getNumArgs() != 4) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments) << 2;
      return nullptr;
    }
  }
  Dim = Attr.getArgAsExpr(3);

  if (Factor == nullptr)
    Factor = createIntegerLiteral(/*Default*/ 0, S, Attr.getLoc());

  return new (S.Context)
      XlxArrayXFormAttr(Attr.getRange(), S.Context, Variable, T, Factor, Dim,
                        Attr.getAttributeSpellingListIndex());
}

static bool checkHasEnumField(const RecordType *t) {
  for (FieldDecl *FD : t->getDecl()->fields()) {
    QualType FieldTy = FD->getType();
    FieldTy = FieldTy.getCanonicalType();
    bool hasEnum = false;

    // enum
    if (FieldTy->isEnumeralType()) {
      hasEnum = true;
    } else if (const RecordType *FieldRecTy = FieldTy->getAs<RecordType>()) {
      // struct
      hasEnum = checkHasEnumField(FieldRecTy);
    } else if (const PointerType *pointerTy = FieldTy->getAs<PointerType>()) {
      // pointer
      while (FieldTy->isPointerType()) {
        FieldTy = FieldTy->getAs<PointerType>()->getPointeeType();
      }

      if (FieldTy->isEnumeralType()) {
        hasEnum = true;
      } else if (FieldTy->isStructureType()) {
        hasEnum = checkHasEnumField(FieldTy->getAs<RecordType>());
      }
    }
    if (hasEnum)
      return true;
  }
  return false;
}

static void handleSDxKernel(Sema &S, Decl *D, const AttributeList &Attr) {
  if (Attr.getNumArgs() > 2) {
    S.Diag(Attr.getLoc(), diag::err_attribute_too_many_arguments)
        << Attr.getName() << 1;
    return;
  }

  StringRef RTLName;
  if (Attr.getNumArgs() >= 1) {
    if (Attr.isArgIdent(0))
      RTLName = Attr.getArgAsIdent(0)->Ident->getName();
    else {
      // come from user code attribute not pragma
      auto *E = dyn_cast<StringLiteral>(Attr.getArgAsExpr(0));
      RTLName = E ? E->getBytes() : "";
    }
  }
  bool GenericInterface = false;
  // if (Attr.getNumArgs() == 2)
  //  GenericInterface = true;

  // Check if function is class method
  if (cast<FunctionDecl>(D)->isCXXClassMember()) {
    S.Diag(Attr.getLoc(), diag::err_sdxkernel_in_wrong_scope)
        << cast<FunctionDecl>(D)->getQualifiedNameAsString();
    return;
  }
  if (S.getLangOpts().C99) {
    auto top_func = dyn_cast<FunctionDecl>(D);
    auto params = top_func->parameters();
    bool hasEnum = false;
    for (auto param : params) {
      auto type = param->getType();
      if (type->isEnumeralType()) {
        hasEnum = true;
      } else if (type->isStructureType()) {
        hasEnum = checkHasEnumField(type->getAs<RecordType>());
      } else if (type->isPointerType()) {
        while (type->isPointerType()) {
          type = type->getAs<PointerType>()->getPointeeType();
        }
        if (type->isStructureType()) {
          hasEnum = checkHasEnumField(type->getAs<RecordType>());
        } else if (type->isEnumeralType()) {
          hasEnum = true;
        }
      }

      if (hasEnum) {
        break;
      }
    }

    if (hasEnum) {
      S.Diag(Attr.getLoc(), diag::err_xlx_not_supported_by_scout_HLS)
          << "C top function parameter with the type is  'enum' or 'stuct "
             "containing enum' ";
    }
  }

  D->addAttr(::new (S.Context) SDxKernelAttr(
      Attr.getRange(), S.Context, RTLName, GenericInterface,
      Attr.getAttributeSpellingListIndex()));
}

template <typename T>
static bool CheckRedundantAdaptor(Decl *D, StringRef Name) {
  return llvm::any_of(D->specific_attrs<T>(), [Name](T *A) {
    if (Name != A->getName())
      return false;

    // TODO: Check them to have the same parameters
    return true;
  });
}

static void handleSAXIAdaptor(Sema &S, Decl *D, const AttributeList &Attr) {
  if (Attr.getNumArgs() != 2) {
    S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments) << 2;
    return;
  }

  auto Name = Attr.getArgAsIdent(0)->Ident->getName();
  if (Name.empty()) {
    S.Diag(Attr.getLoc(), diag::err_attribute_argument_n_type)
        << Attr.getName() << 0 << AANT_ArgumentIdentifier
        << Attr.getArgAsIdent(0)->Loc;
    return;
  }

  if (CheckRedundantAdaptor<SAXIAdaptorAttr>(D, Name))
    return;

  auto Clock = Attr.getArgAsIdent(1)->Ident->getName();

  D->addAttr(::new (S.Context)
                 SAXIAdaptorAttr(Attr.getRange(), S.Context, Name, Clock,
                                 Attr.getAttributeSpellingListIndex()));
}

static void handleXCLMaxWorkGroupSize(Sema &S, Decl *D,
                                      const AttributeList &Attr) {
  if (Attr.getNumArgs() > 3) {
    S.Diag(Attr.getLoc(), diag::err_attribute_too_many_arguments)
        << Attr.getName() << 3;
    return;
  }

  if (Attr.getNumArgs() < 1) {
    S.Diag(Attr.getLoc(), diag::err_attribute_too_few_arguments)
        << Attr.getName() << 1;
    return;
  }

  llvm::Optional<int64_t> X, Y, Z;

  X = ExtractInteger(S, Attr, 0, /*LB*/ 1, /*UB*/ INT32_MAX);

  if (Attr.getNumArgs() >= 2)
    Y = ExtractInteger(S, Attr, 1, /*LB*/ 1, /*UB*/ INT32_MAX);

  if (Attr.getNumArgs() >= 3)
    Z = ExtractInteger(S, Attr, 2, /*LB*/ 1, /*UB*/ INT32_MAX);

  if (!X)
    return;

  D->addAttr(::new (S.Context) XCLMaxWorkGroupSizeAttr(
      Attr.getRange(), S.Context, X.getValue(), Y.getValueOr(1),
      Z.getValueOr(1), Attr.getAttributeSpellingListIndex()));
}

static void handleNoCtor(Sema &S, Decl *D, const AttributeList &Attr) {
  D->addAttr(::new (S.Context) NoCtorAttr(
      Attr.getRange(), S.Context, Attr.getAttributeSpellingListIndex()));
}

static void handleXCLZeroGlobalWorkOffset(Sema &S, Decl *D,
                                          const AttributeList &Attr) {
  D->addAttr(::new (S.Context) XCLZeroGlobalWorkOffsetAttr(
      Attr.getRange(), S.Context, Attr.getAttributeSpellingListIndex()));
}

/*  variable=return will incure current bind_op attribute is bind with current
 * function it is not supported now
 */
/*
static void handleXlxBindOp( Sema &S, Decl *D, const AttributeList &A) {
  if (A.getNumArgs() != 4) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 4;
  }

  int latency = 0;
  IdentifierLoc *type_id = nullptr, *impl_id = nullptr;

  if (A.isArgIdent(1) && A.getArg(1)) {
    type_id = A.getArgAsIdent(1);
  }

  if (A.isArgIdent(2) && A.getArg(2)) {
    impl_id = A.getArgAsIdent(2);
  }

  if (A.isArgExpr(3) && A.getArg(3)) {
    auto latency_ice = A.getArgAsExpr(3);
    auto ret = EvaluateInteger(S, latency_ice, 3, "latency", A.getLoc(), 0, -1);
    if (ret.hasValue()) {
      latency = ret.getValue();
    }
  }


  D->addAttr(::new(S.Context) XlxBindOpAttr( A.getRange(), S.Context, nullptr,
type_id->Ident, impl_id->Ident, latency, A.getAttributeSpellingListIndex()));

}
*/

static void handleXlxResourceIPCore(Sema &S, Decl *D, const AttributeList &A) {
  if (A.getNumArgs() != 2) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 2;
  }

  IdentifierLoc *ip_ii = nullptr;
  ip_ii = A.getArgAsIdent(0);

  IdentifierLoc *core_ii = nullptr;
  core_ii = A.getArgAsIdent(1);

  D->addAttr(::new (S.Context) XlxResourceIPCoreAttr(
      A.getRange(), S.Context, ip_ii->Ident, core_ii->Ident,
      A.getAttributeSpellingListIndex()));
}

static Attr *handleXCLSingleWorkitemAttr(Sema &S, Stmt *St,
                                         const AttributeList &A,
                                         SourceRange Range) {
  return ::new (S.Context) XCLSingleWorkitemAttr(
      A.getRange(), S.Context, A.getAttributeSpellingListIndex());
}

static Attr *handleXlxPipelineAttr(Sema &S, Stmt *St, const AttributeList &A,
                                   SourceRange Range) {
#if 0
  if (!isa<ForStmt>(St) && !isa<WhileStmt>(St) && !isa<DoStmt>(St)) {
    S.Diag(A.getRange().getBegin(), diag::warn_xlx_attr_wrong_stmt_target)
        << "xcl_pipeline_loop" << "for/while/do" 
        << FixItHint::CreateRemoval(A.getRange());
    return nullptr;
  }
#endif

  Expr *II = nullptr;
  II = A.getArgAsExpr(0);

  int Style = ExtractInteger(S, A, 1, /*LB*/ -1, /*UB*/ 2).getValue();
  bool Rewind = ExtractInteger(S, A, 2, /*LB*/ 0, /*UB*/ 1).getValue();

  return ::new (S.Context)
      XlxPipelineAttr(A.getRange(), S.Context, II, Style, Rewind,
                      A.getAttributeSpellingListIndex());
}

static Attr *handleXCLPipelineLoopAttr(Sema &S, Stmt *St,
                                       const AttributeList &A,
                                       SourceRange Range) {
  if (!isa<ForStmt>(St) && !isa<WhileStmt>(St) && !isa<DoStmt>(St)) {
    S.Diag(A.getRange().getBegin(), diag::warn_xlx_attr_wrong_stmt_target)
        << "xcl_pipeline_loop"
        << "for/while/do" << FixItHint::CreateRemoval(A.getRange());
    return nullptr;
  }

  if (A.getNumArgs() > 1) {
    S.Diag(A.getLoc(), diag::err_attribute_too_many_arguments) << 1;
    return nullptr;
  }

  Expr *E = nullptr;
  if (A.getNumArgs() == 1) {
    E = A.getArgAsExpr(0);
  }

  if (!E)
    E = createIntegerLiteral(-1, S, A.getLoc());

  return ::new (S.Context) XCLPipelineLoopAttr(
      A.getRange(), S.Context, E, A.getAttributeSpellingListIndex());
}

static Attr *handleXCLPipelineWorkitemsAttr(Sema &S, Stmt *St,
                                            const AttributeList &A,
                                            SourceRange Range) {

  if (!isa<CompoundStmt>(St)) {
    S.Diag(A.getRange().getBegin(), diag::err_attr_in_wrong_scope)
        << "xcl_pipeline_workitems";
    return nullptr;
  }

  // XCL attribute document is very confused, xcl spec define that no
  // option/argument for XCLPipelineWorkItems, while there some case in precommit
  // use one "II" option
  if (A.getNumArgs() > 1) {
    S.Diag(A.getLoc(), diag::err_attribute_too_many_arguments) << 1;
    return nullptr;
  }

  Expr *II = nullptr;
  if (A.getNumArgs() > 0) {
    II = A.getArgAsExpr(0);
  }
  if (!II)
    II = createIntegerLiteral(-1, S, A.getLoc());

  return ::new (S.Context) XCLPipelineWorkitemsAttr(
      A.getRange(), S.Context, II, A.getAttributeSpellingListIndex());
}

static Attr *handleXCLUnrollWorkitemsAttr(Sema &S, Stmt *St,
                                          const AttributeList &A,
                                          SourceRange Range) {
  unsigned NumArgs = A.getNumArgs();

  if (NumArgs > 1) {
    S.Diag(A.getLoc(), diag::err_attribute_too_many_arguments)
        << A.getName() << 1;
    return nullptr;
  }

  llvm::Optional<int64_t> UnrollFactor;

  if (NumArgs == 1)
    UnrollFactor = ExtractInteger(S, A, 0, /*LB*/ 0, /*UB*/ -1);
  if (!UnrollFactor.hasValue())
    return nullptr;

  return ::new (S.Context)
      XCLUnrollWorkitemsAttr(A.getRange(), S.Context, UnrollFactor.getValue(),
                             A.getAttributeSpellingListIndex());
}

bool Sema::CheckSPMDDataflow(Decl *D, SourceLocation L) {
  assert(D->hasAttr<OpenCLKernelAttr>() && "Expect OpenCL kernel!");
  // xcl_dataflow can only apply to (1,1,1) kernel
  auto *RWGS = D->getAttr<ReqdWorkGroupSizeAttr>();
  if (RWGS && RWGS->getXDim() == 1 && RWGS->getYDim() == 1 &&
      RWGS->getZDim() == 1)
    return false;

  if (RWGS)
    L = RWGS->getLocation();

  auto B = Diag(L, diag::err_xcl_dataflow_attr_spmd_kernel)
           << (RWGS == nullptr ? 0 : 1);
  if (RWGS)
    B << FixItHint::CreateReplacement(RWGS->getRange(),
                                      "reqd_work_group_size(1, 1, 1)");

  return true;
}

static Attr *handleXCLDataFlowAttr(Sema &S, Stmt *St, const AttributeList &A,
                                   SourceRange Range) {

  if (auto *CurFD = S.getCurFunctionDecl()) {
    if (CurFD->hasAttr<OpenCLKernelAttr>() &&
        S.CheckSPMDDataflow(CurFD, A.getRange().getBegin()))
      return nullptr;
  }

  if (A.getNumArgs() > 1) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 1;
    return nullptr;
  }

  XCLDataFlowAttr::PropagationType Type = XCLDataFlowAttr::StartPropagation;
  if (A.getNumArgs() == 1) {
    if (!A.isArgIdent(0))
      return nullptr;
    auto TypeStr = A.getArgAsIdent(0)->Ident->getName();
    if (!XCLDataFlowAttr::ConvertStrToPropagationType(TypeStr, Type))
      return nullptr;
  }

  return ::new (S.Context) XCLDataFlowAttr(A.getRange(), S.Context, Type,
                                           A.getAttributeSpellingListIndex());
}

static Attr *handleXCLFlattenLoopAttr(Sema &S, Stmt *St, const AttributeList &A,
                                      SourceRange Range) {
  if (!isa<ForStmt>(St) && !isa<WhileStmt>(St)) {
    S.Diag(A.getRange().getBegin(), diag::warn_xcl_loop_attr_wrong_target)
        << "loop_flatten" << FixItHint::CreateRemoval(A.getRange());
    return nullptr;
  }

  if (A.getNumArgs() > 1) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 1;
    return nullptr;
  }

  llvm::Optional<int64_t> Off;
  if (A.getNumArgs() == 1)
    Off = ExtractInteger(S, A, 0, /*LB*/ 0, /*UB*/ 1);
  // reverse Off for Enabled
  uint32_t Enabled = Off.getValueOr(0) ? 0 : 1;

  return ::new (S.Context) XCLFlattenLoopAttr(
      A.getRange(), S.Context, Enabled, A.getAttributeSpellingListIndex());
}

static Attr *handleXCLLoopTripCountAttr(Sema &S, Stmt *St,
                                        const AttributeList &A,
                                        SourceRange Range) {
  // TODO: what about do-while loop?
  if (!isa<ForStmt>(St) && !isa<WhileStmt>(St)) {
    S.Diag(A.getRange().getBegin(), diag::warn_xcl_loop_attr_wrong_target)
        << "loop_tripcount" << FixItHint::CreateRemoval(A.getRange());
    return nullptr;
  }

  if (A.getNumArgs() > 3) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 3;
    return nullptr;
  }
  Expr *MinExpr = A.getArgAsExpr(0);
  Expr *MaxExpr = A.getArgAsExpr(1);
  Expr *AvgExpr = nullptr;

  if (A.getNumArgs() == 3)
    AvgExpr = A.getArgAsExpr(2);
  else {
    Expr *Two = createIntegerLiteral(2, S, A.getLoc());
    SourceLocation DefaultLoc;
    Expr *Add =
        S.BuildBinOp(S.getCurScope(), A.getLoc(), BO_Add, MinExpr, MaxExpr)
            .get();
    Expr *Paren = S.ActOnParenExpr(DefaultLoc, DefaultLoc, Add).get();
    AvgExpr =
        S.BuildBinOp(S.getCurScope(), A.getLoc(), BO_Div, Paren, Two).get();
  }

  // Check the semantic of the tripcount attribute.
  // If AvgExpr is nullptr, we will create the AvgExpr during the check.
  if (!S.CheckXCLLoopTripCountExprs(MinExpr, MaxExpr, AvgExpr, A.getLoc(),
                                    A.getName()->getName()))
    return nullptr;

  return (::new (S.Context)
              XCLLoopTripCountAttr(A.getRange(), S.Context, MinExpr, MaxExpr,
                                   AvgExpr, A.getAttributeSpellingListIndex()));
}

static Attr *handleXlxLoopTripCountAttr(Sema &S, Stmt *St,
                                        const AttributeList &A,
                                        SourceRange Range) {

  if (A.getNumArgs() > 3) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 3;
    return nullptr;
  }
  Expr *MinExpr = A.getArgAsExpr(0);
  Expr *MaxExpr = A.getArgAsExpr(1);
  Expr *AvgExpr = nullptr;

  if (A.getNumArgs() == 3)
    AvgExpr = A.getArgAsExpr(2);
  else {
    Expr *Two = createIntegerLiteral(2, S, A.getLoc());
    SourceLocation DefaultLoc;
    Expr *Add =
        S.BuildBinOp(S.getCurScope(), A.getLoc(), BO_Add, MinExpr, MaxExpr)
            .get();
    Expr *Paren = S.ActOnParenExpr(DefaultLoc, DefaultLoc, Add).get();
    AvgExpr =
        S.BuildBinOp(S.getCurScope(), A.getLoc(), BO_Div, Paren, Two).get();
  }

  return (::new (S.Context)
              XlxLoopTripCountAttr(A.getRange(), S.Context, MinExpr, MaxExpr,
                                   AvgExpr, A.getAttributeSpellingListIndex()));
}

static void handleXCLVisibility(Sema &S, Decl *D, const AttributeList &Attr) {
  if (Attr.getNumArgs() != 1) {
    S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments) << 1;
    return;
  }

  if (!Attr.isArgExpr(0))
    return;

  auto *TStr = dyn_cast<StringLiteral>(Attr.getArgAsExpr(0));
  if (TStr == nullptr)
    return;

  XCLVisibilityAttr::XCLVisibilityType T;

  if (!XCLVisibilityAttr::ConvertStrToXCLVisibilityType(TStr->getBytes(), T))
    return;

  D->addAttr(::new (S.Context) XCLVisibilityAttr(
      Attr.getRange(), S.Context, T, Attr.getAttributeSpellingListIndex()));
}

static void handleXCLArrayGeometry(Sema &S, Decl *D,
                                   const AttributeList &Attr) {
  SmallVector<Expr *, 4> Args;
  for (unsigned Idx = 0; Idx < Attr.getNumArgs(); ++Idx) {
    auto *ArgExp = Attr.getArgAsExpr(Idx);
    // TODO: Check the type of ArgExpr
    Args.push_back(ArgExp);
  }

  D->addAttr(::new (S.Context) XCLArrayGeometryAttr(
      Attr.getRange(), S.Context, Args.data(), Args.size(),
      Attr.getAttributeSpellingListIndex()));
}

static Attr *handleXlxArrayGeometry(Sema &S, Stmt *st,
                                    const AttributeList &Attr,
                                    SourceRange Range) {
  SmallVector<Expr *, 4> Args;
  for (unsigned Idx = 0; Idx < Attr.getNumArgs(); ++Idx) {
    auto *ArgExp = Attr.getArgAsExpr(Idx);
    // TODO: Check the type of ArgExpr
    Args.push_back(ArgExp);
  }

  return (::new (S.Context) XlxArrayGeometryAttr(
      Attr.getRange(), S.Context, Args.data(), Args.size(),
      Attr.getAttributeSpellingListIndex()));
}

static Attr *handleXlxFunctionAllocationAttr(Sema &S, Stmt *St,
                                             const AttributeList &A,
                                             SourceRange Range) {
  if (A.getNumArgs() != 2) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments) << 2;
    return nullptr;
  }

  Expr *func_pointer = A.getArgAsExpr(0);
  Expr *instance_limit = A.getArgAsExpr(1);
  {
#if 0
    llvm::dbgs() << "Sema: functionAllocation, before: \n";
    func_pointer->dump();
    llvm::dbgs() << "is typeDependent : " << func_pointer->isTypeDependent() << "\n";
#endif
#if 0
    if (isa<UnaryOperator>(func_pointer)) {
      if (cast<UnaryOperator>(func_pointer)->getOpcode() == UO_AddrOf) {
        func_pointer = cast<UnaryOperator>(func_pointer)->getSubExpr();
      } else {
        S.Diag(func_pointer->getLocStart(),
               diag::warn_xlx_attribute_ignore_because_invalid_option)
            << "ALLOCATION"
            << "Instances value is not valid function pointer expression";
        return nullptr;
      }
    }
#endif 

    if (!func_pointer->isTypeDependent()){
      if (isa<UnaryOperator>(func_pointer) && dyn_cast<UnaryOperator>(func_pointer)->getOpcode() == UO_AddrOf) { 
        auto sub_expr = dyn_cast<UnaryOperator>(func_pointer)->getSubExpr();
        func_pointer = sub_expr;
      }

      if (isa<OverloadExpr>(func_pointer)) { 
        FunctionDecl *decl = nullptr;
        OverloadExpr *ovl_expr = dyn_cast<OverloadExpr>(func_pointer);
        decl =
          S.ResolveSingleFunctionTemplateSpecialization(ovl_expr, true);
  
        if (!decl) {
          S.Diag(func_pointer->getLocStart(),
                 diag::warn_xlx_attribute_ignore_because_invalid_option)
              << "Allocation"
              << "Instances value is not valid function pointer expression";
          return nullptr;
        }
        ExprResult ret = S.BuildDeclRefExpr(decl, decl->getType(), VK_LValue,
                                            func_pointer->getLocStart());
        func_pointer = ret.get();
      }
    }
#if 0
    llvm::dbgs() << "Sema FunctionAllocation, After: \n";
    func_pointer->dump();
#endif
  }

  return (::new (S.Context) XlxFunctionAllocationAttr(
      A.getRange(), S.Context, func_pointer, instance_limit,
      A.getAttributeSpellingListIndex()));
}

static Attr *handleFPGAResourceLimitHintAttr(Sema &S, Stmt *St,
                                             const AttributeList &A,
                                             SourceRange Range) {
  if (A.getNumArgs() != 3) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments) << 2;
    return nullptr;
  }

  IdentifierInfo *Type = A.getArgAsIdent(0)->Ident;
  IdentifierInfo *Instance = A.getArgAsIdent(1)->Ident;

  if (!Type || !Instance) {
    return nullptr;
  }

  Expr *Limit = A.getArgAsExpr(2);

  return (::new (S.Context) FPGAResourceLimitHintAttr(
      A.getRange(), S.Context, Type, Instance, Limit,
      A.getAttributeSpellingListIndex()));
}

static Attr *handleXCLRegionNameAttr(Sema &S, Stmt *St, const AttributeList &A,
                                     SourceRange Range) {

  if (A.getNumArgs() != 1) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 1;
    return nullptr;
  }

  if (!A.isArgIdent(0)) {
    S.Diag(A.getLoc(), diag::err_attribute_argument_type)
        << A.getName() << AANT_ArgumentIdentifier
        << A.getArgAsExpr(0)->getSourceRange();

    return nullptr;
  }

  auto Name = A.getArgAsIdent(0)->Ident->getName();
  return XCLRegionNameAttr::CreateImplicit(S.Context, Name);
}

static Attr *handleXCLArrayViewAttr(Sema &S, Stmt *St, const AttributeList &A,
                                    SourceRange Range) {
  XCLArrayViewAttr::AccessModeType Mode;
  auto ModeStr = A.getArgAsIdent(0)->Ident->getName();
  if (!XCLArrayViewAttr::ConvertStrToAccessModeType(ModeStr, Mode))
    return nullptr;

  auto *ArrayDecl = A.getArgAsExpr(1);
  // TODO: Check if it has a decl type

  SmallVector<Expr *, 8> Shape;
  for (unsigned i = 2, e = A.getNumArgs(); i < e; ++i)
    Shape.push_back(A.getArgAsExpr(i));

  return ::new (S.Context)
      XCLArrayViewAttr(A.getRange(), S.Context, Mode, ArrayDecl, Shape.data(),
                       Shape.size(), A.getAttributeSpellingListIndex());
}

static Attr *handleXlxExprBalanceAttr(Sema &S, Stmt *St, const AttributeList &A,
                                      SourceRange Range) {
  if (!isa<CompoundStmt>(St)) {
    S.Diag(A.getRange().getBegin(), diag::err_attr_in_wrong_scope)
        << "xlx_expr_balance";
    return nullptr;
  }
  if (A.getNumArgs() > 1) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 1;
    return nullptr;
  }
  llvm::Optional<int64_t> Off;
  if (A.getNumArgs() == 1)
    Off = ExtractInteger(S, A, 0, /*LB*/ 0, /*UB*/ 1);

  // reverse Off for Enabled
  uint32_t Enabled = Off.getValueOr(0) ? 0 : 1;
  return ::new (S.Context) XlxExprBalanceAttr(
      A.getRange(), S.Context, Enabled, A.getAttributeSpellingListIndex());
}

static Attr *handleXlxOccurrenceAttr(Sema &S, Stmt *St, const AttributeList &A,
                                     SourceRange Range) {
  if (!isa<CompoundStmt>(St))
    return nullptr;
  if (A.getNumArgs() != 1) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 1;
    return nullptr;
  }

  Expr *Cycle = A.getArgAsExpr(0);

  if (S.CheckXlxOccurrenceExprs(Cycle, A.getLoc(), A.getName()->getName()))
    return nullptr;

  return ::new (S.Context) XlxOccurrenceAttr(A.getRange(), S.Context, Cycle,
                                             A.getAttributeSpellingListIndex());
}

static Attr *handleXlxProtocolAttr(Sema &S, Stmt *St, const AttributeList &A,
                                   SourceRange Range) {
  if (!isa<CompoundStmt>(St)) {
    return nullptr;
  }

  if (A.getNumArgs() != 1) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments) << 2;
    return nullptr;
  }

  IdentifierInfo *Type = A.getArgAsIdent(0)->Ident;
  if (!Type)
    return nullptr;
  XlxProtocolAttr::ProtocolModeType PType;
  if (!XlxProtocolAttr::ConvertStrToProtocolModeType(Type->getName(), PType))
    return nullptr;
  return (::new (S.Context) XlxProtocolAttr(A.getRange(), S.Context, PType,
                                            A.getAttributeSpellingListIndex()));
}

static Attr *handleXCLLatencyAttr(Sema &S, Stmt *St, const AttributeList &A,
                                  SourceRange Range) {
  if (A.getNumArgs() != 2) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 2;
    return nullptr;
  }

  Expr *MinExpr = A.getArgAsExpr(0);
  Expr *MaxExpr = A.getArgAsExpr(1);

  if (!isTemplateDependent(MinExpr) && !isTemplateDependent(MaxExpr)) {
    if (S.CheckXCLLatencyExprs(MinExpr, MaxExpr, A.getLoc(),
                               A.getName()->getName())) {
      return nullptr;
    }
  }

  return (::new (S.Context) XCLLatencyAttr(A.getRange(), S.Context,
                                           /*Min*/ MinExpr, /*Max*/ MaxExpr,
                                           A.getAttributeSpellingListIndex()));
}

static void handleDeclXCLLatencyAttr(Sema &S, Decl *D, const AttributeList &A) {
  if (A.getNumArgs() != 2) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 2;
    return;
  }

  Expr *MinExpr = A.getArgAsExpr(0);
  Expr *MaxExpr = A.getArgAsExpr(1);

  if (!isTemplateDependent(MinExpr) && !isTemplateDependent(MaxExpr)) {
    if (S.CheckXCLLatencyExprs(MinExpr, MaxExpr, A.getLoc(),
                               A.getName()->getName())) {
      return;
    }
  }

  D->addAttr(::new (S.Context) XCLLatencyAttr(
      A.getRange(), S.Context,
      /*Min*/ MinExpr, /*Max*/ MaxExpr, A.getAttributeSpellingListIndex()));
}

static Attr *handleXlxStableContent(Sema &S, Stmt *St, const AttributeList &A,
                                    SourceRange Range) {
  if (A.getNumArgs() != 1) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 1;
  }

  Expr *variable = nullptr;
  if (A.isArgExpr(0) && A.getArg(0)) {
    variable = A.getArgAsExpr(0);
  }
  return ::new (S.Context) XlxStableContentAttr(
      A.getRange(), S.Context, variable, A.getAttributeSpellingListIndex());
}

static Attr *handleXlxStable(Sema &S, Stmt *St, const AttributeList &A,
                             SourceRange Range) {
  if (A.getNumArgs() != 1) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 1;
  }

  Expr *variable = nullptr;
  if (A.isArgExpr(0) && A.getArg(0)) {
    variable = A.getArgAsExpr(0);
  }
  return ::new (S.Context) XlxStableAttr(A.getRange(), S.Context, variable,
                                         A.getAttributeSpellingListIndex());
}

static Attr *handleXlxShared(Sema &S, Stmt *St, const AttributeList &A,
                             SourceRange Range) {
  if (A.getNumArgs() != 1) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 1;
  }

  Expr *variable = nullptr;
  if (A.isArgExpr(0) && A.getArg(0)) {
    variable = A.getArgAsExpr(0);
  }
  return ::new (S.Context) XlxSharedAttr(A.getRange(), S.Context, variable,
                                         A.getAttributeSpellingListIndex());
}

static Attr *handleXlxDisaggr(Sema &S, Stmt *St, const AttributeList &A,
                              SourceRange Range) {
  if (A.getNumArgs() != 1) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 1;
  }

  Expr *variable = nullptr;
  if (A.isArgExpr(0) && A.getArg(0)) {
    variable = A.getArgAsExpr(0);
  }

  return ::new (S.Context) XlxDisaggrAttr(A.getRange(), S.Context, variable,
                                          A.getAttributeSpellingListIndex());
}

static Attr *handleXlxReqdPipeDepth(Sema &S, Stmt *St, const AttributeList &A,
                                    SourceRange Range) {

  auto variable = A.getArgAsExpr(0);
  auto DepthExpr = A.getArgAsExpr(1);

  int64_t offIntVal = 0;

  // for Opencl Attribute, user may only specify only one depth argument , and
  // off argument is missed we need support it
  if (A.getNumArgs() == 3) {
    auto OffExpr = A.getArgAsExpr(2);

    llvm::APSInt Int(32);

    S.VerifyIntegerConstantExpression(OffExpr, &Int);
    offIntVal = Int.getSExtValue();
  }

  return (::new (S.Context) XlxReqdPipeDepthAttr(
      A.getRange(), S.Context, variable, DepthExpr, offIntVal,
      A.getAttributeSpellingListIndex()));
}
static Attr *handleXlxDataPack(Sema &S, Stmt *st, const AttributeList &A, SourceRange range) 
{
  if (A.getNumArgs() != 2) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 1;
  }

  Expr *variable = nullptr;
  if (A.isArgExpr(0) && A.getArg(0)) {
    variable = A.getArgAsExpr(0);
  }

  XlxDataPackAttr::BytePadLevel level = XlxDataPackAttr::none;
  if (A.getArg(1)) { 
    XlxDataPackAttr::ConvertStrToBytePadLevel(A.getArgAsIdent(1)->Ident->getName(), level);
  }
  return ::new (S.Context) XlxDataPackAttr(A.getRange(), S.Context, variable, level,
                                            A.getAttributeSpellingListIndex());
}

        

static Attr *handleXlxAggregate(Sema &S, Stmt *St, const AttributeList &A,
                                SourceRange Range) {
  if (A.getNumArgs() != 2) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 1;
  }

  Expr *variable = nullptr;
  if (A.isArgExpr(0) && A.getArg(0)) {
    variable = A.getArgAsExpr(0);
  }

  XlxAggregateAttr::Compact compact = XlxAggregateAttr::empty;
  if (A.getArg(1)) { 
    if (!XlxAggregateAttr::ConvertStrToCompact(A.getArgAsIdent(1)->Ident->getName(), compact)) {
      S.Diag(A.getArgAsIdent(1)->Loc, diag::err_xlx_attribute_invalid_option) 
        << "compact" << A.getArgAsIdent(1)->Ident->getName();
    }
  }
  return ::new (S.Context) XlxAggregateAttr(A.getRange(), S.Context, variable, compact,
                                            A.getAttributeSpellingListIndex());
}

template <class DependenceAttr>
static Attr *handleDependence(Sema &S, Stmt *St, const AttributeList &A,
                              SourceRange Range) {

  /*
  if (!isa<ForStmt>(St) && !isa<WhileStmt>(St)) {
    S.Diag(A.getRange().getBegin(), diag::warn_xlx_attr_wrong_stmt_target)
      <<"xlx_dependence" << "for/while"
        << FixItHint::CreateRemoval(A.getRange());
    return nullptr;
  }
  */

  if (A.getNumArgs() != 6) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 6;
    return nullptr;
  }

  Expr *dep_variable = NULL;

  /*
    _ssdm_SpecDepenence(var, type, carry, direction, distance, Looplndep)

      Var: Value attached on, default is constant 0
      Type: 1(array), 2 (pointer) default is 0
      Carry: 0(false), 1(true), default is 0
      Direction: 0(raw), 1(war), 2(waw), default is -1
      Distance: default is 0
      LoopIndep: 0(intra), 1(inter), default is 1

      Yi Gao
  */

  typename DependenceAttr::XlxDepClass dep_class = DependenceAttr::NO_CLASS;
  typename DependenceAttr::XlxDepType dep_type = DependenceAttr::inter;
  typename DependenceAttr::XlxDepDirection dep_direction =
      DependenceAttr::NO_DIRECTION;

  int64_t dep_distance = -1;
  int dep_compel = 0;

  /* variable,  poiner/array, inter/intra, raw/war/waw, INTCONST, bool
   * if "variable" is empty, return Intconst( 0 )
   */
  if (A.isArgExpr(0) && A.getArg(0)) {
    dep_variable = A.getArgAsExpr(0);
  } else {
    dep_variable = createIntegerLiteral(0, S, A.getLoc());
  }

  if (A.isArgIdent(1)) {
    auto ident = A.getArgAsIdent(1);
    if (ident) {
      DependenceAttr::ConvertStrToXlxDepClass(
          A.getArgAsIdent(1)->Ident->getName(), dep_class);
    }
  } else if (A.isArgExpr(1)) {
    auto expr = A.getArgAsExpr(1);
    if (expr) {
      auto str = dyn_cast<StringLiteral>(expr);
      auto dep_class_str = str->getBytes();
      if (dep_class_str.equals_lower("pointer")) {
        dep_class = DependenceAttr::pointer;
      } else if (dep_class_str.equals_lower("array")) {
        dep_class = DependenceAttr::array;
      } else {
        S.Diag(str->getLocStart(), diag::err_attribute_unexpected_value)
            << "class " << str->getBytes() << "pointer/array";
      }
    }
  }

  if (A.isArgIdent(2)) {
    auto ident = A.getArgAsIdent(2);
    if (ident) {
      DependenceAttr::ConvertStrToXlxDepType(ident->Ident->getName(), dep_type);
    }
  } else if (A.isArgExpr(2)) {
    auto expr = A.getArgAsExpr(2);
    if (expr) {
      auto str = dyn_cast<StringLiteral>(expr);
      auto dep_type_str = str->getBytes();
      if (dep_type_str.equals_lower("inter")) {
        dep_type = DependenceAttr::inter;
      } else if (dep_type_str.equals_lower("intra")) {
        dep_type = DependenceAttr::intra;
      } else {
        S.Diag(str->getLocStart(), diag::err_attribute_unexpected_value)
            << "type " << str->getBytes() << "inter/intra";
      }
    }
  }

  if (A.isArgIdent(3)) {
    auto ident = A.getArgAsIdent(3);
    if (ident) {
      DependenceAttr::ConvertStrToXlxDepDirection(ident->Ident->getName(),
                                                  dep_direction);
    }
  } else if (A.isArgExpr(3)) {
    auto expr = A.getArgAsExpr(3);
    if (expr) {
      auto str = dyn_cast<StringLiteral>(expr);
      if (str->getBytes().equals_lower("RAW")) {
        dep_direction = DependenceAttr::RAW;
      } else if (str->getBytes().equals_lower("WAW")) {
        dep_direction = DependenceAttr::WAW;
      } else if (str->getBytes().equals_lower("WAR")) {
        dep_direction = DependenceAttr::WAR;
      } else {
        S.Diag(str->getLocStart(), diag::err_attribute_unexpected_value)
            << "direction " << str->getBytes() << " RAW/WAR/WAW";
      }
    }
  }

  if (A.getArgAsExpr(4)) {
    llvm::Optional<int64_t> distance_op = ExtractInteger(S, A, 4, 0, INT32_MAX);
    if (distance_op.hasValue()) {
      dep_distance = distance_op.getValue();
    }
  }

  Expr *isDep_expr = A.getArgAsExpr(5);
  if (isDep_expr) {
    llvm::Optional<int64_t> compel_op = ExtractInteger(S, A, 5, 0, 1);
    if (compel_op.hasValue()) {
      dep_compel = compel_op.getValue();
    }
  }
  else {
    // if "dependence true/false" option is not presented,
    // set dependence to false dep, and warning
    dep_compel = 0;
    S.Diag(A.getLoc(), diag::warn_xlx_dependence_missing_dependent_option);
  }

  return ::new (S.Context) DependenceAttr(
      A.getRange(), S.Context, dep_variable, dep_class, dep_type, dep_direction,
      dep_distance, (bool)dep_compel, A.getAttributeSpellingListIndex());
}

static Attr *handleXlxCrossDependence(Sema &S, Stmt *St, const AttributeList &A, 
                                 SourceRange range) { 

  if (A.getNumArgs() != 7) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 7;
    return nullptr;
  }

  Expr *dep_variable = NULL;

  /*
    _ssdm_SpecCrossDepenence(var, type, carry, direction, distance, Looplndep)

      Var0: Value attached on
      Var1: Value attached on,
      Type: 1(array), 2 (pointer) default is 0
      Carry: 0(false), 1(true), default is 0
      Direction: 0(raw), 1(war), 2(waw), default is -1
      Distance: default is 0
      LoopIndep: 0(intra), 1(inter), default is 1

      Yi Gao
  */

  typename XlxCrossDependenceAttr::XlxDepClass dep_class = XlxCrossDependenceAttr::NO_CLASS;
  typename XlxCrossDependenceAttr::XlxDepType dep_type = XlxCrossDependenceAttr::inter;
  typename XlxCrossDependenceAttr::XlxDepDirection dep_direction =
      XlxCrossDependenceAttr::NO_DIRECTION;

  int64_t dep_distance = -1;
  int dep_compel = 0;

  /* variable,  poiner/array, inter/intra, raw/war/waw, INTCONST, bool
   * if "variable" is empty, return Intconst( 0 )
   */

  assert( A.isArgExpr(0) && A.isArgExpr(1) && "unexpected, XlxPragma Parser should return two variable");
  Expr *cross_variable_0 = A.getArgAsExpr(0);
  Expr *cross_variable_1 = A.getArgAsExpr(1);

  if (A.isArgIdent(2)) {
    auto ident = A.getArgAsIdent(2);
    if (ident) {
      XlxCrossDependenceAttr::ConvertStrToXlxDepClass(
          A.getArgAsIdent(2)->Ident->getName(), dep_class);
    }
  }

  if (A.isArgIdent(3)) {
    auto ident = A.getArgAsIdent(3);
    if (ident) {
      XlxCrossDependenceAttr::ConvertStrToXlxDepType(ident->Ident->getName(), dep_type);
    }
  } 

  if (A.isArgIdent(4)) {
    auto ident = A.getArgAsIdent(3);
    if (ident) {
      XlxCrossDependenceAttr::ConvertStrToXlxDepDirection(ident->Ident->getName(),
                                                  dep_direction);
    }
  }

  if (A.getArgAsExpr(5)) {
    llvm::Optional<int64_t> distance_op = ExtractInteger(S, A, 5, 0, INT32_MAX);
    if (distance_op.hasValue()) {
      dep_distance = distance_op.getValue();
    }
  }

  Expr *isDep_expr = A.getArgAsExpr(6);
  if (isDep_expr) {
    llvm::Optional<int64_t> compel_op = ExtractInteger(S, A, 6, 0, 1);
    if (compel_op.hasValue()) {
      dep_compel = compel_op.getValue();
    }
  }

  // if dep_deistance != 0 && "dependence true/false" option is not presented,
  // set dependence to true dep
  if (dep_distance != 0 && !isDep_expr) {
    dep_compel = 1;
  }
  return ::new (S.Context) XlxCrossDependenceAttr(
      A.getRange(), S.Context, cross_variable_0, cross_variable_1, dep_class, dep_type, dep_direction,
      dep_distance, (bool)dep_compel, A.getAttributeSpellingListIndex());

}

static Attr *handleXCLInlineAttr(Sema &S, Stmt *St, const AttributeList &A,
                                 SourceRange Range) {
  if (!isa<CompoundStmt>(St)) {
    S.Diag(A.getRange().getBegin(), diag::err_attr_in_wrong_scope)
        << "xcl_inline";
    return nullptr;
  }
  if (A.getNumArgs() > 1) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 1;
    return nullptr;
  }
  llvm::Optional<int64_t> Recursive;
  if (A.getNumArgs() == 1)
    Recursive = ExtractInteger(S, A, 0, /*LB*/ 0, /*UB*/ 1);

  return ::new (S.Context)
      XCLInlineAttr(A.getRange(), S.Context, Recursive.getValueOr(0),
                    A.getAttributeSpellingListIndex());
}

static Attr *handleXlxMergeLoopAttr(Sema &S, Stmt *St, const AttributeList &A,
                                    SourceRange Range) {
  if (!isa<CompoundStmt>(St)) {
    S.Diag(A.getRange().getBegin(), diag::err_attr_in_wrong_scope)
        << "xlx_merge_loop";
    return nullptr;
  }
  if (A.getNumArgs() > 1) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 1;
    return nullptr;
  }
  llvm::Optional<int64_t> Force;
  if (A.getNumArgs() == 1)
    Force = ExtractInteger(S, A, 0, /*LB*/ 0, /*UB*/ 1);

  return ::new (S.Context)
      XlxMergeLoopAttr(A.getRange(), S.Context, Force.getValueOr(0),
                       A.getAttributeSpellingListIndex());
}

static Attr *handleXlxBindOpAttr(Sema &S, Stmt *st, const AttributeList &A,
                                 SourceRange Range) {

  if (A.getNumArgs() != 4) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 4;
    return nullptr;
  }

  Expr *var_ref = nullptr;
  Expr *latency = nullptr;
  Expr *op = nullptr, *impl = nullptr;

  if (A.isArgExpr(0) && A.getArg(0)) {
    var_ref = A.getArgAsExpr(0);
  }

  if (A.isArgExpr(1) && A.getArg(1)) {
    op = A.getArgAsExpr(1);
  }

  if (A.isArgExpr(2) && A.getArg(2)) {
    impl = A.getArgAsExpr(2);
  }

  if (A.isArgExpr(3) && A.getArg(3)) {
    latency = A.getArgAsExpr(3);
  }

  return ::new (S.Context)
      XlxBindOpAttr(A.getRange(), S.Context, var_ref, op, impl, latency,
                    A.getAttributeSpellingListIndex());
}

static Attr *handleXlxBindStorageAttr(Sema &S, Stmt *St, const AttributeList &A,
                                      SourceRange Range) {

  if (A.getNumArgs() != 4) {
    S.Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getName() << 4;
  }

  Expr *var_expr = nullptr;

  Expr *latency = nullptr;
  Expr *type = nullptr, *impl = nullptr;

  if (A.isArgExpr(0) && A.getArg(0)) {
    var_expr = A.getArgAsExpr(0);
  }

  if (A.isArgExpr(1) && A.getArg(1)) {
    type = A.getArgAsExpr(1);
  }

  if (A.isArgExpr(2) && A.getArg(2)) {
    impl = A.getArgAsExpr(2);
  }

  if (A.isArgExpr(3) && A.getArg(3)) {
    latency = A.getArgAsExpr(3);
  }

  return ::new (S.Context)
      XlxBindStorageAttr(A.getRange(), S.Context, var_expr, type, impl, latency,
                         A.getAttributeSpellingListIndex());
}

static const XCLDataFlowAttr *FindDataflowAttr(ArrayRef<const Attr *> Attrs) {
  auto I = llvm::find_if(Attrs,
                         [](const Attr *A) { return isa<XCLDataFlowAttr>(A); });
  if (I == Attrs.end())
    return nullptr;

  return cast<XCLDataFlowAttr>(*I);
}

void Sema::CheckForIncompatibleXlxAttributes(ArrayRef<const Attr *> Attrs) {
  auto *DataflowAttr = FindDataflowAttr(Attrs);
  if (!DataflowAttr)
    return;

  // Dataflow attribute is not compatible with any other loop hints
  for (const auto *I : Attrs) {
    if (!isa<OpenCLUnrollHintAttr>(I) && !isa<XlxPipelineAttr>(I) &&
        !isa<LoopHintAttr>(I))
      continue;

    Diag(I->getLocation(), diag::err_attributes_are_not_compatible)
        << I << DataflowAttr;
    Diag(DataflowAttr->getLocation(), diag::note_conflicting_attribute);
  }
}

Attr *handleXlxUnrollHintAttr(Sema &S, Stmt *stmt, const AttributeList &A,
                              SourceRange range) {
  Expr *factor = nullptr;
  int skip_exit_check = 0;
  bool isDefaultFactor = false;
  if (A.isArgExpr(0)) {
    factor = A.getArgAsExpr(0);
  }

  if (A.isArgExpr(1)) {
    Expr *expr = A.getArgAsExpr(1);
    llvm::Optional<int64_t> val = EvaluateInteger(S, expr, /*Idx*/ 0, "Unroll",
                                                  expr->getLocStart(), 0, 1);
    skip_exit_check = val.getValue();
  }

  if (!factor) {
    isDefaultFactor = true;
    factor = createIntegerLiteral(0, S, A.getLoc());
  }

  return new (S.Context)
      XlxUnrollHintAttr(A.getRange(), S.Context, factor, !!skip_exit_check,
                        isDefaultFactor, A.getAttributeSpellingListIndex());
}

Attr *handleMAXIInterface(Sema &S, Stmt*stm, const AttributeList &A, SourceRange range) 
{
  Expr *port = A.getArgAsExpr(0);
  StringRef  bundleName = "";
  if (A.getArg(1)) {
    bundleName = A.getArgAsIdent(1)->Ident->getName();
  }
  Expr *depth = nullptr;
  if (A.getArg(2)) { 
    depth = A.getArgAsExpr(2);
  }
  else { 
    depth = createIntegerLiteral(0, S, SourceLocation());
  }

  StringRef offset_type = "";
  if (A.getArg(3)) { 
    offset_type = A.getArgAsIdent(3)->Ident->getName();
  }


  StringRef signal_name = "";
  if (A.getArg(4)) { 
    signal_name = A.getArgAsIdent(4)->Ident->getName();
  }

  Expr *num_read_outstanding = nullptr;
  if (A.getArg(5)) { 
    num_read_outstanding = A.getArgAsExpr(5);
  }
  else { 
    num_read_outstanding = createIntegerLiteral(-1, S, SourceLocation());
  }

  Expr *num_write_outstanding = nullptr;
  if (A.getArg(6)) { 
    num_write_outstanding = A.getArgAsExpr(6);
  }
  else { 
    num_write_outstanding = createIntegerLiteral(-1, S, SourceLocation());
  }

  Expr *max_read_burst_length = nullptr;
  if (A.getArg(7)) { 
    max_read_burst_length = A.getArgAsExpr(7);
  }
  else { 
    max_read_burst_length = createIntegerLiteral(-1, S, SourceLocation());
  }

  Expr *max_write_burst_length = nullptr;
  if (A.getArg(8)) { 
    max_write_burst_length = A.getArgAsExpr(8);
  }
  else { 
    max_write_burst_length = createIntegerLiteral(-1, S, SourceLocation());
  }

  Expr *latency =  nullptr;
  if (A.getArg(9)) { 
    latency = A.getArgAsExpr(9);
  }
  else { 
    latency = createIntegerLiteral(-1, S, A.getLoc());
  }

  Expr *max_widen_bitwidth = nullptr;
  if (A.getArg(10)) { 
    max_widen_bitwidth = A.getArgAsExpr(10);
  }
  else { 
    max_widen_bitwidth = createIntegerLiteral(-1, S, SourceLocation());
  }

  return new (S.Context)MAXIInterfaceAttr( A.getRange(), S.Context, 
      port, bundleName, depth, offset_type, signal_name, 
      num_read_outstanding, num_write_outstanding, max_read_burst_length, max_write_burst_length, 
      latency, max_widen_bitwidth, A.getAttributeSpellingListIndex());
}

Attr *handleAXIStreamInterface( Sema&S, Stmt* stm, const AttributeList &A, SourceRange range) 
{
  Expr *port = A.getArgAsExpr(0);

  bool isRegister = false;
  if (A.getArg(1)) { 
    isRegister = true;
  }

  StringRef register_mode = "both";
  if (A.getArg(2)) { 
    register_mode = A.getArgAsIdent(2)->Ident->getName();
  }


  Expr *depth = nullptr;
  if (A.getArg(3) ){ 
    depth = A.getArgAsExpr(3);
  }
  else { 
    depth = createIntegerLiteral(0, S, SourceLocation());
  }

  StringRef signal_name = "";
  if (A.getArg(4)) { 
    signal_name = A.getArgAsIdent(4)->Ident->getName();
  }
  AXIStreamInterfaceAttr::RegisterModeEnum register_mode_enum;
  AXIStreamInterfaceAttr::ConvertStrToRegisterModeEnum(register_mode, register_mode_enum);


  return new (S.Context)AXIStreamInterfaceAttr( A.getRange(), S.Context, 
    port, isRegister, register_mode_enum, depth, signal_name, A.getAttributeSpellingListIndex());

}

Attr *handleAPFifoInterface( Sema &S, Stmt *stmt, const AttributeList &A, SourceRange range) 
{
  Expr *port = A.getArgAsExpr(0);


  bool isRegister = false;
  if (A.getArg(1)){ 
    isRegister = true;
  }

  Expr *depth = nullptr;
  if (A.getArg(2)) { 
    depth = A.getArgAsExpr(2);
  }
  else { 
    depth = createIntegerLiteral(0, S, SourceLocation());
  }

  StringRef signal_name = "";
  if (A.getArg(3)) { 
    signal_name = A.getArgAsIdent(3)->Ident->getName();
  }
  return new (S.Context) APFifoInterfaceAttr( A.getRange(), S.Context, 
      port, isRegister, depth, signal_name,
      A.getAttributeSpellingListIndex());
}

Attr *handleAPScalarInterface( Sema &S, Stmt *stmt, const AttributeList &A, SourceRange range) 
{
  Expr *port = A.getArgAsExpr(0);

  StringRef mode = A.getArgAsIdent(1)->Ident->getName();

  bool isRegister = false;
  if (A.getArg(2)) { 
    isRegister = true;
  }

  StringRef signal_name= "";
  if (A.getArg(3)) { 
    signal_name = A.getArgAsIdent(3)->Ident->getName();
  }
  return new (S.Context) APScalarInterfaceAttr(A.getRange(), S.Context, port, mode, isRegister, signal_name, A.getAttributeSpellingListIndex());

}




Attr *handleSAXILITEOffsetInterfaceAttr(Sema &S, Stmt *stmt, const AttributeList &A, SourceRange range) 
{
  Expr* port = A.getArgAsExpr(0);
  StringRef  bundleName = ""; 
  Expr* offset_expr = nullptr; 
  bool IsRegister = false;
  StringRef signal_name = "";
  /* set default value */

  if (A.getArg(1)) { 
    bundleName = A.getArgAsIdent(1)->Ident->getName();
  }
  else { 
    bundleName = "";
  }

  if (A.getArg(2)){
    offset_expr = A.getArgAsExpr(2);
  }
  else { 
    offset_expr = createIntegerLiteral(-1, S, SourceLocation());
  }

  if (A.getArg(3)) {
    IsRegister = true;
  }
  else { 
    IsRegister = false;
  }

  if (A.getArg(4)) { 
    signal_name = A.getArgAsIdent(4)->Ident->getName();
  }

  return new(S.Context)SAXILITEOffsetInterfaceAttr(A.getRange(), S.Context, port, bundleName, offset_expr, IsRegister, signal_name,
      A.getAttributeSpellingListIndex());
}

Attr* handleMemoryInterface(Sema &S, Stmt* stmt, const AttributeList &A, SourceRange range) 
{
  Expr *port = A.getArgAsExpr(0);
  assert(A.getArg(1) &&"unexpected, parser should generated it already");
  StringRef mode = A.getArgAsIdent(1)->Ident->getName();

  StringRef storage_type ="";
  if (A.getArg(2)) { 
    storage_type = A.getArgAsIdent(2)->Ident->getName();
  }

  Expr *latency = nullptr;
  if (A.getArg(3)) { 
    latency = A.getArgAsExpr(3);
  }
  else { 
    latency = createIntegerLiteral(-1, S, SourceLocation());
  }

  StringRef signal_name = "";
  if (A.getArg(4)) { 
    signal_name = A.getArgAsIdent(4)->Ident->getName();
  }

  return new (S.Context) MemoryInterfaceAttr(A.getRange(), S.Context, port, mode, storage_type, latency, signal_name, A.getAttributeSpellingListIndex());
}

Attr *Sema::ProcessXlxStmtAttributes(Stmt *S, const AttributeList &A,
                                     SourceRange Range) {
  switch (A.getKind()) {
  default:
    break;
  case AttributeList::AT_XCLUnrollWorkitems:
    return handleXCLUnrollWorkitemsAttr(*this, S, A, Range);
  case AttributeList::AT_XlxUnrollHint:
    return handleXlxUnrollHintAttr(*this, S, A, Range);
  case AttributeList::AT_XCLSingleWorkitem:
    return handleXCLSingleWorkitemAttr(*this, S, A, Range);
  case AttributeList::AT_XlxPipeline:
    return handleXlxPipelineAttr(*this, S, A, Range);
  case AttributeList::AT_XCLPipelineLoop:
    return handleXCLPipelineLoopAttr(*this, S, A, Range);
  case AttributeList::AT_XCLPipelineWorkitems:
    return handleXCLPipelineWorkitemsAttr(*this, S, A, Range);
  case AttributeList::AT_XCLDataFlow:
    return handleXCLDataFlowAttr(*this, S, A, Range);
  case AttributeList::AT_XCLFlattenLoop:
    return handleXCLFlattenLoopAttr(*this, S, A, Range);
  case AttributeList::AT_XCLLoopTripCount:
    return handleXCLLoopTripCountAttr(*this, S, A, Range);
  case AttributeList::AT_XlxLoopTripCount:
    return handleXlxLoopTripCountAttr(*this, S, A, Range);
  case AttributeList::AT_FPGAResourceLimitHint:
    return handleFPGAResourceLimitHintAttr(*this, S, A, Range);
  case AttributeList::AT_XlxFunctionAllocation:
    return handleXlxFunctionAllocationAttr(*this, S, A, Range);
  case AttributeList::AT_XCLRegionName:
    return handleXCLRegionNameAttr(*this, S, A, Range);
  case AttributeList::AT_XCLArrayView:
    return handleXCLArrayViewAttr(*this, S, A, Range);
  case AttributeList::AT_XlxExprBalance:
    return handleXlxExprBalanceAttr(*this, S, A, Range);
  case AttributeList::AT_XlxOccurrence:
    return handleXlxOccurrenceAttr(*this, S, A, Range);
  case AttributeList::AT_XlxProtocol:
    return handleXlxProtocolAttr(*this, S, A, Range);
  case AttributeList::AT_XCLLatency:
    return handleXCLLatencyAttr(*this, S, A, Range);
  case AttributeList::AT_XCLInline:
    return handleXCLInlineAttr(*this, S, A, Range);
  case AttributeList::AT_XlxMergeLoop:
    return handleXlxMergeLoopAttr(*this, S, A, Range);
  case AttributeList::AT_XlxDependence:
    return handleDependence<XlxDependenceAttr>(*this, S, A, Range);
  case AttributeList::AT_XCLDependence:
    return handleDependence<XCLDependenceAttr>(*this, S, A, Range);

  case AttributeList::AT_XlxCrossDependence: 
    return handleXlxCrossDependence(*this, S, A, Range);

  case AttributeList::AT_XlxReqdPipeDepth:
    return handleXlxReqdPipeDepth(*this, S, A, Range);
  case AttributeList::AT_XlxStable:
    return handleXlxStable(*this, S, A, Range);
  case AttributeList::AT_XlxStableContent:
    return handleXlxStableContent(*this, S, A, Range);
  case AttributeList::AT_XlxShared:
    return handleXlxShared(*this, S, A, Range);
  case AttributeList::AT_XlxDisaggr:
    return handleXlxDisaggr(*this, S, A, Range);
  case AttributeList::AT_XlxAggregate:
    return handleXlxAggregate(*this, S, A, Range);
  case AttributeList::AT_XlxDataPack:
    return handleXlxDataPack(*this, S, A, Range);
  case AttributeList::AT_XlxBindOp:
    return handleXlxBindOpAttr(*this, S, A, Range);
  case AttributeList::AT_XlxArrayXForm:
    return handleXlxArrayXForm(*this, S, A, Range);
  case AttributeList::AT_XlxArrayGeometry:
    return handleXlxArrayGeometry(*this, S, A, Range);
  case AttributeList::AT_XlxBindStorage:
    return handleXlxBindStorageAttr(*this, S, A, Range);

  case AttributeList::AT_MAXIInterface:
    return handleMAXIInterface(*this, S, A, Range);

  case AttributeList::AT_AXIStreamInterface: 
    return handleAXIStreamInterface(*this, S, A, Range);

  case AttributeList::AT_MemoryInterface:
    return handleMemoryInterface(*this, S, A, Range);

  case AttributeList::AT_SAXILITEOffsetInterface: 
    return handleSAXILITEOffsetInterfaceAttr(*this, S, A, Range);

  case AttributeList::AT_APFifoInterface:
    return handleAPFifoInterface(*this, S, A, Range);

  case AttributeList::AT_APScalarInterface: 
    return handleAPScalarInterface(*this, S, A, Range);

  }

  return nullptr;
}

static void handleMAXIAliasAttr(Sema &S, Decl *D, const AttributeList &Attr) { 
  if (Attr.getNumArgs() != 1) { 
    S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments)
        << Attr.getName() << 1;
    return ;
  }
  Expr * offset = Attr.getArgAsExpr(0);
  SourceLocation loc = Attr.getLoc();
  int alias_group = loc.getRawEncoding();
  D->addAttr(::new(S.Context) MAXIAliasAttr(Attr.getRange(), S.Context, offset, alias_group, Attr.getAttributeSpellingListIndex()));
}

static void handleHLSPreserve(Sema &S, Decl *D, const AttributeList &Attr) { 
  D->addAttr(::new(S.Context) HLSPreserveAttr(Attr.getRange(), S.Context, Attr.getAttributeSpellingListIndex()));
}


bool Sema::ProcessXlxDeclAttributes(Scope *scope, Decl *D,
                                    const AttributeList &Attr) {
  switch (Attr.getKind()) {
  default:
    break;
  case AttributeList::AT_SAXIAdaptor:
    handleSAXIAdaptor(*this, D, Attr);
    return true;
  case AttributeList::AT_NoCtor:
    handleNoCtor(*this, D, Attr);
    return true;
  case AttributeList::AT_SDxKernel:
    handleSDxKernel(*this, D, Attr);
    return true;
  case AttributeList::AT_XCLArrayGeometry:
    handleXCLArrayGeometry(*this, D, Attr);
    return true;
  case AttributeList::AT_XCLMaxWorkGroupSize:
    handleXCLMaxWorkGroupSize(*this, D, Attr);
    return true;
  case AttributeList::AT_XCLVisibility:
    handleXCLVisibility(*this, D, Attr);
    return true;
  case AttributeList::AT_XCLZeroGlobalWorkOffset:
    handleXCLZeroGlobalWorkOffset(*this, D, Attr);
    return true;
  case AttributeList::AT_XCLArrayXForm:
    handleXCLArrayXForm(*this, D, Attr);
    return true;
  case AttributeList::AT_XCLLatency:
    handleDeclXCLLatencyAttr(*this, D, Attr);
    return true;
  case AttributeList::AT_XlxResourceIPCore:
    handleXlxResourceIPCore(*this, D, Attr);
    return true;
  case AttributeList::AT_MAXIAlias:
    handleMAXIAliasAttr(*this, D, Attr);
    return true;
  case AttributeList::AT_HLSPreserve: 
    handleHLSPreserve(*this, D, Attr);
    return true;
  };

  return false;
}

/// CheckXLCLatencyAttr checks whether the XCLLatencyAttr is semantically
/// correct. Return true if the attribute violate the semantics, vice versa.
bool Sema::CheckXCLLatencyExprs(Expr *MinExpr, Expr *MaxExpr,
                                SourceLocation Loc, StringRef AttrName) {
  auto Min = EvaluateInteger(*this, MinExpr, /*Idx*/ 0, AttrName, Loc,
                             /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!Min.hasValue())
    return true;

  auto Max = EvaluateInteger(*this, MaxExpr, /*Idx*/ 1, AttrName, Loc,
                             /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!Max.hasValue())
    return true;

  if (Min.getValue() > Max.getValue()) {
    Diag(Loc, diag::err_attribute_argument_invalid)
        << str("'" + AttrName + "'") << 1;
    return true;
  }

  return false;
}

bool Sema::CheckOpenCLUnrollHintExprs(Expr *Factor, SourceLocation Loc,
                                      StringRef AttrName) {
  // If Factor is value dependent, we can skip the check and leave it
  // to template instantiation. We will invoke this check again during
  // template instantiation.
  if (Factor->isValueDependent())
    return false;

  // this checker is only used by opencl,  HLS use "xlx_unroll_hint"
  // for "opencl_unroll_hint"  , "factor= 0" is valid value
  auto F = EvaluateInteger(*this, Factor, /*Idx*/ 0, AttrName, Loc,
                           /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!F.hasValue())
    return true;

  return false;
}

bool Sema::CheckXCLLoopTripCountExprs(Expr *Min, Expr *Max, Expr *&Avg,
                                      SourceLocation Loc, StringRef AttrName) {
  // If any exprs are value dependent, we can skip the check and leave it
  // to template instantiation. We will invoke this check again during
  // template instantiation.
  // TODO,  delete following Template dependentParameter check,
  // HoistXlxAttr will not handle TempalteFunction , HoistXlxAttr only
  // handle instantiated TemplateFunction, so it isn't expecetd there
  // are expression about DependentParameter
  if (Min->isValueDependent() || Max->isValueDependent() ||
      (Avg && Avg->isValueDependent()))
    return false;

  auto MinInt = EvaluateInteger(*this, Min, /*Idx*/ 0, AttrName, Loc,
                                /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!MinInt.hasValue())
    return false;

  auto MaxInt = EvaluateInteger(*this, Max, /*Idx*/ 1, AttrName, Loc,
                                /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!MaxInt.hasValue())
    return false;

  auto MinVal = MinInt.getValue();
  auto MaxVal = MaxInt.getValue();

  if (MinVal > MaxVal) {
    Diag(Loc, diag::err_attribute_argument_invalid)
        << str("'" + AttrName + "'") << 2;
    return false;
  }

  if (Avg) {
    auto AvgInt = EvaluateInteger(*this, Avg, /*Idx*/ 2, AttrName, Loc,
                                  /*LB*/ MinVal, /*UB*/ MaxVal);
    if (!AvgInt.hasValue())
      return false;

    auto AvgVal = AvgInt.getValue();

    if (MinVal > AvgVal || AvgVal > MaxVal) {
      Diag(Loc, diag::err_attribute_argument_invalid) << AttrName << 2;
      return false;
    }
  } else {
    auto AvgVal = (MinVal + MaxVal) / 2;
    Avg = createIntegerLiteral(AvgVal, *this, Loc);
  }

  return true;
}

bool Sema::CheckFPGAResourceHintExprs(Expr *Latency, SourceLocation Loc,
                                      StringRef AttrName) {
  // If Latency is value dependent, we can skip the check and leave it
  // to template instantiation. We will invoke this check again during
  // template instantiation.
  if (Latency->isValueDependent())
    return false;

  auto LatencyInt = EvaluateInteger(*this, Latency, /*Idx*/ 2, AttrName, Loc,
                                    /*LB*/ -1, /*UB*/ INT32_MAX);
  if (!LatencyInt.hasValue())
    return true;

  return false;
}

bool Sema::CheckFPGAResourceLimitHintExprs(Expr *Limit, SourceLocation Loc,
                                           StringRef AttrName) {
  // If Limit is value dependent, we can skip the check and leave it
  // to template instantiation. We will invoke this check again during
  // template instantiation.
  if (Limit->isValueDependent())
    return false;

  auto LimitInt = EvaluateInteger(*this, Limit, /*Idx*/ 2, AttrName, Loc,
                                  /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!LimitInt.hasValue())
    return true;

  return false;
}

bool Sema::CheckXCLReqdPipeDepthExprs(Expr *Depth, SourceLocation Loc,
                                      StringRef AttrName) {
  // If Depth is value dependent, we can skip the check and leave it
  // to template instantiation. We will invoke this check again during
  // template instantiation.
  if (Depth->isValueDependent())
    return false;

  auto DepthInt = EvaluateInteger(*this, Depth, /*Idx*/ 2, AttrName, Loc,
                                  /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!DepthInt.hasValue())
    return true;

  return false;
}

bool Sema::CheckXlxOccurrenceExprs(Expr *Cycle, SourceLocation Loc,
                                   StringRef AttrName) {
  // If Cycle is value dependent, we can skip the check and leave it
  // to template instantiation. We will invoke this check again during
  // template instantiation.
  if (Cycle->isValueDependent())
    return false;

  auto CycleInt = EvaluateInteger(*this, Cycle, /*Idx*/ 0, AttrName, Loc,
                                  /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!CycleInt.hasValue())
    return true;

  return false;
}

bool Sema::CheckMAXIAdaptorExprs(Expr *ReadOutStanding, Expr *WriteOutStanding,
                                 Expr *ReadBurstLength, Expr *WriteBurstLength,
                                 Expr *Latency, SourceLocation Loc,
                                 StringRef AttrName) {
  // If any Expr is value dependent, we can skip the check and leave it
  // to template instantiation. We will invoke this check again during
  // template instantiation.
  if (ReadOutStanding->isValueDependent() ||
      WriteOutStanding->isValueDependent() ||
      ReadBurstLength->isValueDependent() ||
      WriteBurstLength->isValueDependent() || Latency->isValueDependent())
    return false;

  auto ROSInt = EvaluateInteger(*this, ReadOutStanding, /*Idx*/ 1, AttrName,
                                Loc, /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!ROSInt.hasValue())
    return true;

  auto WOSInt = EvaluateInteger(*this, WriteOutStanding, /*Idx*/ 2, AttrName,
                                Loc, /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!WOSInt.hasValue())
    return true;

  auto RBLInt = EvaluateInteger(*this, ReadBurstLength, /*Idx*/ 3, AttrName,
                                Loc, /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!RBLInt.hasValue())
    return true;

  auto WBLInt = EvaluateInteger(*this, WriteBurstLength, /*Idx*/ 4, AttrName,
                                Loc, /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!WBLInt.hasValue())
    return true;

  auto LatInt = EvaluateInteger(*this, Latency, /*Idx*/ 5, AttrName, Loc,
                                /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!LatInt.hasValue())
    return true;

  return false;
}

bool Sema::CheckSAXIAdaptorExprs(SourceLocation Loc, StringRef AttrName) {
  return false;
}

bool Sema::CheckBRAMAdaptorExprs(Expr *Latency, SourceLocation Loc,
                                 StringRef AttrName) {
  if (Latency->isValueDependent())
    return false;

  auto LatInt = EvaluateInteger(*this, Latency, /*Idx*/ 3, AttrName, Loc,
                                /*LB*/ -1, /*UB*/ INT32_MAX);
  if (!LatInt.hasValue())
    return true;

  return false;
}

bool Sema::CheckFPGAScalarInterfaceWrapperExprs(Expr *Offset,
                                                SourceLocation Loc,
                                                StringRef AttrName) {
  if (Offset == nullptr)
    return false;
  // If Offset is value dependent, we can skip the check and leave it
  // to template instantiation. We will invoke this check again during
  // template instantiation.
  if (Offset->isValueDependent())
    return false;

  auto OffsetInt = EvaluateInteger(*this, Offset, /*Idx*/ 2, AttrName, Loc,
                                   /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!OffsetInt.hasValue())
    return true;

  return false;
}

bool Sema::CheckFPGADataFootPrintHintExprs(Expr *Depth, SourceLocation Loc,
                                           StringRef AttrName) {
  // If Depth is value dependent, we can skip the check and leave it
  // to template instantiation. We will invoke this check again during
  // template instantiation.
  if (Depth->isValueDependent())
    return false;

  auto DepthInt = EvaluateInteger(*this, Depth, /*Idx*/ 0, AttrName, Loc,
                                  /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!DepthInt.hasValue())
    return true;

  return false;
}

bool Sema::CheckFPGAMaxiMaxWidenBitwidthExprs(Expr *MaxWidenBitwidth,
                                              SourceLocation Loc,
                                              StringRef AttrName) {
  // If MaxWidenBitwidth is value dependent, we can skip the check and leave it
  // to template instantiation. We will invoke this check again during
  // template instantiation.
  if (MaxWidenBitwidth->isValueDependent())
    return false;

  // Here clang only focuses on 'basic check'.
  // Real check will be in LLVM instead, including pow of 2 and real range
  auto MaxWidenBitwidthInt =
      EvaluateInteger(*this, MaxWidenBitwidth, /*Idx*/ 0, AttrName, Loc,
                      /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!MaxWidenBitwidthInt.hasValue())
    return true;

  return false;
}

bool Sema::CheckFPGAMaxiLatencyExprs(Expr *Latency, SourceLocation Loc,
                                     StringRef AttrName) {
  // If Latency is value dependent, we can skip the check and leave it
  // to template instantiation. We will invoke this check again during
  // template instantiation.
  if (Latency->isValueDependent())
    return false;

  auto LatencyInt = EvaluateInteger(*this, Latency, /*Idx*/ 0, AttrName, Loc,
                                    /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!LatencyInt.hasValue())
    return true;

  return false;
}

bool Sema::CheckFPGAMaxiNumRdOutstandExprs(Expr *NumRdOutstand,
                                           SourceLocation Loc,
                                           StringRef AttrName) {
  // If NumRdOutstand is value dependent, we can skip the check and leave it
  // to template instantiation. We will invoke this check again during
  // template instantiation.
  if (NumRdOutstand->isValueDependent())
    return false;

  auto NumRdOutstandInt =
      EvaluateInteger(*this, NumRdOutstand, /*Idx*/ 0, AttrName, Loc,
                      /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!NumRdOutstandInt.hasValue())
    return true;

  return false;
}

bool Sema::CheckFPGAMaxiNumWtOutstandExprs(Expr *NumWtOutstand,
                                           SourceLocation Loc,
                                           StringRef AttrName) {
  // If NumWtOutstand is value dependent, we can skip the check and leave it
  // to template instantiation. We will invoke this check again during
  // template instantiation.
  if (NumWtOutstand->isValueDependent())
    return false;

  auto NumWtOutstandInt =
      EvaluateInteger(*this, NumWtOutstand, /*Idx*/ 0, AttrName, Loc,
                      /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!NumWtOutstandInt.hasValue())
    return true;

  return false;
}

bool Sema::CheckFPGAMaxiRdBurstLenExprs(Expr *RdBurstLen, SourceLocation Loc,
                                        StringRef AttrName) {
  // If RdBurstLen is value dependent, we can skip the check and leave it
  // to template instantiation. We will invoke this check again during
  // template instantiation.
  if (RdBurstLen->isValueDependent())
    return false;

  auto RdBurstLenInt =
      EvaluateInteger(*this, RdBurstLen, /*Idx*/ 0, AttrName, Loc,
                      /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!RdBurstLenInt.hasValue())
    return true;

  return false;
}

bool Sema::CheckFPGAMaxiWtBurstLenExprs(Expr *WtBurstLen, SourceLocation Loc,
                                        StringRef AttrName) {
  // If WtBurstLen is value dependent, we can skip the check and leave it
  // to template instantiation. We will invoke this check again during
  // template instantiation.
  if (WtBurstLen->isValueDependent())
    return false;

  auto WtBurstLenInt =
      EvaluateInteger(*this, WtBurstLen, /*Idx*/ 0, AttrName, Loc,
                      /*LB*/ 0, /*UB*/ INT32_MAX);
  if (!WtBurstLenInt.hasValue())
    return true;

  return false;
}

static bool instantiateXCLLatencyDeclAttr(Sema &S, XCLLatencyAttr *A,
                                          Decl *NewD) {
  if (S.CheckXCLLatencyExprs(A->getMin(), A->getMax(), A->getLocation(),
                             A->getSpelling())) {
    return false;
  }

  NewD->addAttr(A);
  return true;
}

// TODO, clean following obsolted code
static bool instantiateOpenCLUnrollHintDeclAttr(Sema &S,
                                                OpenCLUnrollHintAttr *A,
                                                Decl *NewD) {
  S.Diag(A->getRange().getBegin(), diag::err_attr_in_wrong_scope)
      << "xlx_opencl_unroll_hint";
  return false;
}

static bool instantiateXCLLoopTripCountDeclAttr(Sema &S,
                                                XCLLoopTripCountAttr *A,
                                                Decl *NewD) {
  S.Diag(A->getRange().getBegin(), diag::err_attr_in_wrong_scope)
      << "xlx_loop_tripcount";
  return false;
}

static bool instantiateFPGAResourceHintDeclAttr(Sema &S,
                                                FPGAResourceHintAttr *A,
                                                Decl *NewD) {
  if (S.CheckFPGAResourceHintExprs(A->getLatency(), A->getLocation(),
                                   A->getSpelling())) {
    return false;
  }

  NewD->addAttr(A);
  return true;
}

static bool
instantiateFPGAResourceLimitHintDeclAttr(Sema &S, FPGAResourceLimitHintAttr *A,
                                         Decl *NewD) {
  if (S.CheckFPGAResourceLimitHintExprs(A->getLimit(), A->getLocation(),
                                        A->getSpelling())) {
    return false;
  }

  NewD->addAttr(A);
  return true;
}

static bool instantiateXlxOccurrenceDeclAttr(Sema &S, XlxOccurrenceAttr *A,
                                             Decl *NewD) {
  S.Diag(A->getRange().getBegin(), diag::err_attr_in_wrong_scope)
      << "xlx_occurrence";
  return false;
}

static bool instantiateMAXIAdaptorDeclAttr(Sema &S, MAXIAdaptorAttr *A,
                                           Decl *NewD) {
  if (S.CheckMAXIAdaptorExprs(
          A->getNumReadOutstanding(), A->getNumWriteOutstanding(),
          A->getMaxReadBurstLength(), A->getMaxWriteBurstLength(),
          A->getLatency(), A->getLocation(), A->getSpelling())) {
    return false;
  }

  NewD->addAttr(A);
  return true;
}

static bool instantiateBRAMAdaptorDeclAttr(Sema &S, BRAMAdaptorAttr *A,
                                           Decl *NewD) {
  if (S.CheckBRAMAdaptorExprs(A->getLatency(), A->getLocation(),
                              A->getSpelling())) {
    return false;
  }

  NewD->addAttr(A);
  return true;
}

static bool instantiateSAXIAdaptorDeclAttr(Sema &S, SAXIAdaptorAttr *A,
                                           Decl *NewD) {
  NewD->addAttr(A);
  return true;
}

static bool
instantiateFPGADataFootPrintHintDeclAttr(Sema &S, FPGADataFootPrintHintAttr *A,
                                         Decl *NewD) {
  if (S.CheckFPGADataFootPrintHintExprs(A->getDepth(), A->getLocation(),
                                        A->getSpelling())) {
    return false;
  }

  NewD->addAttr(A);
  return true;
}

static bool instantiateFPGAMaxiMaxWidenBitwidthDeclAttr(
    Sema &S, FPGAMaxiMaxWidenBitwidthAttr *A, Decl *NewD) {
  if (S.CheckFPGAMaxiMaxWidenBitwidthExprs(
          A->getMaxWidenBitwidth(), A->getLocation(), A->getSpelling())) {
    return false;
  }

  NewD->addAttr(A);
  return true;
}

static bool instantiateFPGAMaxiLatencyDeclAttr(Sema &S, FPGAMaxiLatencyAttr *A,
                                               Decl *NewD) {
  if (S.CheckFPGAMaxiLatencyExprs(A->getLatency(), A->getLocation(),
                                  A->getSpelling())) {
    return false;
  }

  NewD->addAttr(A);
  return true;
}

static bool
instantiateFPGAMaxiNumRdOutstandDeclAttr(Sema &S, FPGAMaxiNumRdOutstandAttr *A,
                                         Decl *NewD) {
  if (S.CheckFPGAMaxiNumRdOutstandExprs(A->getNumRdOutstand(), A->getLocation(),
                                        A->getSpelling())) {
    return false;
  }

  NewD->addAttr(A);
  return true;
}

static bool
instantiateFPGAMaxiNumWtOutstandDeclAttr(Sema &S, FPGAMaxiNumWtOutstandAttr *A,
                                         Decl *NewD) {
  if (S.CheckFPGAMaxiNumWtOutstandExprs(A->getNumWtOutstand(), A->getLocation(),
                                        A->getSpelling())) {
    return false;
  }

  NewD->addAttr(A);
  return true;
}

static bool instantiateFPGAMaxiRdBurstLenDeclAttr(Sema &S,
                                                  FPGAMaxiRdBurstLenAttr *A,
                                                  Decl *NewD) {
  if (S.CheckFPGAMaxiRdBurstLenExprs(A->getRdBurstLen(), A->getLocation(),
                                     A->getSpelling())) {
    return false;
  }

  NewD->addAttr(A);
  return true;
}

static bool instantiateFPGAMaxiWtBurstLenDeclAttr(Sema &S,
                                                  FPGAMaxiWtBurstLenAttr *A,
                                                  Decl *NewD) {
  if (S.CheckFPGAMaxiWtBurstLenExprs(A->getWtBurstLen(), A->getLocation(),
                                     A->getSpelling())) {
    return false;
  }

  NewD->addAttr(A);
  return true;
}

bool Sema::instantiateXlxDeclAttr(Attr *NewAttr, Decl *NewD) {
  if (!NewAttr)
    return false;

  switch (NewAttr->getKind()) {
#define ATTR(X)
#define XLX_PRAGMA_SPELLING_ATTR(X)                                            \
  case attr::X:                                                                \
    return instantiate##X##DeclAttr(*this, cast<X##Attr>(NewAttr), NewD);
#include "clang/Basic/AttrList.inc"
  default:
    return false;
  }
}

// TODO, delete it
namespace {
// Partially transform the constant expr.
// As we may hoist the attributes to their parent scope, when we perform
// template instantiation, the attribute may depend on the exprs that are not
// yet instantiated. Therefore, we need to partially transform the constant
// value such that the attributes will no longer depend on the uninstantitated
// exprs. For example, template<...> void test() {
//   constant static int S = 1;
//   #pragma HLS pipeline II = S;
// }
// the pipeline will become a function attribtue (hoisted from the function
// body) which depend on the S in the body. This may crash if we don't transform
// the attribute such that it no longer depends on the S. We can evaluate the
// attribute partially(fully in this case) into template<...> void test() {
//   constant static int S = 1;
//   #pragma HLS pipeline II = 1;
// }
// now the pipeline attribute will no longer depend on the body.
class TransformPartialConstantExpr
    : public TreeTransform<TransformPartialConstantExpr> {
  typedef TreeTransform<TransformPartialConstantExpr> BaseTransform;

public:
  TransformPartialConstantExpr(Sema &SemaRef) : BaseTransform(SemaRef) {}

  // Replace the DeclRefExpr with Integer Constant Expression if possible.
  // TODO: We can do recursive substitution of the constant integer expression.
  ExprResult TransformDeclRefExpr(DeclRefExpr *E) {
    auto Ty = E->getDecl()->getType();
    // Only replace const integer type
    if (!Ty.isConstQualified() || !Ty->isIntegerType())
      return E;

    if (auto *Decl = dyn_cast<VarDecl>(E->getDecl())) {
      return TransformExpr(Decl->getInit());
    }

    return E;
  }
};
} // namespace

ExprResult Sema::CheckOrBuildPartialConstExpr(Expr *E) {
  TransformPartialConstantExpr PCE(*this);
  return PCE.TransformExpr(E);
}

/*
 * some clarify for HoistXlxScope:
 *
 * 1. ASConsumer:
 *  HandleTopLevelDecl is called after parser generate one Decl
 *  take notation that Class/Function instantiate  in
 * SemaTemplateInstantiate.cpp file  will generate new TopLevelDecl , and call
 * ASTConsumer::HandleTopLevelDecl
 *
 * 2. can we use a standalone FrontendAction to handle HoistXlxScope ?
 *    No,  ParseAST is drived by FrontendAction, CodeGenAction is subclass of
 *    FrontendAction, we can not add a standalone Action which call Parse source
 * code and generate AST in memory, and feed the memory AST to CodgenAction,
 *    Clang 's action mechanism doesn't support it
 *
 * 3. When HoistXlxScope is called ?
 * ParseAST call HandleTopLevelDecl for Sema's ASTConsumer , then
 * ASTConsumer::HandleTopLevelDecl is called, and do Xlx Scope Hoist
 *
 */

class XlxAttrHoistConsumer : public SemaConsumer {
  Sema *sema_ptr;

public:
  virtual bool HandleTopLevelDecl(DeclGroupRef D);
  virtual void InitializeSema(Sema &sema);
  void HoistXlxScope(Decl *decl);
};

void XlxAttrHoistConsumer::InitializeSema(Sema &sema) { sema_ptr = &sema; }

bool XlxAttrHoistConsumer::HandleTopLevelDecl(DeclGroupRef D) {
  for (DeclGroupRef::iterator I = D.begin(), E = D.end(); I != E; I++) {
    HoistXlxScope(*I);
  }
  return true;
}

/*======================================= following is for Dataflow-Lawyer
 * checker ==================*/
// If Statement is an incemement or decrement, return true and sets the
// variables Increment and DRE.
static bool ProcessIterationStmt(DiagnosticsEngine &Diags, ASTContext &Context,
                                 Stmt *Statement, bool &Increment,
                                 DeclRefExpr *&DRE) {
  if (auto Cleanups = dyn_cast<ExprWithCleanups>(Statement))
    if (!Cleanups->cleanupsHaveSideEffects())
      Statement = Cleanups->getSubExpr();

  if (UnaryOperator *UO = dyn_cast<UnaryOperator>(Statement)) {
    switch (UO->getOpcode()) {
    default:
      return false;
    case UO_PostInc:
    case UO_PreInc:
      Increment = true;
      break;
    case UO_PostDec:
    case UO_PreDec:
      Increment = false;
      break;
    }
    DRE = dyn_cast<DeclRefExpr>(UO->getSubExpr()->IgnoreImpCasts());
    return DRE;
  }

  if (CXXOperatorCallExpr *Call = dyn_cast<CXXOperatorCallExpr>(Statement)) {
    FunctionDecl *FD = Call->getDirectCallee();
    if (FD && FD->isOverloadedOperator()) {
      switch (FD->getOverloadedOperator()) {
      default:
        return false;
      case OO_PlusPlus:
        Increment = true;
        break;
      case OO_MinusMinus:
        Increment = false;
        break;
      }
      DRE = dyn_cast<DeclRefExpr>(Call->getArg(0)->IgnoreImpCasts());
      return DRE;
    }
  }
  Diags.Report(Statement->getLocStart(),
               diag::warn_ignore_xcl_dataflow_on_invalid_loop_iteration_stmt);
  return false;
}

static bool CheckLoopIterationVariable(DiagnosticsEngine &Diags,
                                       ASTContext &Context, Stmt *InitStmt,
                                       Decl *CondVar, Decl *IncVar) {

  if (DeclStmt *Istmt = dyn_cast<DeclStmt>(InitStmt)) {
    if (Istmt->isSingleDecl()) {
      auto *DIVar = Istmt->getSingleDecl();
      if (DIVar == CondVar && DIVar == IncVar)
        return true;
      else {
        Diags.Report(
            InitStmt->getLocStart(),
            diag::warn_ignore_xcl_dataflow_on_multiple_loop_induction_variable);
        return false;
      }
    }
  }

  Diags.Report(InitStmt->getLocStart(),
               diag::warn_ignore_xcl_dataflow_on_invalid_loop_initial_stmt);

  return false;
}

static bool CheckLoopConditionStmt(DiagnosticsEngine &Diags,
                                   ASTContext &Context, Expr *Cond) {
  if (isa<IntegerLiteral>(Cond))
    return true;
  else if (Cond->getType()->isIntegralOrEnumerationType() &&
           Cond->isEvaluatable(Context))
    // expression can be constant folded
    return true;
  else if (DeclRefExpr *DR = dyn_cast<DeclRefExpr>(Cond)) {
    if (isa<ParmVarDecl>(DR->getDecl()))
      return true;
  }
  Diags.Report(Cond->getLocStart(),
               diag::warn_ignore_xcl_dataflow_on_invalid_loop_bound);
  return false;
}

static bool ParseLoopConditionalStatement(Expr *Condition, Decl *&CondVar,
                                          Expr *&CondExpr) {

  if (auto Cleanups = dyn_cast<ExprWithCleanups>(Condition))
    if (!Cleanups->cleanupsHaveSideEffects())
      Condition = Cleanups->getSubExpr();

  if (auto *CmpCond = dyn_cast<BinaryOperator>(Condition)) {
    if (CmpCond->isComparisonOp()) {
      auto expr = dyn_cast<DeclRefExpr>(CmpCond->getLHS()->IgnoreImpCasts());
      if (expr)
        CondVar = expr->getDecl();
      CondExpr = CmpCond->getRHS()->IgnoreImpCasts();
      return true;
    }
  }

  if (CXXOperatorCallExpr *Call = dyn_cast<CXXOperatorCallExpr>(Condition)) {
    FunctionDecl *FD = Call->getDirectCallee();
    if (FD && FD->isOverloadedOperator()) {
      switch (FD->getOverloadedOperator()) {
      default:
        break;
      case OO_ExclaimEqual:
      case OO_EqualEqual:
      case OO_GreaterEqual:
      case OO_LessEqual:
      case OO_Less:
      case OO_Greater: {
        auto expr = dyn_cast<DeclRefExpr>(Call->getArg(0)->IgnoreImpCasts());
        if (expr)
          CondVar = expr->getDecl();
        CondExpr = Call->getArg(1)->IgnoreImpCasts();
      } break;
      }
    }
    return true;
  }

  return false;
}

static bool IsStreamVar(VarDecl *Var) {
  auto VarType = Var->getType().getCanonicalType();
  if (auto CxxRecordType = VarType->getAsCXXRecordDecl()) {
    // hls::stream can be used as static
    auto Str = CxxRecordType->getCanonicalDecl()->getQualifiedNameAsString();
    if (!Str.compare("hls::stream"))
      return true;
  }
  return false;
}

static bool IsCanonicalDecl(DiagnosticsEngine &Diags, ASTContext &Context,
                            DeclStmt *DStmt) {
  for (auto D : DStmt->getDeclGroup()) {
    if (auto *DS = dyn_cast<VarDecl>(D)) {
      // non-local variable except hls::stream
      if (!DS->hasLocalStorage() && !IsStreamVar(DS)) {
        Diags.Report(DS->getLocation(), diag::warn_dataflow_static_variable);
        return false;
      }
    }
  }
  return true;
}

static Expr *StripImplicitCast(Expr *FE) {
  if (auto *Cast = dyn_cast<ImplicitCastExpr>(FE))
    return StripImplicitCast(Cast->getSubExpr());

  return FE;
}

static Expr *GetConstructorExprArg(CXXConstructExpr *CxxExpr) {
  // work for class pass by value, also strip temporary expr
  if (CxxExpr->getNumArgs() == 1) { // copy or move constructor
    auto *Arg = StripImplicitCast(CxxExpr->getArg(0));
    // strip temporary expr comes from exprwithcleanups
    if (auto TempExpr = dyn_cast_or_null<MaterializeTemporaryExpr>(Arg)) {
      Arg = StripImplicitCast(TempExpr->GetTemporaryExpr());
      if (isa<CXXConstructExpr>(Arg))
        Arg = GetConstructorExprArg(cast<CXXConstructExpr>(Arg));
    }
    return Arg;
  }
  return nullptr;
}

static bool IsCanonicalCallArg(DiagnosticsEngine &Diags, ASTContext &Context,
                               Expr *Arg,
                               SmallVector<VarDecl *, 4> &LocalDeclSet) {
  // strip *var &var
  if (auto UExpr = dyn_cast<UnaryOperator>(Arg)) {
    auto OpStr = UExpr->getOpcodeStr(UExpr->getOpcode());
    if (!OpStr.compare("*") || !OpStr.compare("&"))
      Arg = UExpr->getSubExpr();
  }

  if (auto *CxxExpr = dyn_cast_or_null<CXXConstructExpr>(Arg)) {
    // Get first argument of copy/move construcor
    Arg = GetConstructorExprArg(CxxExpr);
  } else {
    // strip ArraySubscript and class Member
    while (isa<ArraySubscriptExpr>(Arg) || isa<MemberExpr>(Arg)) {
      if (isa<ArraySubscriptExpr>(Arg))
        Arg = cast<ArraySubscriptExpr>(Arg)->getBase()->IgnoreImpCasts();
      if (isa<MemberExpr>(Arg))
        Arg = cast<MemberExpr>(Arg)->getBase()->IgnoreImpCasts();
    }
  }

  if (!Arg)
    return false;
  // Literal expr
  if (isa<IntegerLiteral>(Arg) || isa<StringLiteral>(Arg))
    return true;

  if (Arg->getType()->isArithmeticType() && Arg->isEvaluatable(Context))
    // expression can be constant folded
    return true;

  // check as DeclRefExpr
  auto Declref = dyn_cast<DeclRefExpr>(Arg);
  if (!Declref)
    return false;
  auto LDecl = dyn_cast<VarDecl>(Declref->getDecl());
  if (!LDecl)
    return false;
  if (llvm::none_of(LocalDeclSet,
                    [LDecl](ValueDecl *V) { return LDecl == V; }) &&
      !isa<ParmVarDecl>(LDecl))
    return false;
  return true;
}

template <typename T> static bool CheckAssignmentWithCall(T *St) {
  if (auto *Bin = dyn_cast_or_null<BinaryOperator>(St)) {
    if (!Bin->isAssignmentOp())
      return false;

    auto *E = StripImplicitCast(Bin->getRHS());
    return isa<CallExpr>(E);
  }

  if (auto *Cop = dyn_cast_or_null<CXXOperatorCallExpr>(St)) {
    if (!Cop->isAssignmentOp())
      return false;

    auto *E = StripImplicitCast(Cop->getArg(0));
    return isa<CallExpr>(E);
  }

  if (auto *CleanUp = dyn_cast_or_null<ExprWithCleanups>(St))
    return CheckAssignmentWithCall(CleanUp->getSubExpr());

  return false;
}

template <typename T> static CallExpr *GetCallExpr(T *St) {
  if (auto *CallE = dyn_cast_or_null<CallExpr>(St))
    return CallE;

  if (auto *CleanUp = dyn_cast_or_null<ExprWithCleanups>(St))
    return GetCallExpr(CleanUp->getSubExpr());

  return nullptr;
}

static bool IsCanonicalStmt(DiagnosticsEngine &Diags, ASTContext &Context,
                            Stmt *&St, SmallVector<VarDecl *, 4> &LocalDeclSet,
                            SmallVectorImpl<Stmt *> &InvalidStmts,
                            unsigned &InvalidStCounter,
                            unsigned INVALIDSTCOUNTERTHRESHOLD) {
  if (auto *DStmt = dyn_cast_or_null<DeclStmt>(St)) {
    if (!IsCanonicalDecl(Diags, Context, DStmt))
      return false;
  } else if (CheckAssignmentWithCall(St)) {
    // for callexpr in assignment, do not support
    Diags.Report(St->getLocStart(),
                 diag::warn_ignore_xcl_dataflow_on_invalid_call_return_type);
    return false;
  } else if (auto *CExpr = GetCallExpr(St)) {
    // skip from varargs such as printf
    auto CalleeFn = dyn_cast_or_null<FunctionDecl>(CExpr->getCalleeDecl());
    if (CalleeFn && CalleeFn->isVariadic())
      return true;
    bool AllArgsInCanonicalForm = true;
    for (auto *Arg : CExpr->arguments())
      if (!IsCanonicalCallArg(Diags, Context, Arg->IgnoreImpCasts(),
                              LocalDeclSet)) {
        Diags.Report(
            Arg->getLocStart(),
            diag::warn_ignore_xcl_dataflow_on_invalid_call_argument_type);
        AllArgsInCanonicalForm = false;
      }
    if (!AllArgsInCanonicalForm)
      return false;
  } else if (auto *DStmt = dyn_cast_or_null<ReturnStmt>(St)) {
    // FIXME check return value?
    return true;
  } else if (isa<NullStmt>(St)) {
    return true;
  } else {
    // do not support other stmt now
    if (InvalidStCounter++ < INVALIDSTCOUNTERTHRESHOLD)
      InvalidStmts.push_back(St);
    return false;
  }
  return true;
}

static bool CheckDataflowRegion(DiagnosticsEngine &Diags, ASTContext &Context,
                                CompoundStmt *compoundStmt,
                                SmallVector<VarDecl *, 4> &LocalDeclSet) {

  ArrayRef<Stmt *> Stmts(compoundStmt->body_begin(), compoundStmt->body_end());
  for (auto *St : Stmts) {
    if (isa<DeclStmt>(St)) {
      DeclStmt *decl_stmt = dyn_cast<DeclStmt>(St);
      DeclGroupRef decl_group = decl_stmt->getDeclGroup();
      for (auto decl : decl_group) {
        VarDecl *var_decl = dyn_cast_or_null<VarDecl>(decl);
        LocalDeclSet.push_back(var_decl);
      }
    }
  }

  bool IsCanonical = true;
  unsigned InvalidStCounter = 0;
  const unsigned INVALIDSTCOUNTERTHRESHOLD = 3;
  SmallVector<Stmt *, 3> InvalidStmts;
  for (auto *St : Stmts)
    IsCanonical &=
        IsCanonicalStmt(Diags, Context, St, LocalDeclSet, InvalidStmts,
                        InvalidStCounter, INVALIDSTCOUNTERTHRESHOLD);

  // Error out the invalid statements in dataflow region
  for (auto *St : InvalidStmts) {
    Diags.Report(St->getLocStart(),
                 diag::warn_ignore_xcl_dataflow_on_invalid_statement);
  }
  // Dump the statistics
  if (InvalidStCounter > INVALIDSTCOUNTERTHRESHOLD)
    Diags.Report(Stmts[0]->getLocStart(),
                 diag::warn_statistics_xcl_dataflow_on_invalid_statement)
        << unsigned(InvalidStCounter);

  return IsCanonical;
}

static bool CheckDataflowLoop(DiagnosticsEngine &Diags, ASTContext &Context,
                              ForStmt *for_stmt) {
  Stmt *First = for_stmt->getInit();
  Expr *Second = for_stmt->getCond();
  Expr *Third = for_stmt->getInc();
  Stmt *Body = for_stmt->getBody();

  auto *LoopCompound = dyn_cast_or_null<CompoundStmt>(Body);
  if (!LoopCompound)
    return false;
  // check INIT COND_VAR, COND, INC
  if (First && Second && Third) {
    bool LoopIncrement = false;
    DeclRefExpr *LoopDRE = nullptr;
    Decl *CondVar = nullptr;
    Expr *CondExpr = nullptr;
    ParseLoopConditionalStatement(Second, CondVar, CondExpr);

    if (ProcessIterationStmt(Diags, Context, Third, LoopIncrement, LoopDRE) &&
        CheckLoopIterationVariable(Diags, Context, First, CondVar,
                                   LoopDRE->getDecl()) &&
        CheckLoopConditionStmt(Diags, Context, CondExpr))
      return true;
  } else {
    if (!First)
      Diags.Report(LoopCompound->getLBracLoc(),
                   diag::warn_ignore_xcl_dataflow_on_invalid_loop_initial_stmt);
    else if (!Second)
      Diags.Report(
          LoopCompound->getLBracLoc(),
          diag::warn_ignore_xcl_dataflow_on_invalid_loop_condition_stmt);
    else if (!Third)
      Diags.Report(
          LoopCompound->getLBracLoc(),
          diag::warn_ignore_xcl_dataflow_on_invalid_loop_iteration_stmt);
    return false;
  }
  return false;
}

//========================== finish dataflow lawyer checker
//========================//
void GenerateDataFlowProc(CompoundStmt *dataflow_region, ASTContext &Context) {
  for (auto &iter : dataflow_region->body()) {
    if (isa<ReturnStmt>(iter))
      continue;

    if (isa<NullStmt>(iter)) {
      continue;
    }
    // is HLS stmt
    if (isa<AttributedStmt>(iter)) {
      auto sub_stmt = dyn_cast<AttributedStmt>(iter)->getSubStmt();
      if (isa<NullStmt>(sub_stmt))
        continue;
    }

    StringRef Name = "proc";
    if (auto *LS = dyn_cast<LabelStmt>(iter))
      Name = LS->getName();

    auto *outline =
        XCLOutlineAttr::CreateImplicit(Context, Name, iter->getSourceRange());
    SmallVector<Attr *, 4> attrs;
    attrs.push_back(outline);
    if (isa<AttributedStmt>(iter)) {
      AttributedStmt *attrStmt = dyn_cast<AttributedStmt>(iter);
      ArrayRef<const Attr *> old_attrs = attrStmt->getAttrs();
      llvm::SmallVector<const Attr *, 4> new_attrs;
      new_attrs.push_back(outline);
      new_attrs.append(old_attrs.begin(), old_attrs.end());

      iter = AttributedStmt::Create(Context, attrs[0]->getLocation(), new_attrs,
                                    attrStmt->getSubStmt());
    } else {
      iter =
          AttributedStmt::Create(Context, attrs[0]->getLocation(), attrs, iter);
    }
  }
}

static bool IsHLSStreamType(Expr *expr) {
  if (!expr)
    return false;
  auto Ty = expr->getType().getCanonicalType();
  if (isa<DeclRefExpr>(expr)) {
    ValueDecl *decl = dyn_cast<DeclRefExpr>(expr)->getDecl();
    if (isa<ParmVarDecl>(decl)) {
      Ty = dyn_cast<ParmVarDecl>(decl)->getOriginalType();
    }
  }

  auto *BTy = Ty->isReferenceType() ? Ty->getPointeeType().getTypePtr()
                                    : Ty->getPointeeOrArrayElementType();

  if (BTy->isClassType() && !BTy->getAsCXXRecordDecl()
                                 ->getCanonicalDecl()
                                 ->getQualifiedNameAsString()
                                 .compare("hls::stream"))
    return true;

  // hls::stream<type> with template in type
  if (dyn_cast<TemplateSpecializationType>(BTy)) {
    TemplateName name =
        dyn_cast<TemplateSpecializationType>(BTy)->getTemplateName();
    if (TemplateDecl *decl = name.getAsTemplateDecl()) {
      std::string base_name = decl->getQualifiedNameAsString();
      if (base_name == "hls::stream")
        return true;
    }
  }
  return false;
}

static bool IsStreamable(Expr *expr) {
  if (!expr)
    return false;

  auto Ty = expr->getType();
  if (isa<DeclRefExpr>(expr)) {
    ValueDecl *decl = dyn_cast<DeclRefExpr>(expr)->getDecl();
    if (isa<ParmVarDecl>(decl)) {
      Ty = dyn_cast<ParmVarDecl>(decl)->getOriginalType();
    }
  }

  return Ty->isArrayType();
}

static const Type *getOriginalType(Expr *expr) {
  const Type *type = expr->getType().getTypePtr();
  if (isa<DeclRefExpr>(expr)) {
    ValueDecl *decl = dyn_cast<DeclRefExpr>(expr)->getDecl();
    if (isa<ParmVarDecl>(decl)) {
      type = dyn_cast<ParmVarDecl>(decl)->getOriginalType().getTypePtr();
    }
  }
  return type;
}

static Stmt *hoistXlxAttrs(
    SmallVector<Stmt *, 8> &parents,
    llvm::DenseMap<Stmt *, SmallVector<const Attr *, 4>> &hoistedAttrs,
    Sema &S) {
  DiagnosticsEngine &Diags = S.getDiagnostics();
  ASTContext &context = S.getASTContext();

  Stmt *stmt = parents.back();
  int parent_size = parents.size() - 1;
  switch (stmt->getStmtClass()) {
  case Stmt::AttributedStmtClass: {
    AttributedStmt *attributedStmt = dyn_cast<AttributedStmt>(stmt);
    Stmt *subStmt = attributedStmt->getSubStmt();

    parents.push_back(subStmt);
    subStmt = hoistXlxAttrs(parents, hoistedAttrs, S);
    attributedStmt->setSubStmt(subStmt);
    parents.pop_back();

    if (!isa<NullStmt>(subStmt)) {
      // it must be stupid XCL __attribute__ before statement
      break;
    }

    // return transformed attributed stmt
    SmallVector<const Attr *, 4> left;

    // TODO, if it is possible or not that body is attributed statment ?
    assert(parent_size != 0 && "unexpected, function body is attributed stmt");
    auto attrs = attributedStmt->getAttrs();
    for (auto attr : attrs) {
      if (isa<XCLLatencyAttr>(attr)) {
        //**********  pattern match***************/
        //
        // for loop latency:
        // current attributed_stmt , parent_size = N
        // compound :  parent_size = N - 1
        // attributed_stmt: parent_size = N - 2
        //....[attributed_stmt]: ....
        // forStmt: parent_size = N - n
        //
        //
        // region latency
        // current attributed_stmt, parent_size = N
        // compound_stmt : parent_size N - 1
        //....[attributed_stmt]
        // labeled_stmt: parent_size N - n
        //
        //
        // function latency
        // current attributed_stmt, parent_size = N
        //....[attributed_stmt]
        // body_stmt: parent_size N - N
        //
        //
        int off = 0;
        for (off = 1; off < parent_size; off++) {
          if (!isa<AttributedStmt>(parents[parent_size - off])) {
            break;
          }
        }
        if (off == parent_size) {
          // it is function latency
          hoistedAttrs[nullptr].push_back(attr);
        } else {
          // it is latency attribute in for loop
          hoistedAttrs[parents[parent_size - off]].push_back(attr);
        }
      } else if (isa<XlxPipelineAttr>(attr)) {
        Stmt *parent_1 = parents[parent_size - 1];
        // XlxPipeline apply in fuction body, if parent_2 is nullptr
        Stmt *parent_2 = nullptr;
        if (parent_size >= 2) {
          parent_2 = parents[parent_size - 2];
        }

        if (!isa<CompoundStmt>(parent_1)) {
          llvm_unreachable(
              "unexpected, HLS attributed stmt must be in CompoundStmt");
        }

        const XlxPipelineAttr *pipeline_attr = dyn_cast<XlxPipelineAttr>(attr);
        if (parent_2 && (isa<ForStmt>(parent_2) || isa<WhileStmt>(parent_2) ||
                         isa<DoStmt>(parent_2)|| isa<CXXForRangeStmt>(parent_2))) {
          // hoist  pipeline to loop
          hoistedAttrs[parent_2].push_back(attr);
        } else if (parent_2) {
          // parent_2 is not do/while/for stmt, it is not valid
          Diags.Report(attr->getRange().getBegin(),
                       diag::warn_xlx_attr_wrong_stmt_target)
              << "pipeline "
              << "for/while/do" << FixItHint::CreateRemoval(attr->getRange());
          // during emit stmt,  stmt's XlxPiepline would be ignored
        } else if (!parent_2) {
          // llvm::dbgs() << "hoist pipeline attr to function decl\n";
          // hoistedAttrs[nullptr] meanse , the attribute will hoisted to
          // function decl
          hoistedAttrs[nullptr].push_back(attr);
          // just warning out here, later,  during Emit llvm ir,  unsupported
          // action will be ignored
          if (pipeline_attr->getRewind())
            Diags.Report(attr->getLocation(),
                         diag::warn_pragma_unsupported_action)
                << "HLS Pipeline"
                << "rewind";
        } else {
          llvm_unreachable("unexpected xlx_pipeline attribute , can not hoist");
        }
      } else if (isa<XCLDataFlowAttr>(attr)) {
        Stmt *parent_1 = parents[parent_size - 1];
        // XCLDataflow apply in fuction body, if parent_2 is nullptr
        Stmt *parent_2 = nullptr;
        if (parent_size >= 2) {
          parent_2 = parents[parent_size - 2];
        }

        if (parent_2) {
          if (isa<ForStmt>(parent_2)) {
            assert(
                isa<CompoundStmt>(parent_1) &&
                "unexpected , data flow pragma doesn't locate in CompoundStmt");
            hoistedAttrs[parent_2].push_back(attr);
          } else {
            Diags.Report(attr->getRange().getBegin(),
                         diag::warn_xcl_dataflow_attr_wrong_target)
                << FixItHint::CreateRemoval(attr->getRange());
          }
        } else if (!parent_2) {
          hoistedAttrs[nullptr].push_back(attr);
        }
      } else if (isa<XlxReqdPipeDepthAttr>(attr)) {
        auto pipeDepth = cast<XlxReqdPipeDepthAttr>(attr);
        Expr *expr = pipeDepth->getVariable();
        if (!IsHLSStreamType(expr) && !IsStreamable(expr)) {
          Diags.Report(expr->getLocStart(), diag::warn_implicit_hls_stream)
              << "stream";
          continue;
        }
        //check  depth 
        if (!EvaluateInteger(S, pipeDepth->getDepth(), 1, "STREAM", pipeDepth->getLocation(), -1, INT32_MAX)) { 
          continue;
        }
        left.push_back(attr);
      } else if (isa<XlxArrayXFormAttr>(attr)) {
        // check variable expression, if function parameter decl , skip it
        bool isParam = false;
        auto xform = dyn_cast<XlxArrayXFormAttr>(attr);
        Expr *var_expr = xform->getVariable();
        if (getOriginalType(var_expr)->isScalarType()) {
          Diags.Report(var_expr->getLocStart(),
                       diag::warn_xlx_attribute_ignore_because_invalid_option)
              << "Array_Partition/Array_Reshape"
              << "variable is scalar type";
          continue;
        }
        while (isa<MemberExpr>(var_expr)) {
          auto sub = dyn_cast<MemberExpr>(var_expr);
          var_expr = sub->getBase();
        }

        // it is also possible "ThisPtrExpr"
        // folllowing check is only used to check ParamDecl ,
        // array_partition/reshape doens't support applying on "function
        // Parameter"
        if (isa<DeclRefExpr>(var_expr)) {
          auto refExpr = dyn_cast<DeclRefExpr>(var_expr);
          Decl *decl = refExpr->getDecl();
          if (!decl->isUsed(false)) {
            // skip unused , there are some stupid test case , variable = unused
            // variable
            continue;
          }
          // check paramVarDecl , warning out
          if (isa<ParmVarDecl>(decl)) {
            isParam = true;
          }
        }
        if (isParam) {
          // In fact, partition on function argument is unsupported.
          // Here we still generate call intrinsic for partition.
          // Because clang is very hard to do complex checking.
          // We will leave the correct actions to PramgaPreprocess:
          left.push_back(attr);
        } else {
          // don't do hoist, just emit intrinsic for HLS-Stmt in codegen
          left.push_back(attr);
        }
      } else if (isa<FPGAResourceLimitHintAttr>(attr)) {
        Stmt *parent_1 = parents[parent_size - 1];
        Stmt *parent_2 = nullptr;
        if (parent_size >= 2) {
          parent_2 = parents[parent_size - 2];
        }

        auto resourceLimit = dyn_cast<FPGAResourceLimitHintAttr>(attr);
        Expr *Limit = resourceLimit->getLimit();
        llvm::APSInt Int(32);
        auto ICE = S.HLSVerifyIntegerConstantExpression(Limit, &Int);
        if (ICE.isInvalid()) {
          Diags.Report(Limit->getLocStart(),
                       diag::warn_xlx_attribute_ignore_because_invalid_option)
              << "Allocation"
              << "'limit' is not const integer" << attr->getRange();
          continue;
        }
        if (parent_2) { 
          hoistedAttrs[parent_1].push_back(attr);
        }
        else { 
          left.push_back(attr);
        }
      } else if (isa<XlxFunctionAllocationAttr>(attr)) {
        Stmt *parent_1 = parents[parent_size - 1];
        // XCLDataflow apply in fuction body, if parent_2 is nullptr
        Stmt *parent_2 = nullptr;
        if (parent_size >= 2) {
          parent_2 = parents[parent_size - 2];
        }

        auto functionAlloc = dyn_cast<XlxFunctionAllocationAttr>(attr);
        auto Limit = functionAlloc->getLimit();
        llvm::APSInt Int(32);
        auto ICE = S.HLSVerifyIntegerConstantExpression(Limit, &Int);
        if (ICE.isInvalid()) {
          Diags.Report(Limit->getLocStart(),
                       diag::warn_xlx_attribute_ignore_because_invalid_option)
              << "Allocation"
              << "'limit' is not const integer" << attr->getRange();
          continue;
        }
        if (parent_2) { 
          hoistedAttrs[parent_1].push_back(attr);
        }
        else { 
          left.push_back(attr);
        }
      } else if (isa<XlxLoopTripCountAttr>(attr)) {
        // Check the semantic of the tripcount attribute.
        // If AvgExpr is nullptr, we will create the AvgExpr during the check.
        const XlxLoopTripCountAttr *trip_count =
            dyn_cast<XlxLoopTripCountAttr>(attr);
        Expr *MinExpr = trip_count->getMin();
        Expr *MaxExpr = trip_count->getMax();
        Expr *AvgExpr = trip_count->getAvg();

        Stmt *parent_2 = parents[parent_size - 2];

        // during Xlxhoist, template
        if (!S.CheckXCLLoopTripCountExprs(MinExpr, MaxExpr, AvgExpr,
                                          trip_count->getLocation(),
                                          "Loop_TripCount")) {
          Diags.Report(attr->getRange().getBegin(),
                       diag::warn_xlx_loop_tripcount_ignored)
              << "because option is invalid";
        } else if (parent_2 &&
                   (isa<ForStmt>(parent_2) || isa<WhileStmt>(parent_2) ||
                    isa<DoStmt>(parent_2))) {
          hoistedAttrs[parent_2].push_back(attr);
        } else {
          Diags.Report(attr->getRange().getBegin(),
                       diag::warn_xcl_loop_attr_wrong_target)
              << "loop_tripcount" << FixItHint::CreateRemoval(attr->getRange());
        }
      } else if (isa<XlxUnrollHintAttr>(attr)) {
        const XlxUnrollHintAttr *unroll_hint =
            dyn_cast<XlxUnrollHintAttr>(attr);
        Stmt *parent_2 = nullptr;
        if (parent_size > 2) {
          parent_2 = parents[parent_size - 2];
        } else {
          Diags.Report(attr->getRange().getBegin(),
                       diag::warn_xcl_loop_attr_wrong_target)
              << "Unroll " << FixItHint::CreateRemoval(attr->getRange());
          continue;
        }

        if (!isa<ForStmt>(parent_2) && !isa<WhileStmt>(parent_2) &&
            !isa<DoStmt>(parent_2)) {
          Diags.Report(attr->getRange().getBegin(),
                       diag::warn_xcl_loop_attr_wrong_target)
              << "Unroll " << FixItHint::CreateRemoval(attr->getRange());
          continue;
        }

        if (!unroll_hint->getIsDefaultFactor()) {

          llvm::APSInt Int(32);

          auto ICE = S.HLSVerifyIntegerConstantExpression(
              unroll_hint->getFactor(), &Int);
          if (ICE.isInvalid()) {
            Diags.Report(attr->getRange().getBegin(),
                         diag::warn_xlx_attribute_ignore_because_invalid_option)
                << "Unroll"
                << "'factor' is not const integer" << attr->getRange();
            continue;
          }

          int32_t Ret = Int.getSExtValue();
          if (Ret <= 0) {
            S.Diag(unroll_hint->getFactor()->getLocStart(),
                   diag::warn_xlx_attribute_ignore_because_invalid_option)
                << "Unroll"
                << "'factor' is not positive integer"
                << unroll_hint->getFactor()->getSourceRange();
            continue;
          }
        }

        hoistedAttrs[parent_2].push_back(attr);
      } 
      else if (isa<XlxBindOpAttr>(attr)) {
        auto bindOp = cast<XlxBindOpAttr>(attr);
        auto op = EvaluateInteger(S, bindOp->getOp(), 0, "bind_op", bindOp->getLocation(), -1, INT32_MAX);
        auto op_enum = (platform::PlatformBasic::OP_TYPE)op.getValue();
        auto impl = EvaluateInteger(S, bindOp->getImpl(), 1, "bind_op", bindOp->getLocation(), -1, INT32_MAX);
        auto impl_enum = (platform::PlatformBasic::IMPL_TYPE)impl.getValue();
        auto latency_v = EvaluateInteger(S, bindOp->getLatency(), 2, "bind_op", bindOp->getLocation(), -1, INT32_MAX);
        if (!latency_v.hasValue()) { 
          //EvaluateInteger report waring message already
          continue;
        }
        auto latency = latency_v.getValue();


        const platform::PlatformBasic *XilinxPlatform =
            platform::PlatformBasic::getInstance();
        auto range = XilinxPlatform->verifyLatency(op_enum, impl_enum);
        if (latency != -1 &&
            (latency < range.first || latency > range.second)) {
          S.Diag(attr->getLocation(),
                                diag::err_latency_value_is_out_of_range)
              << (int)latency
              << "[" + std::to_string(range.first) + ", " +
                     std::to_string(range.second) + "]";
          continue;
        }

        Stmt *parent_1 = parents[parent_size - 1];
        if (parent_size > 1) { 
          hoistedAttrs[parent_1].push_back(attr);
        }
        else { 
          hoistedAttrs[nullptr].push_back(attr);
        }
      }
      else {
        // some attribute doesn't need hoist
        left.push_back(attr);
      }
    }

    if (!left.size()) {
      // subStmt is NullStmt;
      return subStmt;
    } else {
      return AttributedStmt::Create(context, left[0]->getLocation(), left,
                                    subStmt);
    }
    break;
  }
  case Stmt::NullStmtClass: {
    break;
  }
#define STMT(Type, Base)
#define ABSTRACT_STMT(Op)
#define EXPR(Type, Base) case Stmt::Type##Class:
#include "clang/AST/StmtNodes.inc"
    { break; }
  case Stmt::CompoundStmtClass: {
    auto *compound = dyn_cast<CompoundStmt>(stmt);
    for (auto &iter : compound->body()) {
      parents.push_back(iter);
      auto subStmt = hoistXlxAttrs(parents, hoistedAttrs, S);
      iter = subStmt;
      parents.pop_back();
    }
    break;
  }
  case Stmt::DeclStmtClass: {
    SmallVector<const Attr *, 4> attrs;
    auto *declStmt = dyn_cast<DeclStmt>(stmt);
    auto declGroup = declStmt->getDeclGroup();
    for (auto decl = declGroup.begin(); decl != declGroup.end(); decl++) {
      // llvm::dbgs() << " TODO, handle decl stmt \n" ;
    }
    break;
  }
  case Stmt::LabelStmtClass: {
    LabelStmt *labelStmt = dyn_cast<LabelStmt>(stmt);
    Stmt *subStmt = labelStmt->getSubStmt();
    parents.push_back(subStmt);
    subStmt = hoistXlxAttrs(parents, hoistedAttrs, S);
    labelStmt->setSubStmt(subStmt);
    parents.pop_back();
    break;
  }
  case Stmt::DefaultStmtClass: {
    auto caseStmt = dyn_cast<DefaultStmt>(stmt);
    auto subStmt = caseStmt->getSubStmt();
    parents.push_back(subStmt);
    subStmt = hoistXlxAttrs(parents, hoistedAttrs, S);
    caseStmt->setSubStmt(subStmt);
    parents.pop_back();
    break;
  }
  case Stmt::CaseStmtClass: {
    auto caseStmt = dyn_cast<CaseStmt>(stmt);
    auto subStmt = caseStmt->getSubStmt();
    parents.push_back(subStmt);
    subStmt = hoistXlxAttrs(parents, hoistedAttrs, S);
    caseStmt->setSubStmt(subStmt);
    parents.pop_back();
    break;
  }
  case Stmt::IfStmtClass: {
    auto ifStmt = dyn_cast<IfStmt>(stmt);
    auto thenStmt = ifStmt->getThen();
    auto elseStmt = ifStmt->getElse();
    if (thenStmt) {
      parents.push_back(thenStmt);
      thenStmt = hoistXlxAttrs(parents, hoistedAttrs, S);
      ifStmt->setThen(thenStmt);
      parents.pop_back();
    }

    if (elseStmt) {
      parents.push_back(elseStmt);
      elseStmt = hoistXlxAttrs(parents, hoistedAttrs, S);
      ifStmt->setElse(elseStmt);
      parents.pop_back();
    }
    break;
  }
  case Stmt::WhileStmtClass: {
    auto whileStmt = dyn_cast<WhileStmt>(stmt);
    auto body = whileStmt->getBody();
    parents.push_back(body);
    body = hoistXlxAttrs(parents, hoistedAttrs, S);
    whileStmt->setBody(body);
    parents.pop_back();
    break;
  }
  case Stmt::DoStmtClass: {
    auto doStmt = dyn_cast<DoStmt>(stmt);
    auto body = doStmt->getBody();
    parents.push_back(body);
    body = hoistXlxAttrs(parents, hoistedAttrs, S);
    doStmt->setBody(body);
    parents.pop_back();
    break;
  }
  case Stmt::ForStmtClass: {
    auto forStmt = dyn_cast<ForStmt>(stmt);
    Stmt *body = forStmt->getBody();
    parents.push_back(body);
    body = hoistXlxAttrs(parents, hoistedAttrs, S);
    forStmt->setBody(body);
    parents.pop_back();
    break;
  }
  case Stmt::CXXForRangeStmtClass: { 
    auto forStmt = dyn_cast<CXXForRangeStmt>(stmt);
    Stmt *body = forStmt->getBody();
    parents.push_back(body);
    body = hoistXlxAttrs(parents, hoistedAttrs, S);
    forStmt->setBody(body);
    parents.pop_back();
    break;
  }
  case Stmt::SwitchStmtClass : {
    auto switchStmt = dyn_cast<SwitchStmt>(stmt);
    auto body = switchStmt->getBody();
    parents.push_back(body);
    body = hoistXlxAttrs(parents, hoistedAttrs, S);
    switchStmt->setBody(body);
    parents.pop_back();
    break;
  }
  default:
    break;
  }
  // EmitXlxXXXAttribute can not supporte attributeStmt nesting attributeStmt,
  // so , move attribute to parent
  if (hoistedAttrs.count(stmt)) {
    // llvm::dbgs() << "hoisted attr to stmt\n";
    llvm::SmallVector<const Attr *, 4> new_attrs;

    // if parent is attributedStmt, add attribute to the parent
    if (parent_size >= 1 && isa<AttributedStmt>(parents[parent_size - 1])) {
      // llvm::dbgs()<< "hoist Attr to parent which is attributed stmt\n";
      // parents[parent_size -1]->dump();
      hoistedAttrs[parents[parent_size - 1]].append(hoistedAttrs[stmt].begin(),
                                                    hoistedAttrs[stmt].end());
      hoistedAttrs.erase(stmt);
      // here, would no produce new_attrs, and skip the following procedure
      // which apply new_attrs to stmt
    } else if (isa<AttributedStmt>(stmt)) {
      // llvm::dbgs() << "AttributeStmt with hoisted attr\n";
      auto attrStmt = dyn_cast<AttributedStmt>(stmt);
      ArrayRef<const Attr *> old_attrs = attrStmt->getAttrs();
      new_attrs = hoistedAttrs[stmt];
      new_attrs.append(old_attrs.begin(), old_attrs.end());

      stmt = attrStmt->getSubStmt();
    } else {
      new_attrs = hoistedAttrs[stmt];
    }
    //HLS pragma can not apply on "SwitchStmt" after hoist, check it and error out
    if (isa<SwitchStmt>(parents[parent_size - 1])) { 
      //warning out and ignore the pragma  for following case: 
      /*
      switch( cond) { 
       case 1: 
         ... 
         #pramga HLS latency/resource/allocation ....
         ...
         break;
       case ...: 
        ....
      }
      */
      for (auto attr: new_attrs) { 
        S.Diag(attr->getLocation(),diag::warn_xlx_attribute_ignore_because_invalid_option) 
          << attr->getSpelling()
          << "the pragma can not be applied on \"SWitch Statement\"";
      }
      return stmt;
    }

    // get all attributes which will apply on stmt
    // stmt is no attributedStmt
    if (new_attrs.size() > 0) {

      for (unsigned long i = 0; i < new_attrs.size(); i++) {
        S.CheckForIncompatibleXlxAttributes(new_attrs);
        if (isa<XCLDataFlowAttr>(new_attrs[i])) {
          // it must be apply on for loop
          // because , if it apply function,  current "stmt" is null ,
          auto *for_stmt = dyn_cast<ForStmt>(stmt);
          if (context.getLangOpts().StrictDataflow) {
            // check whether loop init, cond, increment are canonincal or not
            CheckDataflowLoop(Diags, context, for_stmt);

            SmallVector<VarDecl *, 4> local_decls;
            Stmt *init = for_stmt->getInit();
            DeclStmt *init_decl = dyn_cast_or_null<DeclStmt>(init);
            if (init_decl) {
              DeclGroupRef decl_group = init_decl->getDeclGroup();
              for (auto decl : decl_group) {
                if (isa<VarDecl>(decl)) {
                  local_decls.push_back(dyn_cast<VarDecl>(decl));
                }
              }
            }
            // check stmts in body whether are  var decl and  function call
            CheckDataflowRegion(Diags, context,
                                dyn_cast<CompoundStmt>(for_stmt->getBody()),
                                local_decls);
          }
          GenerateDataFlowProc(dyn_cast<CompoundStmt>(for_stmt->getBody()),
                               context);
        }
      }

      stmt =
          AttributedStmt::Create(context, stmt->getLocStart(), new_attrs, stmt);
    }
  }
  return stmt;
}

static void doHoistXlxScope(FunctionDecl *funcDecl, Sema &S) {
  DiagnosticsEngine &Diags = S.getDiagnostics();
  ASTContext &context = S.getASTContext();
  // llvm::dbgs() << " ============================= do Hoist on Function
  // ==============\n"; funcDecl->dump(); llvm::dbgs() <<
  // "\n===============================================================\n";
  // HoistedAttrs is used to record target stmt where that the attribute should
  // be attached hoistedAttrs[ target_stmt] = attr; hoistedAttrs[nullptr] meanse
  // , the attribute will be hoisted out ,and be attached to function decl
  llvm::DenseMap<Stmt *, SmallVector<const Attr *, 4>> hoistedAttrs;
  SmallVector<Stmt *, 8> parents;
  parents.push_back(funcDecl->getBody());
  hoistXlxAttrs(parents, hoistedAttrs, S);
  parents.pop_back();
  if (hoistedAttrs.count(nullptr)) {
    SmallVector<const Attr *, 4> &func_body_attrs = hoistedAttrs[nullptr];
    // some stupid test case add two dataflow in body, check it

    bool find_dataflow = false;
    bool find_pipeline = false;
    bool find_inline = false;
    for (auto s = func_body_attrs.begin(); func_body_attrs.end() != s;) {
      if (isa<XCLDataFlowAttr>(*s)) {
        if (!find_dataflow) {
          find_dataflow = true;
          s++;
        } else {
          s = func_body_attrs.erase(s);
        }
      } else if (isa<XlxPipelineAttr>(*s)) {
        if (!find_pipeline) {
          find_pipeline = true;
          s++;
        } else {
          s = func_body_attrs.erase(s);
        }
      } else if (isa<XlxPipelineAttr>(*s)) {
        if (!find_inline) {
          find_inline = true;
          s++;
        } else {
          s = func_body_attrs.erase(s);
        }
      } else {
        s++;
      }
    }

    for (unsigned long i = 0; i < func_body_attrs.size(); i++) {
      if (isa<XCLDataFlowAttr>(func_body_attrs[i])) {
        if (context.getLangOpts().StrictDataflow) {
          ArrayRef<ParmVarDecl *> parameters = funcDecl->parameters();
          SmallVector<VarDecl *, 4> local_decls(parameters.begin(),
                                                parameters.end());
          if (CheckDataflowRegion(Diags, context,
                                  dyn_cast<CompoundStmt>(funcDecl->getBody()),
                                  local_decls)) {
            GenerateDataFlowProc(dyn_cast<CompoundStmt>(funcDecl->getBody()),
                                 context);
          } else {
            // Dataflow strict check failed, skip XCLDataflow attributed
            continue;
          }
        } else {
          GenerateDataFlowProc(dyn_cast<CompoundStmt>(funcDecl->getBody()),
                               context);
        }
      }
      funcDecl->addAttr(const_cast<Attr *>(func_body_attrs[i]));
    }
  }
}

class XlxAttrHoistConsumer;

void XlxAttrHoistConsumer::HoistXlxScope(Decl *D) {

  // Ignore dependent declarations.
  if (D->getDeclContext() && D->getDeclContext()->isDependentContext())
    return;

  switch (D->getKind()) {
  case Decl::Namespace: {
    DeclContext *DC = cast<DeclContext>(D);
    for (auto *I : DC->decls()) {
      HoistXlxScope(I);
    }
    break;
  }
  case Decl::CXXConversion:
  case Decl::CXXMethod:
  case Decl::Function:
  case Decl::CXXConstructor: {
    // Skip function templates
    if (cast<FunctionDecl>(D)->getDescribedFunctionTemplate() ||
        cast<FunctionDecl>(D)->isLateTemplateParsed()) {
      return;
    }

    FunctionDecl *funcDecl = cast<FunctionDecl>(D);
    if (funcDecl->hasBody()) {
      doHoistXlxScope(funcDecl, *sema_ptr);
    }
    break;
  }
  case Decl::ClassTemplateSpecialization:
  case Decl::CXXRecord: {
    // Emit any static data members, they may be definitions.
    for (auto *I : cast<CXXRecordDecl>(D)->decls()) {
      if (!isa<VarDecl>(I))
        HoistXlxScope(I);
    }
    break;
  }
  case Decl::LinkageSpec: {
    LinkageSpecDecl *decl_context = cast<LinkageSpecDecl>(D);

    for (auto *I : decl_context->decls()) {
      // Unlike other DeclContexts, the contents of an ObjCImplDecl at TU scope
      // are themselves considered "top-level", so EmitTopLevelDecl on an
      // ObjCImplDecl does not recursively visit them. We need to do that in
      // case they're nested inside another construct (LinkageSpecDecl /
      // ExportDecl) that does stop them from being considered "top-level".
      if (auto *OID = dyn_cast<ObjCImplDecl>(I)) {
        for (auto *M : OID->methods())
          HoistXlxScope(M);
      }
      HoistXlxScope(I);
    }
    break;
  }
  default:
    // llvm::dbgs() <<"skip it ..... \n";
    break;
  }
}

ASTConsumer *Sema::BuildXlxHoistConsumer(ASTConsumer &Consumer) {
  if (getLangOpts().HLSExt) {
    // llvm::dbgs() << "================ build Sema Consumer ==============\n";
    // build MultiplexConsumer , do XlxAttribute Hoist immediatly after Parser
    // produce AST
    std::vector<std::unique_ptr<ASTConsumer>> Consumers;
    Consumers.push_back(std::move(llvm::make_unique<XlxAttrHoistConsumer>()));
    Consumers.push_back(std::move(std::unique_ptr<ASTConsumer>(&Consumer)));

    return new MultiplexConsumer(std::move(Consumers));
  } else
    return &Consumer;
}
