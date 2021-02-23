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

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_XILINX_TBPROCESSCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_XILINX_TBPROCESSCHECK_H

#include "../ClangTidy.h"

namespace clang {
namespace tidy {
namespace xilinx {
struct wrapfunction {
  std::string Prefix;
  std::string RetType;
  std::string Name;
  SmallVector<std::string, 4> Arg;
  SmallVector<std::string, 4> ArgDef;
  SmallVector<std::string, 4> ArgType;
  std::string Body;
  std::string dumpAll();
  std::string dumpAllReturnStructAsArg();
  std::string dumpSignature(bool IsDef = false);
  std::string dumpReturnStructAsArg(bool IsDef = false);
};
struct structdecl {
  std::string Name;
  std::string Body;
  std::string dump();
};
struct wrapdumper {
  wrapfunction Sw;
  wrapfunction HwStub;
  SmallVector<structdecl, 4> SturctureDecl;
  //	llvm::StringMap<std::string> StructMap;
};
/// FIXME: Write a short description.
///
/// For the user-facing documentation see:
/// http://clang.llvm.org/extra/clang-tidy/checks/xilinx-tb-process.html
class TbProcessCheck : public ClangTidyCheck {
public:
  TbProcessCheck(StringRef Name, ClangTidyContext *Context);
  void storeOptions(ClangTidyOptions::OptionMap &Options) override;
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;

  SourceLocation
  findLocationAfterBody(const ast_matchers::MatchFinder::MatchResult &Result,
                        SourceLocation BodyEnd);

  void dumpTclFile();
  std::string generateStructureDecl(const clang::QualType &Ty,
                                    const clang::ASTContext *Context);
  std::string generateTypeString(const clang::QualType &Ty,
                                 const clang::ASTContext *Context,
                                 const Twine &PlaceHolder = Twine());
  void buildSWWrapFunction(const clang::FunctionDecl *Callee,
                           const clang::ASTContext *Context);
  void buildHWWrapFunction(const clang::FunctionDecl *Top,
                           const clang::ASTContext *Context);
  void
  generateWrapFunction(const clang::FunctionDecl *Top,
                       const ast_matchers::MatchFinder::MatchResult &Result);
  void generateWrapBody(const clang::FunctionDecl *Top, bool IsSW,
                        const clang::ASTContext *Context);
  void insertGuardForKeepTopName(
      const clang::Expr *Mexpr,
      const ast_matchers::MatchFinder::MatchResult &Result);

  void insertMainGuard(const clang::FunctionDecl *Caller,
                       const clang::FunctionDecl *Callee,
                       const ast_matchers::MatchFinder::MatchResult &Result);

private:
  std::string TopFunctionName;
  bool KeepTopName;
  bool NewFlow;
  bool BCSim;
  std::string BuildDir;
  wrapdumper Wrap;
};
} // namespace xilinx
} // namespace tidy
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_XILINX_TBPROCESSCHECK_H
