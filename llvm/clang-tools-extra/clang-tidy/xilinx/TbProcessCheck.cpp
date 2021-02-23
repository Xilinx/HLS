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

#include "TbProcessCheck.h"
#include "XilinxTidyCommon.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <fstream>
#include <iostream>

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {

static bool isTemplateDecl(const FunctionDecl *Caller) {
  if (Caller->getTemplatedKind() == FunctionDecl::TK_FunctionTemplate)
    return true;

  return false;
}

TbProcessCheck::TbProcessCheck(StringRef Name, ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context),
      TopFunctionName(Options.get("TopFunctionName", "dut")),
      KeepTopName(Options.get("KeepTopName", 0)),
      NewFlow(Options.get("NewFlow", 0)), BCSim(Options.get("BCSim", 0)),
      BuildDir(Context->getCurrentBuildDirectory()) {}

void TbProcessCheck::storeOptions(ClangTidyOptions::OptionMap &Options) {
  TbProcessCheck::storeOptions(Options);
  this->Options.store(Options, "TopFunctionName", TopFunctionName);
  this->Options.store(Options, "KeepTopName", KeepTopName);
  this->Options.store(Options, "NewFlow", NewFlow);
  this->Options.store(Options, "BCSim", BCSim);
}

void TbProcessCheck::registerMatchers(MatchFinder *Finder) {

  if (KeepTopName) {
    // FIXME: do not match CXXMethodDecl as top function
    auto Top =
        functionDecl(allOf(hasName(TopFunctionName), unless(cxxMethodDecl())))
            .bind("top");
    Finder->addMatcher(
        functionDecl(allOf(hasName(TopFunctionName), isDefinition(),
                           unless(cxxMethodDecl())))
            .bind("topdef"),
        this);
    // support format: top()/a=top()/b=top
    Finder->addMatcher(
        expr(anyOf(callExpr(callee(Top)),
                   binaryOperator(
                       hasRHS(ignoringImpCasts(declRefExpr(to(Top)))))),
             hasAncestor(functionDecl(isDefinition()).bind("caller")))
            .bind("expr"),
        this);
  } else if (NewFlow && !BCSim) {
  } else {
  }
}

void TbProcessCheck::check(const MatchFinder::MatchResult &Result) {
  if (KeepTopName) {
    const auto *MatchedTopDef = Result.Nodes.getNodeAs<FunctionDecl>("topdef");
    if (MatchedTopDef) {
      generateWrapFunction(MatchedTopDef, Result);
      dumpTclFile();
    }

    const auto *MatchedTop = Result.Nodes.getNodeAs<FunctionDecl>("top");
    const auto *MatchedDecl = Result.Nodes.getNodeAs<FunctionDecl>("caller");
    const auto *MatchedExpr = Result.Nodes.getNodeAs<Expr>("expr");
    // insert SW wrapfunction first
    if (MatchedTop)
      buildSWWrapFunction(MatchedTop, Result.Context);
    if (MatchedExpr)
      insertGuardForKeepTopName(MatchedExpr, Result);
    if (MatchedDecl) {
      if (isTemplateDecl(MatchedDecl)) return;
      insertMainGuard(MatchedDecl, MatchedTop, Result);
    }
  }
}

SourceLocation
TbProcessCheck::findLocationAfterBody(const MatchFinder::MatchResult &Result,
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

void TbProcessCheck::dumpTclFile() {
  auto curFile = this->getCurrentMainFile().str();
  auto dumpFile =
      curFile.substr(0, curFile.find_last_of("/")) + "/tb.status.tcl";
  std::string dumpStr = "set ::AESL_AUTOSIM::gTopFileName " + curFile;
  std::ofstream o(dumpFile);
  o.write(dumpStr.c_str(), dumpStr.size());
  o.close();
}

std::string
TbProcessCheck::generateStructureDecl(const clang::QualType &Ty,
                                      const clang::ASTContext *Context) {
  clang::QualType canonicalTy = Ty.isCanonical() ?
                                Ty : Ty.getCanonicalType();
  structdecl ret;
  ret.Name =
      "struct __cosim_s" + std::to_string(Wrap.SturctureDecl.size() + 1) + "__";
  auto Str = canonicalTy.getAsString(Context->getPrintingPolicy());

  ret.Body = "char data[sizeof(" + Str + ")];";
  // insert structuredecl HwStub
  Wrap.SturctureDecl.emplace_back(ret);
  return ret.Name;
}
std::string TbProcessCheck::generateTypeString(const clang::QualType &Ty,
                                               const clang::ASTContext *Context,
                                               const Twine &PlaceHolder) {
  clang::QualType canonicalTy = Ty.isCanonical() ?
                                Ty : Ty.getCanonicalType();
  std::string s;
  llvm::raw_string_ostream ss(s);
  canonicalTy.print(ss, Context->getPrintingPolicy(), PlaceHolder);
  return ss.str();
}

void TbProcessCheck::buildSWWrapFunction(const clang::FunctionDecl *Callee,
                                         const clang::ASTContext *Context) {
  auto Fty = Callee->getFunctionType();
  auto &SWWrap = Wrap.Sw;

  SWWrap.Prefix = "#ifdef __cplusplus\nextern \"C\"\n#endif\n";
  SWWrap.RetType = generateTypeString(Fty->getReturnType(), Context);
  SWWrap.Name = "apatb_" + TopFunctionName + "_sw";
  SWWrap.ArgType.clear();
  SWWrap.ArgDef.clear();
  for (auto *Param : Callee->parameters()) {
    SWWrap.ArgType.emplace_back(generateTypeString(Param->getType(), Context));
    SWWrap.ArgDef.emplace_back(
        generateTypeString(Param->getType(), Context, Param->getName()));
  }
}

void TbProcessCheck::buildHWWrapFunction(const clang::FunctionDecl *Top,
                                         const clang::ASTContext *Context) {
  // insert sw arguments
  auto Fty = Top->getFunctionType();
  auto &SWWrap = Wrap.Sw;
  auto &HWWrap = Wrap.HwStub;
  // multiple define
  SWWrap.Arg.clear();
  HWWrap.Arg.clear();
  HWWrap.ArgType.clear();
  HWWrap.ArgDef.clear();

  for (unsigned i = 0; i < Top->getNumParams(); i++) {
    SWWrap.Arg.emplace_back(Top->parameters()[i]->getNameAsString());
    HWWrap.Arg.emplace_back(Top->parameters()[i]->getNameAsString());
  }

  HWWrap.Prefix = "#ifdef __cplusplus\nextern \"C\"\n#endif\n";

  HWWrap.Name = "apatb_" + TopFunctionName;
  HWWrap.Name += BCSim ? "_hw" : "_ir";
  // handle rettype first
  // FIXME: do not care about return type is pointer/reference
  auto OrigRetTy = Fty->getReturnType();
  if (OrigRetTy->isStructureOrClassType()) {
    HWWrap.RetType = OrigRetTy.getQualifiers().getAsString();
    if (!HWWrap.RetType.empty()) HWWrap.RetType += " ";
    HWWrap.RetType += generateStructureDecl(OrigRetTy, Context);
  } else
    HWWrap.RetType = SWWrap.RetType;

  // handle arg
  for (unsigned i = 0; i < Top->getNumParams(); i++) {
    auto Argty = Top->parameters()[i]->getType();
    if (Argty->isStructureOrClassType() || Argty->isUnionType()) {
      std::string Qualifier = Argty.getQualifiers().getAsString();
      if (!Qualifier.empty()) Qualifier += " ";

      std::string TypeStr = Qualifier + generateStructureDecl(Argty, Context);
      if (!BCSim) TypeStr += "*"; 

      HWWrap.ArgType.emplace_back(TypeStr);
      HWWrap.ArgDef.emplace_back(HWWrap.ArgType[i] + " " + HWWrap.Arg[i]);
    } else {
      HWWrap.ArgType.emplace_back(SWWrap.ArgType[i]);
      HWWrap.ArgDef.emplace_back(SWWrap.ArgDef[i]);
    }
  }
}

void TbProcessCheck::generateWrapFunction(
    const clang::FunctionDecl *Top, const MatchFinder::MatchResult &Result) {
  buildSWWrapFunction(Top, Result.Context);
  buildHWWrapFunction(Top, Result.Context);

  generateWrapBody(Top, true, Result.Context);
  generateWrapBody(Top, false, Result.Context);

  // dump structure decl first
  std::string dumpStr = "\n#ifndef HLS_FASTSIM\n";
  for (unsigned i = 0; i < Wrap.SturctureDecl.size(); i++)
    dumpStr += Wrap.SturctureDecl[i].dump() + "\n";

  // add apatb_dut_hw declaration first
  if (!BCSim && Top->getFunctionType()->getReturnType()->isStructureOrClassType()) 
    dumpStr += Wrap.HwStub.dumpReturnStructAsArg() + ";\n";
  else 
    dumpStr += Wrap.HwStub.dumpSignature() + ";\n";
  Wrap.HwStub.Name = TopFunctionName + "_hw_stub";
  if (!BCSim && Top->getFunctionType()->getReturnType()->isStructureOrClassType())
    dumpStr += Wrap.HwStub.dumpAllReturnStructAsArg() + "\n";
  else 
    dumpStr += Wrap.HwStub.dumpAll() + "\n";
  dumpStr += Wrap.Sw.dumpAll() + "\n";
  dumpStr += "#endif\n";

  auto LocEnd = findLocationAfterBody(Result, Top->getBodyRBrace());
  dumpStr += dumpLineInfo(Top->getBodyRBrace(), Result.SourceManager);
  diag(Top->getBodyRBrace(), "insert hw/sw wrap function")
      << FixItHint::CreateInsertion(LocEnd, dumpStr);
}

void TbProcessCheck::generateWrapBody(const clang::FunctionDecl *Top,
                                      bool IsSW,
                                      const clang::ASTContext *Context) {
  std::string Callstmt;
  std::string Retstmt = "return ";
  auto &Called = IsSW ? Wrap.HwStub : Wrap.Sw;
  auto &ConvTo = IsSW ? Wrap.Sw : Wrap.HwStub;
  // using dut in HW->SW
  auto Name = IsSW ? Called.Name : TopFunctionName;

  auto Fty = Top->getFunctionType();
  auto Retty = Fty->getReturnType();

  if (Retty->isVoidType()) {
    // no need void declaration
    Callstmt = Name + "(";
  } else if (Retty->isStructureOrClassType()) {
    if (IsSW && !BCSim) {
      Callstmt = Called.RetType + " _ret;\n";
      Callstmt +=  Name + "(&_ret, ";
    } else if (!IsSW && !BCSim) {
      Callstmt += "*((" + Called.RetType + "*)_ret) = " + Name + "(";
    } else {
      Callstmt = Called.RetType + " _ret = " + Name + "(";
    }
    Retstmt += "*((" + ConvTo.RetType + "*)&_ret)";
    if (!IsSW && !BCSim) Retstmt = "";
  } else {
    Callstmt = Called.RetType + " _ret = " + Name + "(";
    Retstmt += "_ret";
  }

  for (unsigned i = 0; i < Top->getNumParams(); i++) {
    auto Ty = Top->parameters()[i]->getType();
    if (const RecordType *RT = Ty->getAs<RecordType>()) {
      RecordDecl *RD = RT->getDecl();
      auto FIt = RD->field_begin();
      if (!IsSW && !RD->field_empty() && ++FIt == RD->field_end()) {
        auto FTy = (*RD->field_begin())->getType();
        if (FTy->isPointerType()) {
          Callstmt += "(" + generateTypeString(FTy, Context)
                        + ")" + ConvTo.Arg[i];
            if (i < Top->getNumParams() - 1)
              Callstmt += ", ";
            continue;
        }
      }

      if (BCSim)
        Callstmt += "*((" + Called.ArgType[i] + "*)&" + ConvTo.Arg[i] + ")";
      else if (IsSW)
        Callstmt += "((" + Called.ArgType[i] + ")&" + ConvTo.Arg[i] + ")";
      else 
        Callstmt += "*((" + Called.ArgType[i] + "*)" + ConvTo.Arg[i] + ")";
    } else {
      Callstmt += ConvTo.Arg[i];
    }

    if (i < Top->getNumParams() - 1)
      Callstmt += ", ";
  }
  Callstmt += ");\n";
  Retstmt += ";";

  ConvTo.Body = Callstmt + Retstmt;
}

void TbProcessCheck::insertGuardForKeepTopName(
    const Expr *Mexpr, const MatchFinder::MatchResult &Result) {
  std::string GuardMacro = "\n#ifndef HLS_FASTSIM\n";
  GuardMacro += "#define " + TopFunctionName + " " + Wrap.Sw.Name + "\n";
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

void TbProcessCheck::insertMainGuard(
    const clang::FunctionDecl *Caller, const clang::FunctionDecl *Callee,
    const ast_matchers::MatchFinder::MatchResult &Result) {

  const Decl *decl = Caller;
  if (isa<FunctionDecl>(decl)) {
    auto Tempdecl = cast<FunctionDecl>(decl)->getPrimaryTemplate();
    if (Tempdecl)
      decl = Tempdecl;
  } else if (isa<CXXRecordDecl>(decl)) {
    auto Tempdecl = cast<CXXRecordDecl>(decl)->getDescribedClassTemplate();
    if (Tempdecl)
      decl = Tempdecl;
  }
  std::string MainStart = "#ifndef HLS_FASTSIM\n";
  auto SWWrap = Wrap.Sw;

  std::string ReplacedProtoType = SWWrap.dumpSignature();
  MainStart += ReplacedProtoType + ";\n";
  MainStart += dumpLineInfo(decl->getLocStart(), Result.SourceManager);
  diag(decl->getLocStart(), "insert warp function declaration")
      << FixItHint::CreateInsertion(decl->getLocStart(), MainStart);

  auto LocEnd = findLocationAfterBody(Result, Caller->getBodyRBrace());
  std::string MainEnd = "\n#endif\n";
  MainEnd += dumpLineInfo(Caller->getBodyRBrace(), Result.SourceManager);
  diag(Caller->getBodyRBrace(), "insert main function guard macro")
      << FixItHint::CreateInsertion(LocEnd, MainEnd);
}

std::string wrapfunction::dumpAll() {
  std::string dumpStr = dumpSignature(true) + "{\n" + Body + "\n}";
  return dumpStr;
}
std::string wrapfunction::dumpAllReturnStructAsArg() {
  std::string dumpStr = dumpReturnStructAsArg(true) + "{\n" + Body + "\n}";
  return dumpStr;
}
std::string wrapfunction::dumpReturnStructAsArg(bool IsDef) {
  std::string dumpStr = Prefix + "void " + Name + "(" + RetType + "*";
  dumpStr += IsDef ? " _ret" : "";
  if (ArgType.size() > 0) dumpStr += ", ";
  for (unsigned i = 0; i < ArgType.size(); i++) {
    dumpStr += IsDef ? ArgDef[i] : ArgType[i];
    if (i < ArgType.size() - 1)
      dumpStr += ", ";
  }
  dumpStr += ")";
  return dumpStr;
}

std::string wrapfunction::dumpSignature(bool IsDef) {
  std::string dumpStr = Prefix + RetType + " " + Name + "(";
  for (unsigned i = 0; i < ArgType.size(); i++) {
    dumpStr += IsDef ? ArgDef[i] : ArgType[i];
    if (i < ArgType.size() - 1)
      dumpStr += ", ";
  }
  dumpStr += ")";
  return dumpStr;
}

std::string structdecl::dump() {
  std::string dumpStr = Name + "{" + Body + "};";
  return dumpStr;
}

} // namespace xilinx
} // namespace tidy
} // namespace clang
