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

#include "SsdmIntrinsicsScopeCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/StringExtras.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {

void SsdmIntrinsicsScopeCheck::registerMatchers(MatchFinder *Finder) {
  if (getLangOpts().OpenCL)
    return;

  auto ssdmName = matchesName("^::_ssdm.+");
  auto ssdmIntrinsics = functionDecl(ssdmName).bind("ssdm");
  auto hasSSDMCall = has(callExpr(callee(ssdmIntrinsics)).bind("call"));
  auto atthasLabel = attributedStmt(hasParent(labelStmt()));
  auto hasLabeledParent = hasParent(atthasLabel);
  auto loop = anyOf(forStmt(), doStmt(), whileStmt());
  auto labeledLoop = allOf(loop, hasLabeledParent);
  auto noReturn = unless(hasDescendant(returnStmt()));

  // Match scope without labeled
  Finder->addMatcher(
      compoundStmt(allOf(hasSSDMCall, noReturn,
                         hasParent(stmt(unless(anyOf(labeledLoop, atthasLabel)))
                                       .bind("parent"))))
          .bind("scope"),
      this);

  // Match scope with labeled loop and label scope, need to get the name of
  // label in a different
  // way
  auto atthasLabelAndBind =
      attributedStmt(hasParent(labelStmt().bind("parent")));
  auto hasLabeledParentAndBind = hasParent(atthasLabelAndBind);
  auto labeledLoopAndBind =
      anyOf(forStmt(hasLabeledParentAndBind), doStmt(hasLabeledParentAndBind),
            whileStmt(hasLabeledParentAndBind));
  Finder->addMatcher(
      compoundStmt(
          allOf(hasSSDMCall, noReturn,
                hasParent(stmt(anyOf(labeledLoopAndBind, atthasLabelAndBind)))))
          .bind("scope"),
      this);
}

void SsdmIntrinsicsScopeCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *MatchedScope = Result.Nodes.getNodeAs<CompoundStmt>("scope");
  const auto *MatchedCall = Result.Nodes.getNodeAs<CallExpr>("call");
  const auto *MatchedSSDM = Result.Nodes.getNodeAs<FunctionDecl>("ssdm");

  SmallString<64> RegionName;
  if (const auto *ParentLabel = Result.Nodes.getNodeAs<LabelStmt>("parent"))
    RegionName = ParentLabel->getName();
  else
    RegionName = "hls_label_" + llvm::utostr(++LabelIdx);

  auto LBracEnd = Lexer::getLocForEndOfToken(MatchedScope->getLBracLoc(), 0,
                                             *Result.SourceManager,
                                             Result.Context->getLangOpts());

  SmallString<64> RegionBegin;
  RegionBegin = "_ssdm_RegionBegin(\"";
  RegionBegin += RegionName;
  RegionBegin += "\");";

  SmallString<64> RegionEnd;
  RegionEnd = "_ssdm_RegionEnd(\"";
  RegionEnd += RegionName;
  RegionEnd += "\");";

  diag(MatchedCall->getLocStart(),
       "ssdm intrinsics %0 in scope require region begin/end markers")
      << MatchedSSDM->getName()
      << FixItHint::CreateInsertion(LBracEnd, RegionBegin)
      << FixItHint::CreateInsertion(MatchedScope->getRBracLoc(), RegionEnd);
}

} // namespace xilinx
} // namespace tidy
} // namespace clang
