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

#include "AggregateOnApintCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "xilinx-aggregate-on-apint"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {

static const Type *GetStrippedTypeOrArrayElementType(QualType Ty) {
  Ty = Ty.getCanonicalType();
  auto *BTy = Ty->isReferenceType() ? Ty->getPointeeType().getTypePtr()
                                    : Ty->getPointeeOrArrayElementType();
  while (BTy->isArrayType()) {
    BTy = BTy->getPointeeOrArrayElementType();
  }

  return BTy;
}

static const Type *GetAsStructureType(const Expr *Var) {

  auto Ty = Var->getType().getCanonicalType();
  auto *BTy = GetStrippedTypeOrArrayElementType(Ty);
  if (BTy->isStructureOrClassType())
    return BTy;
  return nullptr;
}

static const Type *IsSpecificType(const Type *Ty, StringRef Name) {
  if (Ty->isStructureOrClassType() && !Ty->getAsTagDecl()
                                           ->getCanonicalDecl()
                                           ->getQualifiedNameAsString()
                                           .compare(Name))
    return Ty;

  return nullptr;
}

static bool IsAPType(const Type *Ty) {
  if (!Ty)
    return false;

  if (Ty == IsSpecificType(Ty, "ap_int") ||
      Ty == IsSpecificType(Ty, "ap_uint") ||
      Ty == IsSpecificType(Ty, "ap_fixed") ||
      Ty == IsSpecificType(Ty, "ap_ufixed"))
    return true;

  return false;
}

void AggregateOnApintCheck::registerMatchers(MatchFinder *Finder) {

  Finder->addMatcher(
      functionDecl(isDefinition(), hasAttr(attr::SDxKernel),
                   unless(cxxMethodDecl()),
                   has(compoundStmt(forEach(
                       attributedStmt(hasAttachedAttr(attr::XlxArrayXForm))
                           .bind("axform")))))
          .bind("top"),
      this);
  Finder->addMatcher(functionDecl(isDefinition(), hasAttr(attr::SDxKernel),
                                  unless(cxxMethodDecl()))
                         .bind("top"),
                     this);
}

void AggregateOnApintCheck::check(const MatchFinder::MatchResult &Result) {

  const auto *FuncDecl = Result.Nodes.getNodeAs<FunctionDecl>("top");
  const auto *Attr = Result.Nodes.getNodeAs<AttributedStmt>("axform");

  if (!Attr)
    return;

  for (auto *A : Attr->getAttrs()) {
    auto *AXFormAttr = dyn_cast<XlxArrayXFormAttr>(A);
    if (!AXFormAttr)
      return;

    auto *V = dyn_cast<DeclRefExpr>(AXFormAttr->getVariable());
    if (!V)
      return;

    auto *VD = dyn_cast<ParmVarDecl>(V->getDecl());
    if (!VD)
      return;

    std::string Spelling = AXFormAttr->getSpelling();
    if (!Spelling.empty() && Spelling == "xlx_array_reshape" &&
        IsAPType(GetAsStructureType(V))) {
      SourceLocation TopBodyStart = Lexer::getLocForEndOfToken(
          cast<CompoundStmt>(FuncDecl->getBody())->getLBracLoc(), 0,
          *Result.SourceManager, Result.Context->getLangOpts());
      std::string Param = VD->getQualifiedNameAsString();
      std::string Aggr = "\n#pragma HLS aggregate variable = " + Param + "\n";
      diag(V->getLocation(),
           " array reshape on top function variable %0 that is an array of "
           "arbitrary type requires aggregation on the element type")
          << Param << FixItHint::CreateInsertion(TopBodyStart, Aggr);
    }
  }
}

} // namespace xilinx
} // namespace tidy
} // namespace clang
