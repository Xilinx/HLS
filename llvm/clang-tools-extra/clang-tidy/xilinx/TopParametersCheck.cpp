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

#include "TopParametersCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "clang-tidy-dmp-top-para"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {

static const Type *StripType(QualType Ty) {
  Ty = Ty.getCanonicalType();
  auto *BTy = Ty->isReferenceType() ? Ty->getPointeeType().getTypePtr()
                                    : Ty->getPointeeOrArrayElementType();
  return BTy;
}



static const Type *GetAsStructureType(const DeclaratorDecl *Var) {

  auto Ty = isa<ParmVarDecl>(Var)
                ? cast<ParmVarDecl>(Var)->getOriginalType().getCanonicalType()
                : Var->getType().getCanonicalType();

  auto *BTy = StripType(Ty);
  if (BTy->isStructureOrClassType())
    return BTy;
  return nullptr;
}

static const Type *IsSpecificType(const Type *Ty, StringRef Name) {
  if (Ty->isStructureOrClassType() && !Ty->getAsTagDecl()
                                           ->getCanonicalDecl()
                                           ->getQualifiedNameAsString()
                                           .compare(Name))
    return Ty;

  return nullptr;
}

static const Type *CheckParameterType(const Type *Ty, StringRef Name) {
  if (IsSpecificType(Ty, Name))
    return Ty;

  if (!Ty->getAsCXXRecordDecl())
    return nullptr;

  for (auto *fieldDecl :
       Ty->getAsCXXRecordDecl()->getCanonicalDecl()->fields()) {
    const auto *fieldTy = GetAsStructureType(fieldDecl);
    if (!fieldTy)
      continue;
    const auto *candidataTy = CheckParameterType(fieldTy, Name);
    if (candidataTy)
      return candidataTy;
  }

  return nullptr;
}

static const Type* IsAPType(const Type *Ty) {
  if (Ty == CheckParameterType(Ty, "ap_int"))
    return Ty;
  if (Ty == CheckParameterType(Ty, "ap_uint"))
    return Ty;
  if (Ty == CheckParameterType(Ty, "ap_fixed"))
    return Ty;
  if (Ty == CheckParameterType(Ty, "ap_ufixed"))
    return Ty;
  return nullptr;
}

static const Type *StripStreamType(const Type *Ty) {
  if (!IsSpecificType(Ty, "hls::stream"))
    return nullptr;
  
  assert(
      isa<ClassTemplateSpecializationDecl>(Ty->getAsCXXRecordDecl()) &&
      "Wrong hls::stream type!");

  auto *CandidataDecl =
      cast<ClassTemplateSpecializationDecl>(Ty->getAsCXXRecordDecl());
  auto InnerType =
      CandidataDecl->getTemplateArgs()[0].getAsType();
  return InnerType.getTypePtr(); 
}

static bool InvalidFieldType(const Type *Ty) {
  RecordDecl *Rec = cast<RecordDecl>(Ty->getAsTagDecl());
  if (isa<CXXRecordDecl>(Rec))
    Rec = cast<CXXRecordDecl>(Rec)->getCanonicalDecl();
  for (auto *fieldDecl : Rec->fields()) {
    auto FTy = fieldDecl->getType().getCanonicalType();
    if (FTy->isPointerType())
      return true;

    const auto *fieldTy = GetAsStructureType(fieldDecl);
    if (fieldTy && InvalidFieldType(fieldTy))
      return true;
  }
  return false;
}

static bool UnsupportedAPIntType(const Type *Ty) {
  if (Ty->isAPIntType())
    return true;

  // uint128/int128/float128 is not supported now
  if (Ty->isBuiltinType()) {
    auto Kind = Ty->getAs<BuiltinType>()->getKind();
    if (Kind == BuiltinType::Int128 || Kind == BuiltinType::UInt128 ||
        Kind == BuiltinType::Float128)
      return true;
  }

  if (!Ty->isStructureOrClassType())
    return false;

  for (auto *fieldDecl : Ty->getAs<RecordType>()->getDecl()->fields()) {
    if (UnsupportedAPIntType(StripType(fieldDecl->getType())))
      return true;
  }

  return false;
}

static bool IsArrayOfStructAsAXIS(const ParmVarDecl *Param) {
  bool isArrayType = Param->getOriginalType().getCanonicalType()
                          ->isArrayType();
  if (!isArrayType)
    return false;
  const auto *ParamTy = GetAsStructureType(Param);
 
  if (ParamTy && IsAPType(ParamTy))
    return false;

  FPGAAddressInterfaceAttr *Attr = Param->getAttr<FPGAAddressInterfaceAttr>();
  if (!Attr)
    return false;

  // Check whether the attribute has array to stream implication.
  StringRef Mode = Attr->getMode();
  auto isArray2Stream = llvm::StringSwitch<bool>(Mode)
                        .CaseLower("axis", true)
                        .CaseLower("fifo", true)
                        .Default(false);
  return isArray2Stream;
}

static bool IsArrayOfStreamWithStruct(const ParmVarDecl *Param) {
  bool isArrayType = Param->getOriginalType().getCanonicalType()
                          ->isArrayType();
  if (!isArrayType)
    return false;
  const auto *ParamTy = GetAsStructureType(Param);

  if (!ParamTy)
    return false;

  ParamTy = StripStreamType(ParamTy);
  
  if (ParamTy && IsAPType(ParamTy))
    return false;

  return ParamTy->isStructureOrClassType();
}

void TopParametersCheck::CheckUnsupportedAP_INTType(
    const Type *Ty, const DeclaratorDecl *Decl) {
  if (UnsupportedAPIntType(Ty))
    diag(Decl->getLocation(),
         "%0 %select{Function|Parameter}1 %2 "
         "%select{return value|}1 is a C language "
         "arbitrary-precision integer type",
         DiagnosticIDs::Error)
        << "[Clang7-Core-Subset]" << isa<ParmVarDecl>(Decl) << Decl;
}

void TopParametersCheck::registerMatchers(MatchFinder *Finder) {

  Finder->addMatcher(
      functionDecl(isDefinition(), hasAttr(attr::SDxKernel),
                   unless(cxxMethodDecl()),
                   has(typeLoc(forEach((parmVarDecl().bind("param"))))))
          .bind("x"),
      this);
  Finder->addMatcher(functionDecl(isDefinition(), hasAttr(attr::SDxKernel),
                                  unless(cxxMethodDecl()))
                         .bind("x"),
                     this);
}

void TopParametersCheck::check(const MatchFinder::MatchResult &Result) {

  const auto *FuncDecl = Result.Nodes.getNodeAs<FunctionDecl>("x");
  const auto *ParamDecl = Result.Nodes.getNodeAs<ParmVarDecl>("param");

  auto RetTy = StripType(FuncDecl->getReturnType());
  // return type shouldn't be APInt(Arbitrary-precision integer type) type, or
  // contain a non power of 2 ap_int, or be a smaller than 8 bit ap_int if the
  // ap_int is used as a template parameter

  if (!ParamDecl)
    CheckUnsupportedAP_INTType(RetTy, FuncDecl);
  else {
#if 0
    if (ParamDecl->hasAttr<XlxArrayXFormAttr>()) {
      diag(ParamDecl->getLocation(),
           "%0 Array partition/reshape can not be used on Parameter %1 ",
           DiagnosticIDs::Error)
          << "[Clang7-Core-Subset]" << ParamDecl;
    }
#endif

    // parameter type shouldn't be APInt type, or contain a non power of 2
    // ap_int, or be a smaller than 8 bit ap_int if the ap_int is used as a
    // template parameter
    CheckUnsupportedAP_INTType(StripType(ParamDecl->getType()), ParamDecl);

    const auto *ParamTy = GetAsStructureType(ParamDecl);
    if (!ParamTy)
      return;

    if (IsSpecificType(ParamTy, "hls::burst_maxi"))
      return;

#if 0
    if (InvalidFieldType(ParamTy)) {
      diag(ParamDecl->getLocation(), "%0 Parameter %1 contains pointer type",
           DiagnosticIDs::Error)
          << "[Clang7-Core-Subset]" << ParamDecl;
    }
#endif

#if 0
    if (auto *CandTy = CheckParameterType(ParamTy, "hls::stream")) {
      if (CandTy == ParamTy) {
        if (IsArrayOfStreamWithStruct(ParamDecl))
          diag(ParamDecl->getLocation(),
               "%0 Parameter %1 is stream array,"
               " which contains structure", DiagnosticIDs::Error)
              << "[Clang7-Core-Subset]" << ParamDecl;
      } else {
        diag(ParamDecl->getLocation(),
             "%0 Parameter %1 contains field of stream type",
             DiagnosticIDs::Error)
            << "[Clang7-Core-Subset]" << ParamDecl;
      }
    }
#endif

#if 0
    if (IsArrayOfStructAsAXIS(ParamDecl)) {
      diag(ParamDecl->getLocation(),
         "%0 An Array of user defined type %1 in function %2 cannot be used "
         " as an axi slave stream or as a fifo. Use hls::stream<%1> instead",
         DiagnosticIDs::Error)
        << "[Clang7-Core-Subset]" << ParamDecl << FuncDecl;
    }
#endif

  }
  return;
}

} // namespace xilinx
} // namespace tidy
} // namespace clang
