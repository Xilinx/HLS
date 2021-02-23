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

#include "TbXfmatCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {

static bool IsAPIntType(const Type *Ty) {
  if (Ty->isStructureOrClassType()) {
    auto TypeStr =
        Ty->getAsTagDecl()->getCanonicalDecl()->getQualifiedNameAsString();
    if (!TypeStr.compare("ap_int") || !TypeStr.compare("ap_uint") ||
        !TypeStr.compare("ap_fixed") || !TypeStr.compare("ap_ufixed"))
      return true;
  }
  return false;
}

void TbXfmatCheck::registerMatchers(MatchFinder *Finder) {
  auto field =
      fieldDecl(hasAttr(attr::XCLArrayGeometry), hasType(pointerType()))
          .bind("x");
  auto record = recordDecl(has(field)).bind("y");
  Finder->addMatcher(record, this);
}

void TbXfmatCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *recDecl = Result.Nodes.getNodeAs<RecordDecl>("y");
  for (auto *fieldDecl : recDecl->fields()) {
    if (!fieldDecl->hasAttr<XCLArrayGeometryAttr>() ||
        !fieldDecl->getType()->isPointerType())
      continue;
    auto PointeeTy = fieldDecl->getType()->getPointeeOrArrayElementType();
    if (!PointeeTy->isBuiltinType() && !IsAPIntType(PointeeTy))
      continue;

    auto attr = fieldDecl->getAttr<XCLArrayGeometryAttr>();

    // skip specializationDecl
    if (isa<ClassTemplateSpecializationDecl>(recDecl)) {
      auto tempdecl = cast<ClassTemplateSpecializationDecl>(recDecl)
                          ->getSpecializedTemplate()
                          ->getCanonicalDecl();
      SmallVector<FieldDecl *, 4> fieldset(
          tempdecl->getTemplatedDecl()->getCanonicalDecl()->fields());
      fieldDecl = fieldset[fieldDecl->getFieldIndex()];
      attr = fieldDecl->getAttr<XCLArrayGeometryAttr>();
    }
    // remove attribute
    diag(fieldDecl->getLocStart(), "remove %0")
        << attr->getSpelling() << FixItHint::CreateRemoval(attr->getRange());
  }
}

} // namespace xilinx
} // namespace tidy
} // namespace clang
