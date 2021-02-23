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

#include "Tb31ProcessCheck.h"
#include "XilinxTidyCommon.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <fstream>
#include <iostream>

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {

static bool isDefinedInTemplateDecl(const FunctionDecl *Caller) {
  if (Caller->getTemplatedKind() != FunctionDecl::TK_NonTemplate)
    return true;

  if (isa<CXXMethodDecl>(Caller) &&
      cast<CXXMethodDecl>(Caller)->getParent()->getDescribedClassTemplate() !=
          nullptr)
    return true;
  return false;
}

Tb31ProcessCheck::Tb31ProcessCheck(StringRef Name, ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context),
      TopFunctionName(Options.get("TopFunctionName", "dut")),
      KeepTopName(Options.get("KeepTopName", 0)),
      NewFlow(Options.get("NewFlow", 0)), BCSim(Options.get("BCSim", 0)),
      BuildDir(Context->getCurrentBuildDirectory()) {}

void Tb31ProcessCheck::storeOptions(ClangTidyOptions::OptionMap &Options) {
  Tb31ProcessCheck::storeOptions(Options);
  this->Options.store(Options, "TopFunctionName", TopFunctionName);
  this->Options.store(Options, "KeepTopName", KeepTopName);
  this->Options.store(Options, "NewFlow", NewFlow);
  this->Options.store(Options, "BCSim", BCSim);
}

void Tb31ProcessCheck::registerMatchers(MatchFinder *Finder) {
  auto TopModule =
      callExpr(callee(functionDecl(hasName("_ssdm_op_SpecTopModule"))));
  auto TopCond = allOf(
      unless(isStaticStorageClass()),
      anyOf(hasName(TopFunctionName), hasBody(compoundStmt(has(TopModule)))));
  auto Top = TopFunctionName.find("::") == std::string::npos
                 ? functionDecl(unless(cxxMethodDecl()), TopCond).bind("top")
                 : functionDecl(TopCond).bind("top");
  auto TopDef = TopFunctionName.find("::") == std::string::npos
                    ? functionDecl(allOf(unless(cxxMethodDecl()), TopCond,
                                         isDefinition()))
                          .bind("topdef")
                    : functionDecl(TopCond, isDefinition()).bind("topdef");

  Finder->addMatcher(TopDef, this);
  // support format: top()/a=top()/b=top / return top
  Finder->addMatcher(
      expr(anyOf(callExpr(callee(Top)), binaryOperator(hasRHS(ignoringImpCasts(
                                            declRefExpr(to(Top)))))),
           hasAncestor(functionDecl(isDefinition()).bind("caller")))
          .bind("expr"),
      this);
}

void Tb31ProcessCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *MatchedTopDef = Result.Nodes.getNodeAs<FunctionDecl>("topdef");
  const auto *MatchedTop = Result.Nodes.getNodeAs<FunctionDecl>("top");
  const auto *MatchedDecl = Result.Nodes.getNodeAs<FunctionDecl>("caller");
  const auto *MatchedExpr = Result.Nodes.getNodeAs<Expr>("expr");

  // renmae topfunctionname when use pragma
  if (MatchedTopDef)
    TopFunctionName = MatchedTopDef->getName();
  if (MatchedTop)
    TopFunctionName = MatchedTop->getName();
  // filter out function calls in template funciton or template class
  if (MatchedTop && isDefinedInTemplateDecl(MatchedTop))
    return;
  if (MatchedTopDef && isDefinedInTemplateDecl(MatchedTopDef))
    return;
  if (KeepTopName) {
    if (MatchedTopDef)
      dumpTclFile();
    if (MatchedExpr)
      insertGuardForKeepTopName(MatchedExpr, Result);
    if (MatchedDecl)
      insertMainGuard(MatchedDecl, Result);
  } else if (NewFlow && !BCSim) {
    if (MatchedTopDef)
      insertMacroGuard(MatchedTopDef, Result);
    if (MatchedDecl)
      insertMainGuard(MatchedDecl, Result);
  } else {
  
    if (MatchedTopDef)
      insertOldMacroGuard(MatchedTopDef, Result);
    // Declaration
    if (MatchedTop)
      insertOldMacroGuard(MatchedTop, Result);

    if (!BCSim && MatchedDecl)
      insertOldMainDefinition(MatchedDecl, Result);
    if (MatchedExpr) {
      insertOldTopDefinition(MatchedExpr, Result);
    }
  }
}

SourceLocation
Tb31ProcessCheck::findLocationAfterBody(const MatchFinder::MatchResult &Result,
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

void Tb31ProcessCheck::dumpTclFile() {
  auto curFile = this->getCurrentMainFile().str();
  auto dumpFile =
      curFile.substr(0, curFile.find_last_of("/")) + "/tb.status.tcl";
  std::string dumpStr = "set ::AESL_AUTOSIM::gTopFileName " + curFile;
  std::ofstream o(dumpFile);
  o.write(dumpStr.c_str(), dumpStr.size());
  o.close();
}

void Tb31ProcessCheck::insertGuardForKeepTopName(
    const Expr *Mexpr, const MatchFinder::MatchResult &Result) {
  std::string GuardMacro = "\n#ifndef HLS_FASTSIM\n";
  GuardMacro += "#define " + TopFunctionName + " " + "AESL_WRAP_" +
                TopFunctionName + "\n";
  GuardMacro += "#endif\n";
  GuardMacro += dumpLineInfo(Mexpr->getExprLoc(), Result.SourceManager);
  diag(Mexpr->getLocStart(), "insert top function call guard macro")
      << FixItHint::CreateInsertion(Mexpr->getLocStart(), GuardMacro);

  std::string Undef = "\n#undef " + TopFunctionName + "\n";
  Undef += dumpLineInfo(Mexpr->getExprLoc(), Result.SourceManager);
  auto LocEnd = findLocationAfterBody(Result, Mexpr->getLocEnd());
  diag(Mexpr->getLocEnd(), "insert undef macro")
      << FixItHint::CreateInsertion(LocEnd, Undef);
}

void Tb31ProcessCheck::insertMainGuard(
    const clang::FunctionDecl *Caller,
    const ast_matchers::MatchFinder::MatchResult &Result) {

  std::string MainStart = "#ifndef HLS_FASTSIM\n";
  MainStart += "#ifndef HLS_FASTSIM\n#include \"apatb_" + TopFunctionName +
               +".h\"\n#endif\n";
  const Decl *decl = Caller;
  auto declParent = Caller->getParent();
  // strip to function/class
  while (!declParent->isTranslationUnit()) {
    decl = cast<Decl>(declParent);
    declParent = declParent->getParent();
  }

  if (isa<FunctionDecl>(decl)) {
    auto Tempdecl = cast<FunctionDecl>(decl)->getPrimaryTemplate();
    if (Tempdecl)
      decl = Tempdecl;
  } else if (isa<CXXRecordDecl>(decl)) {
    auto Tempdecl = cast<CXXRecordDecl>(decl)->getDescribedClassTemplate();
    if (Tempdecl)
      decl = Tempdecl;
  }

  MainStart += dumpLineInfo(decl->getLocStart(), Result.SourceManager);
  diag(Caller->getLocStart(), "insert include header")
      << FixItHint::CreateInsertion(decl->getLocStart(), MainStart);

  auto LocEnd = findLocationAfterBody(Result, Caller->getBodyRBrace());
  std::string MainEnd = "\n#endif\n";
  MainEnd += dumpLineInfo(Caller->getBodyRBrace(), Result.SourceManager);
  diag(Caller->getBodyRBrace(), "insert endif macro")
      << FixItHint::CreateInsertion(LocEnd, MainEnd);
}

void Tb31ProcessCheck::insertMacroGuard(
    const clang::FunctionDecl *Top,
    const ast_matchers::MatchFinder::MatchResult &Result) {

  auto Loc = Top->isInExternCContext() ? Top->getExternCContext()->getLocStart()
                                       : Top->getLocStart();
  std::string MainStart = "#ifndef HLS_FASTSIM\n#include \"apatb_" +
                          TopFunctionName + ".h\"\n#endif\n";
  MainStart += "#define " + TopFunctionName + "(...) AESL_ORIG_DUT_" +
               TopFunctionName + "(__VA_ARGS__)\n";
  MainStart += dumpLineInfo(Loc, Result.SourceManager);
  diag(Loc, "insert warp function declaration")
      << FixItHint::CreateInsertion(Loc, MainStart);

  auto LocEnd = findLocationAfterBody(Result, Top->getBodyRBrace());
  std::string MainEnd = "\n#undef " + TopFunctionName + "\n";
  MainEnd += dumpLineInfo(Top->getBodyRBrace(), Result.SourceManager);
  diag(Top->getBodyRBrace(), "insert undef macro")
      << FixItHint::CreateInsertion(LocEnd, MainEnd);
}

void Tb31ProcessCheck::insertOldMacroGuard(
    const clang::FunctionDecl *Top,
    const ast_matchers::MatchFinder::MatchResult &Result) {

  std::string MainStart = "#define " + TopFunctionName + " apatb_" + TopFunctionName + "_wrapper_" + TopFunctionName + "\n";
  //if (!Top->hasExternalFormalLinkage() && !Top->isExternC()) {
  if (!Top->isExternC()) {
    MainStart += (getLangOpts().CPlusPlus) ? "extern \"C\" {" : "extern ";
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
                          "*/}\n#undef " + TopFunctionName + "\n" :
                          "*/\n#undef " + TopFunctionName + "\n";
    MainEnd += dumpLineInfo(LocEnd, Result.SourceManager);
    diag(LocEnd, "insert undef macro")
      << FixItHint::CreateInsertion(LocEnd, MainEnd);

    auto BodyBegin = Top->getBody()->getLocStart();
    diag(BodyBegin, "hide the function body")
      << FixItHint::CreateInsertion(BodyBegin, ";/*");
  } else {
    std::string MainEnd = (MainStart[MainStart.size()-1] == '{') ?
                          "}\n#undef " + TopFunctionName + "\n" :
                          "\n#undef " + TopFunctionName + "\n";

    MainEnd += dumpLineInfo(LocEnd, Result.SourceManager);
    diag(LocEnd, "insert undef macro")
      << FixItHint::CreateInsertion(LocEnd, MainEnd);
  }
}

void Tb31ProcessCheck::insertOldMainDefinition(
    const clang::FunctionDecl *Caller,
    const ast_matchers::MatchFinder::MatchResult &Result) {
  // must be main
  if (!Caller->hasBody() || Caller->getNameAsString().compare("main"))
    return;
  auto Num = Caller->getNumParams();
  std::string MainStart;
  if (Num == 0)
    MainStart = "#define main() apatb_main(int argc, char** argv)\n";
  else if (Num == 1)
    MainStart = "#define main(x) apatb_main(int argc, char** argv)\n";
  else if (Num == 2)
    MainStart = "#define main apatb_main\n";
  else
    return;
  MainStart += dumpLineInfo(Caller->getLocStart(), Result.SourceManager);
  diag(Caller->getLocStart(), "insert main macro")
      << FixItHint::CreateInsertion(Caller->getLocStart(), MainStart);
}

void Tb31ProcessCheck::insertOldTopDefinition(
    const Expr *Mexpr, const MatchFinder::MatchResult &Result) {
  std::string GuardMacro = "\n#ifndef AESL_SYN\n";
  GuardMacro += "#define " + TopFunctionName + "(...) " + "apatb_" +
                TopFunctionName + "_wrapper_" + TopFunctionName +
                "(__VA_ARGS__)\n";
  GuardMacro += "#endif\n";
  GuardMacro += dumpLineInfo(Mexpr->getExprLoc(), Result.SourceManager);
  diag(Mexpr->getLocStart(), "insert top function call guard macro")
      << FixItHint::CreateInsertion(Mexpr->getLocStart(), GuardMacro);

  std::string Undef = "\n#undef " + TopFunctionName + "\n";
  Undef += dumpLineInfo(Mexpr->getExprLoc(), Result.SourceManager);
  auto LocEnd = findLocationAfterBody(Result, Mexpr->getLocEnd());
  diag(Mexpr->getLocEnd(), "insert undef macro")
      << FixItHint::CreateInsertion(LocEnd, Undef);
}

} // namespace xilinx
} // namespace tidy
} // namespace clang
