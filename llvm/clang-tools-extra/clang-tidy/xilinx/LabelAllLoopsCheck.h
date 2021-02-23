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

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_XILINX_LABELALLLOOPSCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_XILINX_LABELALLLOOPSCHECK_H

#include "../ClangTidy.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {

/// Look for all types of loops that don't have user specified loop labels
/// and generate a unique loop label for each such loop. Doing it early,
/// ensures that all loops are named and are accessible from TCL.
///
/// For the user-facing documentation see:
/// http://clang.llvm.org/extra/clang-tidy/checks/xilinx-LabelAllLoops.html
class LabelAllLoopsCheck : public ClangTidyCheck {
private:
    static std::string loopLabelPrefix;
    std::map<const FunctionDecl *, unsigned int> funcLabelMap;
    std::string getLoopLabel(const MatchFinder::MatchResult &Result, const Stmt *stmt);
    std::string getUniqueLabel(const Stmt *loopStmt,
                               const MatchFinder::MatchResult &Result,
                               unsigned int &loopCounter,
                               const FunctionDecl *fDecl);
public:
  LabelAllLoopsCheck(StringRef Name, ClangTidyContext *Context)
      : ClangTidyCheck(Name, Context) {}
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
};

} // namespace xilinx
} // namespace tidy
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_XILINX_LABELALLLOOPSCHECK_H
