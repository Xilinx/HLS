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

#include "LoopBraceBracketCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Lex/Lexer.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {

void LoopBraceBracketCheck::registerMatchers(MatchFinder *Finder) {
  if (getLangOpts().OpenCL)
    return;

  // Need to skip loops in function that has been implicitly added by the
  // compiler(eg.implicit defaultcopy constructors).
  auto noCompund = unless(anyOf(has(compoundStmt()),
                                      hasAncestor(functionDecl(isImplicit()))));
  Finder->addMatcher(forStmt(noCompund).bind("for-loop"), this);
  Finder->addMatcher(doStmt(noCompund).bind("do-loop"), this);
  Finder->addMatcher(whileStmt(noCompund).bind("while-loop"), this);
}

SourceLocation LoopBraceBracketCheck::findLocationAfterBody(
    const ast_matchers::MatchFinder::MatchResult &Result,
    SourceLocation BodyEnd) {
  auto AfterBodyEnd =
      Lexer::findLocationAfterToken(BodyEnd, tok::semi, *Result.SourceManager,
                                    Result.Context->getLangOpts(), false);
  if (!AfterBodyEnd.isInvalid())
    return AfterBodyEnd;
  // We get an invalid location if the loop body end in a macro with a ';'
  // Simply insert the '}' at the end of the body
  return Lexer::getLocForEndOfToken(BodyEnd.getLocWithOffset(1), 0,
                                    *Result.SourceManager,
                                    Result.Context->getLangOpts());
}

void LoopBraceBracketCheck::diagLoop(SourceLocation Loc,
                                     SourceLocation LBracLoc,
                                     SourceLocation RBracLoc) {
  // Sometimes clang will synthesize copy constructor with a for-loop
  if (LBracLoc == RBracLoc)
    return;

  diag(Loc, "loop body requires brace bracket")
      << FixItHint::CreateInsertion(LBracLoc, "{")
      << FixItHint::CreateInsertion(RBracLoc, "}");
}

void LoopBraceBracketCheck::check(const MatchFinder::MatchResult &Result) {
  if (const auto *MatchedFor = Result.Nodes.getNodeAs<ForStmt>("for-loop")) {
    auto LBracLoc = Lexer::getLocForEndOfToken(MatchedFor->getRParenLoc(), 0,
                                               *Result.SourceManager,
                                               Result.Context->getLangOpts());
    auto RBracLoc = findLocationAfterBody(Result, MatchedFor->getLocEnd());
    return diagLoop(MatchedFor->getLocStart(), LBracLoc, RBracLoc);
  }

  if (const auto *MatchedDo = Result.Nodes.getNodeAs<DoStmt>("do-loop")) {
    auto DoEndLoc = Lexer::getLocForEndOfToken(MatchedDo->getDoLoc(), 0,
                                               *Result.SourceManager,
                                               Result.Context->getLangOpts());
    return diagLoop(MatchedDo->getLocStart(), DoEndLoc,
                    MatchedDo->getWhileLoc());
  }

  if (const auto *MatchedWhile =
          Result.Nodes.getNodeAs<WhileStmt>("while-loop")) {
    auto LBracLoc = Lexer::findLocationAfterToken(
        MatchedWhile->getCond()->getLocEnd(), tok::r_paren,
        *Result.SourceManager, Result.Context->getLangOpts(), false);
    auto RBracLoc = findLocationAfterBody(Result, MatchedWhile->getLocEnd());

    return diagLoop(MatchedWhile->getLocStart(), LBracLoc, RBracLoc);
  }
}

} // namespace xilinx
} // namespace tidy
} // namespace clang
