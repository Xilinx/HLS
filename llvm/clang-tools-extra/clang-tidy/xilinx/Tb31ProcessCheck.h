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

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_XILINX_TB31PROCESSCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_XILINX_TB31PROCESSCHECK_H

#include "../ClangTidy.h"

namespace clang {
namespace tidy {
namespace xilinx {

/// FIXME: Write a short description.
///
/// For the user-facing documentation see:
/// http://clang.llvm.org/extra/clang-tidy/checks/xilinx-tb31-process.html
class Tb31ProcessCheck : public ClangTidyCheck {
public:
  Tb31ProcessCheck(StringRef Name, ClangTidyContext *Context);
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void storeOptions(ClangTidyOptions::OptionMap &Options) override;

  void insertGuardForKeepTopName(
      const clang::Expr *Mexpr,
      const ast_matchers::MatchFinder::MatchResult &Result);

  void insertMainGuard(const clang::FunctionDecl *Caller,
                       const ast_matchers::MatchFinder::MatchResult &Result);
  void insertMacroGuard(const clang::FunctionDecl *Top,
                        const ast_matchers::MatchFinder::MatchResult &Result);
  void
  insertOldMacroGuard(const clang::FunctionDecl *Top,
                      const ast_matchers::MatchFinder::MatchResult &Result);
  void
  insertOldMainDefinition(const clang::FunctionDecl *Caller,
                          const ast_matchers::MatchFinder::MatchResult &Result);
  void
  insertOldTopDefinition(const clang::Expr *Mexpr,
                         const ast_matchers::MatchFinder::MatchResult &Result);
  SourceLocation
  findLocationAfterBody(const ast_matchers::MatchFinder::MatchResult &Result,
                        SourceLocation BodyEnd);

  void dumpTclFile();

private:
  std::string TopFunctionName;
  bool KeepTopName;
  bool NewFlow;
  bool BCSim;
  std::string BuildDir;
};

} // namespace xilinx
} // namespace tidy
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_XILINX_TB31PROCESSCHECK_H
