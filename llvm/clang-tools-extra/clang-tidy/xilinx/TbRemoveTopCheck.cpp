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

#include "TbRemoveTopCheck.h"
#include "XilinxTidyCommon.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <fstream>
#include <iostream>

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {

TbRemoveTopCheck::TbRemoveTopCheck(StringRef Name, ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context),
      TopFunctionName(Options.get("TopFunctionName", "dut")) {}

void TbRemoveTopCheck::storeOptions(ClangTidyOptions::OptionMap &Options) {
  TbRemoveTopCheck::storeOptions(Options);
  this->Options.store(Options, "TopFunctionName", TopFunctionName);
}

void TbRemoveTopCheck::registerMatchers(MatchFinder *Finder) {

    auto Top =
        functionDecl(allOf(hasName(TopFunctionName), unless(cxxMethodDecl())))
            .bind("top");
    Finder->addMatcher(
        functionDecl(allOf(hasName(TopFunctionName), isDefinition(),
                           unless(cxxMethodDecl())))
            .bind("topdef"),
        this);
    Finder->addMatcher(Top, this);
}

void TbRemoveTopCheck::check(const MatchFinder::MatchResult &Result) {
    const auto *MatchedTopDef = Result.Nodes.getNodeAs<FunctionDecl>("topdef");
    const auto *MatchedTop = Result.Nodes.getNodeAs<FunctionDecl>("top");
    if (MatchedTopDef) {
      insertOldMacroGuard(MatchedTopDef, Result);
    }
    if (MatchedTop) {
      insertOldMacroGuard(MatchedTop, Result);
    }
    
}

void TbRemoveTopCheck::insertOldMacroGuard(
    const clang::FunctionDecl *Top,
    const ast_matchers::MatchFinder::MatchResult &Result) {
  std::string MainStart;
  if (!Top->isExternC()) {
    MainStart = (getLangOpts().CPlusPlus) ? "extern \"C\" {" : "extern ";
  }
  auto Loc = Top->isInExternCContext() ? Top->getExternCContext()->getLocStart()
                                       : Top->getLocStart();
  diag(Loc, "insert warp function declaration")
      << FixItHint::CreateInsertion(Loc, MainStart);
  
  auto LocEnd = findLocationAfterBody(Result,
                                      Top->doesThisDeclarationHaveABody()
                                          ? Top->getBodyRBrace()
                                          : Top->getLocEnd());
  if (Top->doesThisDeclarationHaveABody()) {
    std::string MainEnd = (MainStart[MainStart.size()-1] == '{') ?
                          "*/}\n" :
                          "*/\n";
    MainEnd += dumpLineInfo(LocEnd, Result.SourceManager);
    diag(LocEnd, "insert undef macro")
      << FixItHint::CreateInsertion(LocEnd, MainEnd);

    auto BodyBegin = Top->getBody()->getLocStart();
    diag(BodyBegin, "hide the function body")
      << FixItHint::CreateInsertion(BodyBegin, ";/*");
  } else {
    std::string MainEnd = (MainStart[MainStart.size()-1] == '{') ?
                          "}\n" :
                          "\n";

    MainEnd += dumpLineInfo(LocEnd, Result.SourceManager);
    diag(LocEnd, "insert undef macro")
      << FixItHint::CreateInsertion(LocEnd, MainEnd);
  }
}

SourceLocation
TbRemoveTopCheck::findLocationAfterBody(const MatchFinder::MatchResult &Result,
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
} // namespace xilinx
} // namespace tidy
} // namespace clang
