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

#include "SystemcDetectorCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>
#include <iostream>
#include <utility>

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {

void SystemcDetectorCheck::registerMatchers(MatchFinder *Finder) {
  if (!getLangOpts().CPlusPlus)
    return;
  if (llvm::sys::fs::exists(Twine(FileName.getValue()))) {
    isSystemC = true;
    return;
  }

  Finder->addMatcher(
      cxxRecordDecl(matchesName(".*sc_module$"), isDefinition()).bind("X"),
      this);
}

void SystemcDetectorCheck::check(const MatchFinder::MatchResult &Result) {
  if (isSystemC)
    return;
  const auto *recdecl = Result.Nodes.getNodeAs<Decl>("X");
  if (recdecl) {
    isSystemC = true;
    diag(recdecl->getLocStart(), "This file is one of SystemC project");
  }
  return;
}

void SystemcDetectorCheck::onEndOfTranslationUnit() {
  if (isSystemC)
    std::ofstream o(FileName.getValue());

  return;
}

} // namespace xilinx
} // namespace tidy
} // namespace clang
