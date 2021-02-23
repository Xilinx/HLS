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

#include "RemoveAssertCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {
template <typename T>
StringRef getText(const ast_matchers::MatchFinder::MatchResult &Result,
                  T const &Node) {
  return Lexer::getSourceText(
      CharSourceRange::getTokenRange(Node.getSourceRange()),
      *Result.SourceManager, Result.Context->getLangOpts());
}

void RemoveAssertCheck::registerMatchers(MatchFinder *Finder) {
  // FIXME: Add matchers.
  auto func = functionDecl(hasName("__assert_fail"));
  auto unresolvefunc = unresolvedLookupExpr(
      hasAnyDeclaration(namedDecl(hasName("__assert_fail"))));
  Finder->addMatcher(
      conditionalOperator(hasFalseExpression(callExpr(
                              anyOf(callee(func), callee(unresolvefunc)))))
          .bind("x"),
      this);
}

void RemoveAssertCheck::check(const MatchFinder::MatchResult &Result) {
  // FIXME: Add callback implementation.
  const auto *MatchedExpr = Result.Nodes.getNodeAs<ConditionalOperator>("x");
  const auto *CondExpr = MatchedExpr->getCond();
  const auto *SM = Result.SourceManager;
  auto CondStr = getText(Result, *CondExpr->IgnoreParens());
  std::string BuiltinStr = "__builtin_assume(" + CondStr.str() + ")";
  SourceLocation StartLoc = MatchedExpr->getLocStart();
  SourceLocation EndLoc = MatchedExpr->getLocEnd();
  // since we can handle file after preprocessing, there is no assert macro,
  diag(StartLoc, "assert call is replaced by __builtin_assume")
      << FixItHint::CreateReplacement(SourceRange(StartLoc, EndLoc),
                                      BuiltinStr);
}

} // namespace xilinx
} // namespace tidy
} // namespace clang
