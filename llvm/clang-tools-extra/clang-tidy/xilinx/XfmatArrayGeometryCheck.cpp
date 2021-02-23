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

#include "XfmatArrayGeometryCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {

static bool ConstructSsdmArrayDimSize(StringRef VarStr, const llvm::APInt &Size,
                                      SmallString<64> &SsdmArraydim) {
  if (Size.getSExtValue() <= 0)
    return false;

  SsdmArraydim = "_ssdm_SpecArrayDimSize(";
  SsdmArraydim += VarStr.str();
  SsdmArraydim += ", ";
  SsdmArraydim += std::to_string(Size.getSExtValue());
  SsdmArraydim += ");";

  return true;
}

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

SourceLocation XfmatArrayGeometryCheck::findLocationAfterBody(
    const ast_matchers::MatchFinder::MatchResult &Result,
    SourceLocation BodyEnd, tok::TokenKind TKind) {

  auto AfterBodyEnd =
      Lexer::findLocationAfterToken(BodyEnd, TKind, *Result.SourceManager,
                                    Result.Context->getLangOpts(), true);
  if (!AfterBodyEnd.isInvalid())
    return AfterBodyEnd;

  return Lexer::getLocForEndOfToken(BodyEnd.getLocWithOffset(1), 0,
                                    *Result.SourceManager,
                                    Result.Context->getLangOpts());
}

void XfmatArrayGeometryCheck::registerMatchers(MatchFinder *Finder) {
  auto field =
      fieldDecl(hasAttr(attr::XCLArrayGeometry), hasType(pointerType()))
          .bind("x");
  auto record = recordDecl(has(field)).bind("y");
  Finder->addMatcher(record, this);
  auto rectype = type(
      hasUnqualifiedDesugaredType(recordType(hasDeclaration(decl(record)))));
  auto temptype = type(hasUnqualifiedDesugaredType(
      templateSpecializationType(hasDeclaration(decl(record)))));
  auto paramtype =
      anyOf(hasType(rectype), hasType(pointerType(pointee(rectype))),
            hasType(referenceType(pointee(rectype))), hasType(temptype),
            hasType(pointerType(pointee(temptype))),
            hasType(referenceType(pointee(temptype))));

  Finder->addMatcher(functionDecl(hasBody(forEachDescendant(declRefExpr(
                                      to(varDecl(paramtype).bind("param"))))),
                                  isDefinition(), unless(cxxMethodDecl()))
                         .bind("topdef"),
                     this);

  Finder->addMatcher(functionDecl(has(typeLoc(forEach(
                                      (parmVarDecl(paramtype).bind("param"))))),
                                  isDefinition(), unless(cxxMethodDecl()))
                         .bind("topdef"),
                     this);
}

void XfmatArrayGeometryCheck::check(const MatchFinder::MatchResult &Result) {
  // FIXME: Add callback implementation.
  const auto *recDecl = Result.Nodes.getNodeAs<RecordDecl>("y");
  const auto *arg = Result.Nodes.getNodeAs<VarDecl>("param");
  const auto *top = Result.Nodes.getNodeAs<FunctionDecl>("topdef");
  for (auto *fieldDecl : recDecl->fields()) {
    if (!fieldDecl->hasAttr<XCLArrayGeometryAttr>() ||
        !fieldDecl->getType()->isPointerType())
      continue;
    auto PointeeTy = fieldDecl->getType()->getPointeeOrArrayElementType();
    if (!PointeeTy->isBuiltinType() && !IsAPIntType(PointeeTy))
      continue;
    auto attr = fieldDecl->getAttr<XCLArrayGeometryAttr>();

    if (recDecl && !top) {
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
      continue;
    }

    if (top) {
      if (!arg->isLocalVarDeclOrParm()) {
        diag(arg->getLocStart(), "Do not support xcl_array_geometry attribute "
                                 "on global xf::mat variable",
             DiagnosticIDs::Error);
        return;
      }

      // insert dim size spec
      auto Dim = *attr->dims_begin();
      llvm::APSInt DimInt;
      if (!Dim->EvaluateAsInt(DimInt, *Result.Context))
        continue;

      SmallString<64> SsdmArraydim;
      std::string ident = arg->getType()->isPointerType() ? "->" : ".";
      std::string VarStr =
          arg->getNameAsString() + ident + fieldDecl->getNameAsString();
      if (!ConstructSsdmArrayDimSize(VarStr, DimInt, SsdmArraydim))
        continue;
      SourceLocation BodyStart;

      if (arg->isLocalVarDecl())
        BodyStart = Lexer::findLocationAfterToken(
            arg->getLocEnd(), tok::semi, *Result.SourceManager,
            Result.Context->getLangOpts(), false);
      else
        BodyStart = Lexer::getLocForEndOfToken(
            cast<CompoundStmt>(top->getBody())->getLBracLoc(), 0,
            *Result.SourceManager, Result.Context->getLangOpts());
      diag(arg->getLocStart(), "Variable %0 field %1 in function %2 require "
                               "add arraydim size for hint")
          << arg->getNameAsString() << fieldDecl->getNameAsString()
          << top->getNameAsString()
          << FixItHint::CreateInsertion(BodyStart, SsdmArraydim);
      continue;
    }
  }
}

} // namespace xilinx
} // namespace tidy
} // namespace clang
