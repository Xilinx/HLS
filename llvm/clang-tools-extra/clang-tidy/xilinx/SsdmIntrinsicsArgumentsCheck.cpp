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

#include "SsdmIntrinsicsArgumentsCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {

static bool isReferenceType(const Expr *expr) {
  auto exprtype = expr->getType().getCanonicalType();
  if (auto templatetype = exprtype->getAs<TemplateSpecializationType>())
    return true;
  if (exprtype->isStructureOrClassType()) {
    return true;
  }

  return false;
}

void SsdmIntrinsicsArgumentsCheck::registerMatchers(MatchFinder *Finder) {
  auto ssdmName =
      matchesName("^::_ssdm_(DataPack|Spec(Stream|Array.+|Dependence|Stable|"
                  "FuncInstantiation)|op_Spec(Reset|Resource|Interface|Stable))");
  auto ssdmIntrinsics = functionDecl(ssdmName).bind("ssdm");
  // auto noPointer = unless(hasType(isAnyPointer()));
  auto hasSSDMCall = callExpr(callee(ssdmIntrinsics)).bind("call");
  auto argWithSSDMCall = expr(hasParent(hasSSDMCall)).bind("arg0impl");
  Finder->addMatcher(argWithSSDMCall, this);
}

void SsdmIntrinsicsArgumentsCheck::check(
    const MatchFinder::MatchResult &Result) {
  const auto *MatchedCall = Result.Nodes.getNodeAs<CallExpr>("call");
  const auto *MatchedSSDM = Result.Nodes.getNodeAs<FunctionDecl>("ssdm");
  const auto *MatchedImplArg = Result.Nodes.getNodeAs<Expr>("arg0impl");
  const auto *MatchedArg = MatchedImplArg->IgnoreImpCasts();

  auto item = std::find(MatchedCall->arg_begin(), MatchedCall->arg_end(),
                        MatchedImplArg);
  if (item == MatchedCall->arg_end())
    return;

  SourceLocation FirstArgStart;
  // do not care number and string
  if (isa<IntegerLiteral>(MatchedArg) || isa<StringLiteral>(MatchedArg))
    return;
  // if expr can be evaluatd
  if (MatchedArg->isEvaluatable(*Result.Context))
    return;

  // only care about UnaryOperator
  // UnaryOperator for *
  auto UnaryArg = dyn_cast<UnaryOperator>(MatchedArg);
  if (UnaryArg) {
    auto prefix = UnaryArg->getOpcodeStr(UnaryArg->getOpcode());
    if (prefix == "&") {

      auto SubExpr = UnaryArg->getSubExpr();

      // FIXME since disaggr will split struct, we'll keep & for memberexpr
      // which base object is paramvardecl
      // it will be removed after fix disaggr pass in hls see crs963289
      if (auto MemExpr = dyn_cast<MemberExpr>(SubExpr->IgnoreImpCasts())) {
        // get last base object
        auto BaseExpr = MemExpr->getBase();
        while (MemExpr = dyn_cast<MemberExpr>(BaseExpr->IgnoreImpCasts()))
          BaseExpr = MemExpr->getBase();

        auto *DeclRef = dyn_cast<DeclRefExpr>(BaseExpr);
        if (DeclRef && isa<ParmVarDecl>(DeclRef->getDecl()))
          return;
      }

      auto *DeclRef = dyn_cast<DeclRefExpr>(SubExpr->IgnoreImpCasts());
      if (DeclRef && isa<ParmVarDecl>(DeclRef->getDecl())) {
        // check paramvardecl see crs969110
        if (isReferenceType(SubExpr))
          return;
      } else {
        if (!SubExpr->getType()->isPointerType() &&
            !SubExpr->getType()->isArrayType())
          return;
      }
      // remove '&'
      diag(MatchedImplArg->getLocStart(),
           "remove & from ssdm intrinsics %0 first argument")
          << MatchedSSDM->getName()
          << FixItHint::CreateRemoval(CharSourceRange::getCharRange(
                 MatchedArg->getLocStart(),
                 Lexer::getLocForEndOfToken(MatchedArg->getLocStart(), 0,
                                            *Result.SourceManager,
                                            Result.Context->getLangOpts())));
      return;
    }
  }

  return;
}

} // namespace xilinx
} // namespace tidy
} // namespace clang
