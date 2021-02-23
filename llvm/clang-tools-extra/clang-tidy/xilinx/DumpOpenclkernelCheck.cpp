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

#include "DumpOpenclkernelCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {

void DumpOpenclkernelCheck::registerMatchers(MatchFinder *Finder) {
  if (!getLangOpts().OpenCL || FileName.getValue().empty())
    return;

  Finder->addMatcher(functionDecl(hasAttr(attr::OpenCLKernel)).bind("x"), this);
}

void DumpOpenclkernelCheck::check(const MatchFinder::MatchResult &Result) {
  // FIXME: Add callback implementation.
  const auto *MatchedDecl = Result.Nodes.getNodeAs<FunctionDecl>("x");
  auto FuncName = MatchedDecl->getNameAsString();
  KernelArray.push_back(FuncName);
  // diag(MatchedDecl->getLocation(), "function %0 is opencl kernel") <<
  // FuncName;
}

void DumpOpenclkernelCheck::onEndOfTranslationUnit() {
  std::ofstream o(FileName.getValue(), std::ios_base::out);

  for (auto item : KernelArray)
    o << item << "\n";

  return;
}

} // namespace xilinx
} // namespace tidy
} // namespace clang
