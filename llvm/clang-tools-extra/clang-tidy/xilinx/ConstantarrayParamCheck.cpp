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

#include "ConstantarrayParamCheck.h"
#include "../utils/Matchers.h"
#include "XilinxTidyCommon.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Lex/Lexer.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {

static bool ConstructSsdmArrayDimSize(const ParmVarDecl *Var,
                                      const llvm::APInt &Size,
                                      SmallString<64> &SsdmArraydim) {
  if (Var->getNameAsString().empty() || Size.getSExtValue() <= 0)
    return false;

  SsdmArraydim = "_ssdm_SpecArrayDimSize(";
  SsdmArraydim += Var->getNameAsString();
  SsdmArraydim += ", ";
  SsdmArraydim += std::to_string(Size.getSExtValue());
  SsdmArraydim += ");";

  return true;
}

static bool ConstructSsdmConstant(const VarDecl *Var,
                                  SmallString<64> &SsdmSpec) {
  if (Var->getNameAsString().empty())
    return false;

  SsdmSpec = "\n_ssdm_SpecConstant(";
  if (Var->getType()->isPointerType() || Var->getType()->isArrayType())
    SsdmSpec += Var->getNameAsString();
  else
    SsdmSpec += "&" + Var->getNameAsString();
  SsdmSpec += ");\n";

  return true;
}

SourceLocation ConstantarrayParamCheck::findLocationAfterBody(
    const ast_matchers::MatchFinder::MatchResult &Result,
    SourceLocation BodyEnd) {
  auto AfterBodyEnd =
      Lexer::findLocationAfterToken(BodyEnd, tok::semi, *Result.SourceManager,
                                    Result.Context->getLangOpts(), false);
 
    return AfterBodyEnd;
   //skip case like const int a[] = {}, b = {};
}

void ConstantarrayParamCheck::registerMatchers(MatchFinder *Finder) {
  const auto ConstantArrayParamDecl =
      parmVarDecl(hasType(decayedType(hasDecayedType(pointerType()))),
                  decl().bind("param"));
  Finder->addMatcher(functionDecl(isDefinition(),
                                  unless(cxxMethodDecl(isOverride())),
                                  unless(isInstantiated()),
                                  has(typeLoc(forEach(ConstantArrayParamDecl))),
                                  decl().bind("functionDecl")),
                     this);
}

void ConstantarrayParamCheck::check(
    const MatchFinder::MatchResult &Result) {
  const auto *Param = Result.Nodes.getNodeAs<ParmVarDecl>("param");
  const auto *Function = Result.Nodes.getNodeAs<FunctionDecl>("functionDecl");
  const auto *ConstVar = Result.Nodes.getNodeAs<VarDecl>("constvar");
  SourceManager *SM = Result.SourceManager;

  QualType ParamTy;
  // getOriginalType will get the type before we decay the type
  ParamTy = Param->getOriginalType();
  // skip typedef type
  ParamTy = ParamTy.getCanonicalType();

  bool IsConstantArray = ParamTy->isConstantArrayType();
  bool IsTopFunc = Function->hasAttr<SDxKernelAttr>();

  // Skip declarations delayed by late template parsing without a body.
  // And, only insert for Top function
  if (!Function->getBody() || !IsConstantArray || !IsTopFunc)
    return;

  auto BodyStart = Lexer::getLocForEndOfToken(
      cast<CompoundStmt>(Function->getBody())->getLBracLoc(), 0,
      *Result.SourceManager, Result.Context->getLangOpts());

  SmallString<64> SsdmArraydim;
  llvm::APInt Size = cast<ConstantArrayType>(ParamTy)->getSize();
  if (!ConstructSsdmArrayDimSize(Param, Size, SsdmArraydim))
    return;

  diag(Param->getLocStart(),
       "param %0 in function %1 require add arraydim size for hint")
      << Param->getNameAsString() << Function->getNameAsString()
      << FixItHint::CreateInsertion(BodyStart, SsdmArraydim);
  return;
}

} // namespace xilinx
} // namespace tidy
} // namespace clang
