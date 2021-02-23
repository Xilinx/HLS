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

#include "ArrayStreamCheckCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {

static QualType GetStrippedType(QualType Ty) {
  if (Ty->isReferenceType() || Ty->isPointerType())
    return Ty->getPointeeType();
  return Ty;
}

static bool IsHLSStreamType(QualType Ty) {
  auto *BTy = Ty->isReferenceType() ? Ty->getPointeeType().getTypePtr()
                                    : Ty->getPointeeOrArrayElementType();

  if (BTy->isStructureOrClassType() && !BTy->getAsTagDecl()
                                           ->getCanonicalDecl()
                                           ->getQualifiedNameAsString()
                                           .compare("hls::stream"))
    return true;

  return false;
}

/// CheckParamAttr - Check the attribute of function parameter 
/// return false if the parameter is an array stream without volatile qualifier.
/// Otherwise return true.
static bool CheckParamAttr(const ParmVarDecl *Param) {
  FPGAAddressInterfaceAttr *Attr = Param->getAttr<FPGAAddressInterfaceAttr>();
  if (!Attr)
    return true;

  // Check whether the attribute has array to stream implication.
  StringRef Mode = Attr->getMode();
  auto isArray2Stream = llvm::StringSwitch<bool>(Mode)
                                .CaseLower("ap_fifo", true)
                                .CaseLower("axis", true)
                                .Default(false);
  if (!isArray2Stream)
    return true;

  auto ParamTy = Param->getOriginalType().getCanonicalType();
  // if the parameter is of type hls::stream, it doesn't need to be volatile.
  if (IsHLSStreamType(ParamTy))
    return true;

  // get the underlying type if the type is pointer or reference.
  auto UnderlyingTy = GetStrippedType(ParamTy);

  return UnderlyingTy.isVolatileQualified();
}

void ArrayStreamCheckCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(
      functionDecl(isDefinition(), unless(cxxMethodDecl()),
                   has(typeLoc(forEach((parmVarDecl().bind("param"))))))
          .bind("x"),
      this);
}

void ArrayStreamCheckCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *MatchedDecl = Result.Nodes.getNodeAs<FunctionDecl>("x");
  const auto *ParamDecl = Result.Nodes.getNodeAs<ParmVarDecl>("param");

  if (!CheckParamAttr(ParamDecl)) {
    diag(ParamDecl->getLocation(),
         "%0 Array stream parameter %1 in function %2 may require the"
         " 'volatile' qualifier to prevent the compiler from altering array"
         " accesses and/or modifying the desired streaming order",
         DiagnosticIDs::Warning)
        << "[Clang7-Core-Subset]" << ParamDecl << MatchedDecl;
//        << FixItHint::CreateInsertion(ParamDecl->getTypeSpecStartLoc(),
//                                      "volatile ");
  }
}

} // namespace xilinx
} // namespace tidy
} // namespace clang
