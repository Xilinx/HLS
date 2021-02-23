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

#include "WarnMayneedNoCtorAttributeCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {

static bool IsStdComplexType(const VarDecl *Var) {
  if (!Var)
    return false;

  auto Ty = Var->getType().getCanonicalType();
  auto *BTy = Ty->isReferenceType() ? Ty->getPointeeType().getTypePtr()
                                    : Ty->getPointeeOrArrayElementType();

  if (auto *RD = BTy->getAsCXXRecordDecl())
    if (!RD->getCanonicalDecl()
           ->getQualifiedNameAsString()
           .compare("std::complex"))
    return true;

  return false;
}

void WarnMayneedNoCtorAttributeCheck::registerMatchers(MatchFinder *Finder) {
  // Matcher for applying XCLDataFlow attribute to Function
  auto DataFlowFunc = functionDecl(isDefinition(), hasAttr(attr::XCLDataFlow),
                                    unless(cxxMethodDecl()));
  auto CompStmtInDataFlowFunc = compoundStmt(hasParent(DataFlowFunc));
  Finder->addMatcher(varDecl(hasParent(declStmt(
          anyOf(hasParent(attributedStmt(hasParent(CompStmtInDataFlowFunc))),
                hasParent(CompStmtInDataFlowFunc)))))
          .bind("dfvar"), this);

  // Matcher for applying XCLDataFlow attribute to Loop
  auto DataFlowAttrStmt = attributedStmt(hasAttachedAttr(attr::XCLDataFlow));
  auto DataFlowFor = forStmt(hasParent(DataFlowAttrStmt));
  auto CompStmtInDataFlowFor = compoundStmt(hasParent(DataFlowFor));
  Finder->addMatcher(varDecl(hasParent(declStmt(
          anyOf(hasParent(attributedStmt(hasParent(CompStmtInDataFlowFor))),
                hasParent(CompStmtInDataFlowFor)))))
          .bind("dlvar"), this);
}

void WarnMayneedNoCtorAttributeCheck::check(
    const MatchFinder::MatchResult &Result) {
  const auto *MatchedVarDeclInFunc = Result.Nodes.getNodeAs<VarDecl>("dfvar");
  const auto *MatchedVarDeclInLoop = Result.Nodes.getNodeAs<VarDecl>("dlvar");

  if (MatchedVarDeclInFunc) {
    if (MatchedVarDeclInFunc->hasAttr<NoCtorAttr>())
      return;

    if (!IsStdComplexType(MatchedVarDeclInFunc))
      return;

    diag(MatchedVarDeclInFunc->getLocation(),
         "Implicit constructor of \'%0\' may lead to multiple writers in dataflow. Can inhibit constructor with __attribute__((no_ctor))")
        << MatchedVarDeclInFunc->getName();
  }

  if (MatchedVarDeclInLoop) {
    if (MatchedVarDeclInLoop->hasAttr<NoCtorAttr>())
      return;

    if (!IsStdComplexType(MatchedVarDeclInLoop))
      return;

    diag(MatchedVarDeclInLoop->getLocation(),
         "Implicit constructor of \'%0\' may lead to multiple writers in dataflow. Can inhibit constructor with __attribute__((no_ctor))")
        << MatchedVarDeclInLoop->getName();
  }
}

} // namespace xilinx
} // namespace tidy
} // namespace clang
