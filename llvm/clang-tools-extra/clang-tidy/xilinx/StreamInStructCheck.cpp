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
//===----------------------------------------------------------------------===//
//
// xilinx-stream-in-struct checks the aggregate type (e.g. struct) to see
// whether there is hls::stream field in it. If so, we will give a warning.
//
// For example:
// struct S0 {
//   hls::stream<int> A;
// };
//
// In struct S0, there is a field A of hls::stream type which is not support
// for now.
//===----------------------------------------------------------------------===//


#include "StreamInStructCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {

static bool IsHLSStreamType(QualType Ty) {
  auto *BTy = Ty.getTypePtr();

  if (Ty->isReferenceType())
    BTy = Ty->getPointeeType().getTypePtr();

  if (Ty->isArrayType() || Ty->isAnyPointerType())
    BTy = Ty->getPointeeOrArrayElementType();

  if (BTy->isStructureOrClassType() && !BTy->getAsTagDecl()
                                           ->getCanonicalDecl()
                                           ->getQualifiedNameAsString()
                                           .compare("hls::stream"))
    return true;

  return false;
}

static bool IsStreamField(const FieldDecl *FD) {
  auto ParamTy = FD->getType().getCanonicalType();
  if (IsHLSStreamType(ParamTy))
    return true;

  return false;
}

void StreamInStructCheck::registerMatchers(MatchFinder *Finder) {
  // Match the struct declaration.
  auto field = fieldDecl(hasType(elaboratedType())).bind("x");
  auto record = recordDecl(forEach(field)).bind("y");
  Finder->addMatcher(record, this);
}

void StreamInStructCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *RecDecl = Result.Nodes.getNodeAs<RecordDecl>("y");
  const auto *FD = Result.Nodes.getNodeAs<FieldDecl>("x");

  // Give an error on struct declaration which contains hls::stream
  // as fields.
  if (IsStreamField(FD)) {
    diag(FD->getLocation(),
         "%0 hls::stream object(%1) inside aggregate type %2 is not supported",
         DiagnosticIDs::Error)
      << "[Clang7-Core-Subset]" << FD << RecDecl;
  }
}

} // namespace xilinx
} // namespace tidy
} // namespace clang
