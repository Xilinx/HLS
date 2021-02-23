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
/// \file
/// \brief This file implements parsing of all Xilinx directives and clauses.
/// '#pragma HLS|AP|AUTOPILOT kind named-arguments'.
///  kind:
///    identifer
///  named-arguments:
///    named-argument named-arguments[opt]
///
///  named-argument:
///    identifer
///    identifer '=' identifer-list
///    identifer '=' integer
///
///  identifer-list:
///    identifer
///    identifer ',' identifer-list[opt]
///
///
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/Lex/LexDiagnostic.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Parse/Parser.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/SemaInternal.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/XILINXFPGAPlatformBasic.h"
#include "clang/Basic/HLSDiagnostic.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <streambuf>
#include <string>

using namespace clang;
using namespace llvm;

// TODO, following code is very ugly , FIXME
template <typename Pred>
static void TransferAttributes(ParsedAttributes &To, ParsedAttributes &From,
                               Pred ShouldTransfer) {
  AttributeList *CheckedList = nullptr;
  while (auto *Cur = From.getList()) {
    // Take out Cur from attrs
    auto *Next = Cur->getNext();
    From.set(Next);

    if (!ShouldTransfer(Cur)) {
      // Put Cur to CheckedList
      Cur->setNext(CheckedList);
      CheckedList = Cur;
      continue;
    }

    // Otherwise add to the To
    Cur->setNext(nullptr);
    To.clone(*Cur);
  }

  // Put the checked list back to From.
  From.set(CheckedList);
}

static bool ShouldSinkFromLabel(AttributeList *A) {
  switch (A->getKind()) {
  default:
    return false;
  case AttributeList::AT_OpenCLUnrollHint:
  case AttributeList::AT_XCLPipelineLoop:
  case AttributeList::AT_XCLDataFlow:
  case AttributeList::AT_XCLFlattenLoop:
  case AttributeList::AT_XCLLoopTripCount:
  case AttributeList::AT_XCLLatency:
  case AttributeList::AT_XlxDependence:
  case AttributeList::AT_XlxStable:
  case AttributeList::AT_XlxStableContent:
  case AttributeList::AT_XlxShared:
  case AttributeList::AT_XlxDisaggr:
  case AttributeList::AT_XlxAggregate:
    return true;
  }
}
/*
 * sink attribute wrapping the label to the stmt
 * __attribute__((xlx_latency))
 * label_name:
 * {
 *   a = b * c;
 *   b = a * d;
 * }
 *
 * will sink xlx_latency to CompoundStmt after "label_name"
 *
 */
void Parser::SinkLabelAttributes(ParsedAttributesWithRange &To,
                                 ParsedAttributesWithRange &From,
                                 const Token &IdentTok) {
  TransferAttributes(To, From, ShouldSinkFromLabel);

  auto *AttrName = getPreprocessor().getIdentifierInfo("xcl_region");
  auto Loc = IdentTok.getLocation();
  ArgsUnion Arg[] = {IdentifierLoc::create(
      Actions.Context, IdentTok.getLocation(), IdentTok.getIdentifierInfo())};
  To.addNew(AttrName, Loc, nullptr, Loc, Arg, array_lengthof(Arg),
            AttributeList::AS_GNU);
}

// Only for internal debugging usage:
static bool enableXilinxPragmaChecker() {
  char *tmp = std::getenv("XILINX_SCOUT_HLS_DISABLE_PRAGMA_CHECKER");
  if (tmp == NULL)
    return true;
  std::string getEnvInfo = "";
  getEnvInfo = tmp;
  if (getEnvInfo == "yes")
    return false;
  else
    return true;
}

static bool HasDataflowAttributeInternal(AttributeList *List) {
  bool HasDataflow = false;
  for (auto *A = List; A; A = A->getNext())
    HasDataflow |= A->isXCLDataflowAttribute();
  return HasDataflow;
}

bool Parser::HasDataflowAttribute(AttributeList *List) {
  return HasDataflowAttributeInternal(List);
}

static bool ShouldHoistFromScope(AttributeList *A) {
  switch (A->getKind()) {
  default:
    return false;
  case AttributeList::AT_OpenCLUnrollHint:
  case AttributeList::AT_XCLDataFlow:
  case AttributeList::AT_XCLPipelineLoop:
  case AttributeList::AT_XlxDependence:
  case AttributeList::AT_XCLFlattenLoop:
  case AttributeList::AT_XCLLoopTripCount:
    return true;
  }
}

void Parser::RemoveDataflowAttribute(ParsedAttributes &From) {
  ParsedAttributes EmptyAttrs(From.getPool().getFactory());
  TransferAttributes(EmptyAttrs, From, [](AttributeList *A) {
    return A->getKind() == AttributeList::AT_XCLDataFlow ? true : false;
  });
  auto A = EmptyAttrs.getList();
  while (A) {
    this->Diag(A->getLoc(), diag::warn_remove_xcl_dataflow_attr)
        << FixItHint::CreateRemoval(A->getRange());
    A = A->getNext();
  }
}

void Scope::hoistParsedHLSPragmas(ParsedAttributes &Dst) {
  TransferAttributes(Dst, ParsedHLSPragmas, ShouldHoistFromScope);
}

static bool IsContinue(Scope *S) {
  return S->getFlags() & Scope::ContinueScope;
}

static bool IsHoistScope(Scope *&S) {
  if (!S->isCompoundStmtScope())
    return false;

  // Hoist to continue scope, e.g. the "for/while/do" statement
  S = S->getParent();
  while (S && !IsContinue(S) && !S->isCompoundStmtScope())
    S = S->getParent();

  // Do not hoist across compound statement scope
  if (!S || S->isCompoundStmtScope())
    return false;

  return true;
}

void Scope::hoistParsedHLSPragmas() {
  auto P = this;
  if (IsHoistScope(P)) {
    hoistParsedHLSPragmas(P->getParsedHLSPragmasRef());
  }
}

static AttributeList *IsRegionUnrollScope(AttributeList *A) {
  while (A) {
    if (A->getKind() == AttributeList::AT_XlxUnrollRegionHint)
      return A;
    A = A->getNext();
  }
  return nullptr;
}

/// SinkParsedHLSUnrollPragmas - Sink parsed HLS region unroll pragmas from
/// parent scope to subloops
void Parser::SinkParsedHLSUnrollPragmas(ParsedAttributesWithRange &To,
                                        Scope *P) {
  auto *RegionUnrollAttr = IsRegionUnrollScope(P->getParsedHLSPragmas());
  if (RegionUnrollAttr && IsHoistScope(P)) {
    auto ArgNum = RegionUnrollAttr->getNumArgs();
    ArgsVector Args;
    for (unsigned i = 0; i < ArgNum; i++)
      Args.emplace_back(RegionUnrollAttr->getArg(i));
    auto *AttrName = getPreprocessor().getIdentifierInfo("opencl_unroll_hint");

    To.addNew(AttrName, RegionUnrollAttr->getLoc(), nullptr,
              RegionUnrollAttr->getScopeLoc(), &Args[0], ArgNum,
              AttributeList::AS_GNU);
  }
}

//===----------------------------------------------------------------------===//
// Xilinx declarative directives.
//===----------------------------------------------------------------------===//

class XlxPragmaArgParser;

typedef ArgsUnion  (*CallBackParserFunc)(XlxPragmaArgParser& PAP, Parser &P, SourceLocation PragmaLoc);
/// it is used to hold pragma param value
struct XlxPragmaParam {
  enum Type {
    Unknown = 0,
    Id,
    Enum,
    ICEExpr,
    VarRefExpr,
    Present /*TODO, delete it,  replace with PresentID*/,
    PresentID,
    CallBackParser,
  };

  Type T = Unknown;
  bool Required = false;

  union {
    StringRef S;
    int64_t Int;
    // TODO, rename it to iceValue,  it is only used to restore ICE option
    Expr *VarRef;

    // it is used to help check error , if pragma param  can be "value_a,
    // value_b, and value_c, ...." these values are exclusive for each other,
    // given these present("value_x" , exlcuded_gorup_id ) XlxParser::parse will
    // check it , avoid some error such as #pragma name,  value_a, value_c
    unsigned PresentGroup;
    CallBackParserFunc callback;
  };
  SmallVector<StringRef, 4> EnumVals;

  XlxPragmaParam():T(Unknown) {}

  XlxPragmaParam(bool Required, StringRef S)
      : T(Id), Required(Required), S(S) {}

  XlxPragmaParam(bool Required, int64_t Int)
      : T(ICEExpr), Required(Required), Int(Int) {}

  XlxPragmaParam(unsigned Group)
      : T(Present), Required(false), PresentGroup(Group) {}

  XlxPragmaParam(bool Required, std::initializer_list<StringRef> Vals)
      : T(Enum), Required(Required), EnumVals(Vals) {}

  XlxPragmaParam(bool Required, Expr *expr)
      : T(VarRefExpr), Required(Required), VarRef(expr) {}

  XlxPragmaParam(Type type, unsigned Group) : T(type), PresentGroup(Group) {}

  XlxPragmaParam(bool Required, CallBackParserFunc callback) 
      : T(CallBackParser), Required(Required), callback(callback) { }
};

static std::pair<StringRef, XlxPragmaParam> reqId(StringRef Name) {
  return {Name, XlxPragmaParam(true, "")};
}

static std::pair<StringRef, XlxPragmaParam> optId(StringRef Name,
                                                  StringRef S = "") {
  return {Name, XlxPragmaParam(false, S)};
}

static std::pair<StringRef, XlxPragmaParam> reqICEExpr(StringRef Name,
                                                       int64_t i = 0) {
  return {Name, XlxPragmaParam(true, i)};
}

static std::pair<StringRef, XlxPragmaParam> optICEExpr(StringRef Name,
                                                       int64_t i = 0) {
  return {Name, XlxPragmaParam(false, i)};
}

static std::pair<StringRef, XlxPragmaParam> reqVarRefExpr(StringRef Name,
                                                          Expr *e = nullptr) {
  return {Name, XlxPragmaParam(true, (Expr *)NULL)};
}

static std::pair<StringRef, XlxPragmaParam> optVarRefExpr(StringRef Name,
                                                          Expr *e = nullptr) {
  return {Name, XlxPragmaParam(false, (Expr *)NULL)};
}

static std::pair<StringRef, XlxPragmaParam> present(StringRef Name,
                                                    unsigned Group = 0) {
  return {Name, XlxPragmaParam(Group)};
}
static std::pair<StringRef, XlxPragmaParam> presentId(StringRef Name,
                                                      unsigned Group = 0) {
  return {Name, XlxPragmaParam(XlxPragmaParam::PresentID, Group)};
}

static std::pair<StringRef, XlxPragmaParam> optCallBackParser(StringRef Name, CallBackParserFunc callback) { 
  return {Name, XlxPragmaParam(false, callback)};
}
static std::pair<StringRef, XlxPragmaParam> reqCallBackParser(StringRef Name, CallBackParserFunc callback) { 
  return {Name, XlxPragmaParam(true,  callback)};
}

static std::pair<StringRef, XlxPragmaParam>
optEnum(StringRef Name, std::initializer_list<StringRef> Vals) {
  return {Name, XlxPragmaParam(false, Vals)};
}

typedef SmallVector<std::pair<Decl *, SourceLocation>, 4> SubjectListTy;

class XlxPragmaArgParser {
  Parser &P;
  Preprocessor &PP;
  ParsedAttributes &ScopeAttrs;
  ParsedAttributes &DependenceAttrs;

  // TODO, SubjectList is very ugly design, we should not do Sematic action :
  // binding attribute with variable by process
  // now, it is only used by Interface pragma parsing
  SubjectListTy &SubjectList;

  bool ApplyToFunction;

  StringMap<XlxPragmaParam> NamedParams;
  SmallVector<StringRef, 4> ParamList;

  // this is used to help parse  some special pragma param
  // some pragma param will impact the pragma parse behavior
  // currently example"interface::mode" pragma param will affect
  // the set of valid param values for other pragma params
  StringRef SubjectParam;
  StringMap<ArgsUnion> ArgMap;
  StringSet<> Presented;
  StringMap<IdentifierLoc *> PresentedID;

  SourceLocation PragmaLoc;
  SourceRange PragmaRange;

  VarDecl *parseSubject();

  bool collectArguments(ArgsVector &Args);

public:
  XlxPragmaArgParser(
      Parser &P, Scope *CurScope, StringRef SubjectParam, bool ApplyToFunction,
      std::initializer_list<std::pair<StringRef, XlxPragmaParam>> List,
      SourceLocation PragmaLoc, SubjectListTy &Subjects)
      : P(P), PP(P.getPreprocessor()),
        ScopeAttrs(CurScope->getParsedHLSPragmasRef()),
        DependenceAttrs(CurScope->getDependencePragmasRef()),
        SubjectList(Subjects), ApplyToFunction(ApplyToFunction),
        NamedParams(List), SubjectParam(SubjectParam), PragmaLoc(PragmaLoc) {
    for (const auto &P : List)
      ParamList.push_back(P.first);
  }

  AttributeList *createAttribute(StringRef Name,
                                 MutableArrayRef<ArgsUnion> Args,
                                 AttributeList *List = nullptr) {
    auto *II = PP.getIdentifierInfo(Name);
    auto &Pool = ScopeAttrs.getPool();
    auto *A = Pool.create(II, PragmaRange, nullptr, PragmaLoc, Args.data(),
                          Args.size(), AttributeList::AS_GNU);
    A->setNext(List);
    return A;
  }

  void addAttribute(StringRef Name, MutableArrayRef<ArgsUnion> Args) {
    ScopeAttrs.add(createAttribute(Name, Args));
  }

  void addDependenceAttribute(StringRef Name, MutableArrayRef<ArgsUnion> Args) {
    DependenceAttrs.add(createAttribute(Name, Args));
  }

  bool parse();
  bool parse(ArgsVector &Args) {
    if (!parse())
      return false;

    return collectArguments(Args);
  }

  IdentifierLoc *parseIdentifierLoc();
  IdentifierLoc *parseEnumIdentifier(const XlxPragmaParam &param);
  Expr *parseICEExpression();
  Expr *parseVarRefExpression(StringRef optionName);

  ArrayRef<std::pair<Decl *, SourceLocation>> subjects() const {
    return SubjectList;
  }

  IdentifierLoc *createIdentLoc(StringRef S,
                                SourceLocation Loc = SourceLocation()) const {
    auto &Ctx = P.getActions().getASTContext();

    auto Ident = PP.getIdentifierInfo(S);
    return IdentifierLoc::create(Ctx, Loc, Ident);
  }

  IdentifierLoc *createIdentLoc(IdentifierInfo *II,
                                SourceLocation Loc = SourceLocation()) const {
    auto &Ctx = P.getActions().getASTContext();
    return IdentifierLoc::create(Ctx, Loc, II);
  }

  IntegerLiteral *
  createIntegerLiteral(int64_t i, SourceLocation Loc = SourceLocation()) const {
    auto &Ctx = P.getActions().getASTContext();
    auto IntTy = Ctx.IntTy;
    auto Width = Ctx.getIntWidth(IntTy);
    auto Int = APInt(Width, i);
    return IntegerLiteral::Create(Ctx, Int, IntTy, Loc);
  }

  ArgsUnion lookup(StringRef Param) const { return ArgMap.lookup(Param); }

  ArgsUnion operator[](StringRef Param) const {
    if (auto Arg = ArgMap.lookup(Param))
      return Arg;

    auto ParamInfo = NamedParams.lookup(Param);
    // TODO, report diagnostic error message
    assert(!ParamInfo.Required && "Missing required argument!");

    auto Loc = SourceLocation();
    // Create the default value
    switch (ParamInfo.T) {
    case XlxPragmaParam::Id:
      return createIdentLoc(ParamInfo.S, Loc);
    case XlxPragmaParam::Enum:
      return createIdentLoc(ParamInfo.EnumVals[0], Loc);
    case XlxPragmaParam::ICEExpr:
      return createIntegerLiteral(ParamInfo.Int, Loc);
    case XlxPragmaParam::VarRefExpr:
      return ArgsUnion((Expr *)nullptr);
    case XlxPragmaParam::Present:
      // TODO, "present" class of option should be evaluated as "IdentifierLoc"
      // because Sema need IdentifierLoc to report precise source location, and
      // name
      return createIntegerLiteral(presented(Param) ? 1 : 0, Loc);
    default:
      llvm_unreachable("unexpected  ParamInfo.Type");
    }
  }

  bool parseSubjectList();

  //TODO, delete presented /presentedId 
  //we can use "lookup("present_name" ) to return the presented IdentifierLoc
  bool presented(StringRef Name) const {
    assert(NamedParams.lookup(Name).T == XlxPragmaParam::Present &&
           "Wrong type!");
    return Presented.count(Name);
  }

  IdentifierLoc *presentedId(StringRef Name) const {
    return PresentedID.lookup(Name);
  }
  bool CheckAndFilter(StringMap<XlxPragmaParam> ParamMap);
};


static void getSubExprOfVariable(SmallVector<Expr *, 4> &subExprs, Expr *var_expr,
                                 Parser &P) {
  // parse "expr1, expr2, expr3"
  while (isa<BinaryOperator>(var_expr) &&
         dyn_cast<BinaryOperator>(var_expr)->getOpcode() == BO_Comma) {

    auto bin_expr = dyn_cast<BinaryOperator>(var_expr);
    assert(!isa<BinaryOperator>(bin_expr->getRHS()) && "unexpected, it is not valid variable expressions format");

    Expr *leaf = bin_expr->getRHS();
    // stupid test case using  "variable = &stream_variable", I don't know why
    if (isa<UnaryOperator>(leaf) &&
        dyn_cast<UnaryOperator>(leaf)->getOpcode() == UO_AddrOf) {
      UnaryOperator *up = dyn_cast<UnaryOperator>(leaf);
      leaf = up->getSubExpr();
      P.Diag(up->getExprLoc(), diag::warn_extra_tokens_before_variable_expression)
        << FixItHint::CreateRemoval(up->getExprLoc());
    }
    if (isa<ImplicitCastExpr>(leaf)) {
      ImplicitCastExpr *cast = dyn_cast<ImplicitCastExpr>(leaf);
      leaf = cast->getSubExpr();
    }
    subExprs.push_back(leaf);
    var_expr = bin_expr->getLHS();
  }

  if (isa<UnaryOperator>(var_expr) &&
      dyn_cast<UnaryOperator>(var_expr)->getOpcode() == UO_AddrOf) {
    UnaryOperator *up = dyn_cast<UnaryOperator>(var_expr);
    P.Diag(up->getExprLoc(), diag::warn_extra_tokens_before_variable_expression)
      << FixItHint::CreateRemoval(up->getExprLoc());
    var_expr = up->getSubExpr();
  }

  if (isa<ImplicitCastExpr>(var_expr)) {
    ImplicitCastExpr *cast = dyn_cast<ImplicitCastExpr>(var_expr);
    var_expr = cast->getSubExpr();
  }
  subExprs.push_back(var_expr);
  //reverse the order of subExprs , because COMMA_ operator  is Left Hand Side first 
  for (int i = 0; i < subExprs.size() / 2; i++) { 
    std::swap(subExprs[i], subExprs[ subExprs.size() - 1 - i ]);
  }
}

static StringRef str(const IdentifierLoc *Id) { return Id->Ident->getName(); }

static StringRef str(const IdentifierLoc &Id) { return Id.Ident->getName(); }

static IdentifierLoc ParseXlxPragmaArgument(Parser &P) {
  const auto &Tok = P.getCurToken();
  auto &PP = P.getPreprocessor();

  // Return null when hit pragma end
  if (Tok.is(tok::annot_pragma_XlxHLS_end))
    return {SourceLocation(), nullptr};

  // Do not fail on #pragma HLS inline
  if (!Tok.isOneOf(tok::identifier, tok::kw_inline, tok::kw_register,
                   tok::kw_auto, tok::kw_false, tok::kw_true)) {
    P.Diag(Tok, diag::warn_unexpected_token_in_pragma_argument)
        << PP.getSpelling(Tok) << tok::identifier;
    return {SourceLocation(), nullptr};
  }

  auto Name = Tok.getIdentifierInfo();
  auto Loc = P.ConsumeToken();

  return {Loc, Name};
}

static IdentifierLoc TryConsumeWords(Parser &P, ArrayRef<StringRef> Words) {
  auto &Tok = P.getCurToken();
  if (Tok.isNot(tok::identifier))
    return {SourceLocation(), nullptr};

  auto *II = Tok.getIdentifierInfo();

  if (llvm::any_of(Words,
                   [II](StringRef S) { return S.equals_lower(II->getName()); }))
    return {P.ConsumeToken(), II};

  return {SourceLocation(), nullptr};
}

Expr *XlxPragmaArgParser::parseVarRefExpression(StringRef optionName) {
  auto &Actions = P.getActions();

  const auto &Tok = P.getCurToken();
  // ExprResult ArgExpr = P.ParseAssignmentExpression();
  // ExprResult ArgExpr = P.ParseExpression();

  ExprResult ArgExpr = P.ParseHLSVariableExpression(optionName);

  ArgExpr = Actions.CorrectDelayedTyposInExpr(ArgExpr);
  if (!ArgExpr.isInvalid()) {
    Expr *expr = ArgExpr.get();

    return expr;
  } else {
    return nullptr;
  }
}

VarDecl *XlxPragmaArgParser::parseSubject() {
  // Skip address_of or dererference
  SourceLocation Loc;
  bool SkippedSomthing =
      P.TryConsumeToken(tok::amp, Loc) || P.TryConsumeToken(tok::star, Loc);

  auto &Actions = P.getActions();
  ExprResult ArgExpr = P.ParseAssignmentExpression();
  ArgExpr = Actions.CorrectDelayedTyposInExpr(ArgExpr);
  auto *DeclRef = dyn_cast_or_null<DeclRefExpr>(ArgExpr.get());
  if (!DeclRef)
    return nullptr;

  if (auto *D = dyn_cast<VarDecl>(DeclRef->getDecl())) {
    if (SkippedSomthing)
      P.Diag(Loc, diag::warn_extra_tokens_before_variable_expression)
          << FixItHint::CreateRemoval(Loc);
    return D;
  }

  return nullptr;
}

IdentifierLoc *
XlxPragmaArgParser::parseEnumIdentifier(const XlxPragmaParam &Param) {
  auto &Tok = P.getCurToken();
  if (!Tok.isOneOf(tok::identifier, tok::string_literal, tok::kw_auto)) {
    P.Diag(Tok, diag::warn_unexpected_token_in_pragma_argument)
        << PP.getSpelling(Tok) << tok::identifier;
    return nullptr;
  }

  IdentifierInfo *Ident;
  if (Tok.is(tok::string_literal)) {
    StringLiteralParser X(Tok, P.getPreprocessor());
    StringRef str = X.hadError ? "" : X.GetString();
    Ident = P.getPreprocessor().getIdentifierInfo(str);
  } else if (Tok.is(tok::kw_auto)) {
    Ident = P.getPreprocessor().getIdentifierInfo("auto");
  } else
    Ident = Tok.getIdentifierInfo();

  auto *IdLoc = createIdentLoc(Ident, Tok.getLocation());
  P.ConsumeAnyToken();

  auto EnumVal = str(IdLoc);
  std::string EnumStr;
  if (llvm::none_of(Param.EnumVals, [EnumVal, &EnumStr](StringRef S) {
        EnumStr += S.str() + '/';
        return S.equals_lower(EnumVal);
      })) {
    P.Diag(IdLoc->Loc, diag::warn_unexpected_token_in_pragma_argument)
        << EnumVal << EnumStr;
    return nullptr;
  }
  return IdLoc;
}

IdentifierLoc *XlxPragmaArgParser::parseIdentifierLoc() {
  auto &Tok = P.getCurToken();
  if (!Tok.isOneOf(tok::identifier, tok::string_literal, tok::kw_auto)) {
    P.Diag(Tok, diag::warn_unexpected_token_in_pragma_argument)
        << PP.getSpelling(Tok) << tok::identifier;
    return nullptr;
  }

  IdentifierInfo *Ident;
  if (Tok.is(tok::string_literal)) {
    StringLiteralParser X(Tok, P.getPreprocessor());
    StringRef str = X.hadError ? "" : X.GetString();
    Ident = P.getPreprocessor().getIdentifierInfo(str);
  } else if (Tok.is(tok::kw_auto)) {
    Ident = P.getPreprocessor().getIdentifierInfo("auto");
  } else
    Ident = Tok.getIdentifierInfo();

  auto *IL = createIdentLoc(Ident, Tok.getLocation());
  P.ConsumeAnyToken();

  return IL;
}

// following function is important  to assume that
// attribute's option is constant scalar in CodeGen
Expr *XlxPragmaArgParser::parseICEExpression() {
  auto &Actions = P.getActions();
  ExprResult ArgExpr = P.ParseConstantExpression();
  ArgExpr = Actions.CheckOrBuildPartialConstExpr(ArgExpr.get());
  ArgExpr = Actions.CorrectDelayedTyposInExpr(ArgExpr);
  return ArgExpr.get();
}

bool XlxPragmaArgParser::parseSubjectList() {
  auto &Actions = P.getActions();

  auto &Tok = P.getCurToken();
  auto Loc = Tok.getLocation();

  // Return means the parent function
  if (P.TryConsumeToken(tok::kw_return)) {
    if (!ApplyToFunction) {
      P.Diag(Tok, diag::warn_unexpected_token_in_pragma_argument)
          << tok::kw_return << tok::identifier;
      return false;
    }

    auto *FD = Actions.getCurFunctionDecl();
    if (!FD)
      return false;

    SubjectList.emplace_back(FD, Loc);
  } else if (P.TryConsumeToken(tok::kw_void))
    /*P.Diag(Loc, diag::warn_extra_pragma_hls_token_ignored)*/;
  else if (auto *Var = parseSubject())
    SubjectList.emplace_back(Var, Loc);
  else
    return false;

  if (P.TryConsumeToken(tok::comma))
    return parseSubjectList();

  return true;
}

bool XlxPragmaArgParser::CheckAndFilter(StringMap<XlxPragmaParam> ParamMap) 
{
  for( auto &kv : ArgMap) {
    StringRef name = kv.getKey();
    ArgsUnion arg = kv.getValue();
    if (arg.isNull()){
      //it is prsentedId
      continue;
    }
    auto parm = ParamMap.lookup( name);
    switch(parm.T) 
    {
      case XlxPragmaParam::Unknown:
      {
        SourceLocation loc ; 
        if (arg.is<IdentifierLoc*>()) 
          loc = arg.get<IdentifierLoc*>()->Loc;
        else if (arg.is<Expr*>()) { 
          loc = arg.get<Expr*>()->getExprLoc();
        }
        P.Diag(loc, diag::warn_unexpected_pragma_parameter) << name;
      //Diagnostic
        return false;
      }
      case XlxPragmaParam::Id: 
      case XlxPragmaParam::Enum:
      case XlxPragmaParam::ICEExpr:
      case XlxPragmaParam::VarRefExpr:
      break;
    }
  }
  for( auto &kv : PresentedID){
    StringRef name = kv.getKey();
    IdentifierLoc* arg = kv.getValue();
    auto parm = ParamMap.lookup( name);
    switch(parm.T) 
    {
      case XlxPragmaParam::Unknown:
      //Diagnostic
        P.Diag(arg->Loc, diag::warn_unexpected_pragma_parameter) << name;
        return false;
      case XlxPragmaParam::Id: 
      case XlxPragmaParam::Enum:
      case XlxPragmaParam::ICEExpr:
      case XlxPragmaParam::VarRefExpr:
        //Diagnostic
        return true;
      case XlxPragmaParam::PresentID:
        continue;
        break;
    }
  }
  return true;
}

// parse all pragma according NamedParams
// and save result into  ArgMap
// FYI,  Subject, Enum, ICEExpr, VarRefExpr  using synatx grammar: name = value
// while present is used  value, no  tok::equal
bool XlxPragmaArgParser::parse() {
  DenseMap<unsigned, IdentifierLoc> PresentGroups;
  while (P.getCurToken().isNot(tok::annot_pragma_XlxHLS_end)) {
    // Parse each named argument:
    ///  named-argument:
    ///    identifer
    ///    identifer '=' identifer
    ///    identifer '=' integer
    auto MaybeArg = ParseXlxPragmaArgument(P);
    if (!MaybeArg.Ident)
      return false;

    auto ArgName = str(MaybeArg);

    if (ArgName.equals_lower(SubjectParam)) {
      if (!P.TryConsumeToken(tok::equal)) {
        const auto &Tok = P.getCurToken();
        P.Diag(Tok, diag::warn_unexpected_token_in_pragma_argument)
            << PP.getSpelling(Tok) << tok::equal;
        return false;
      }

      if (!parseSubjectList())
        return false;

      continue;
    }

    const auto &Param = NamedParams.lookup(ArgName.lower());
    auto T = Param.T;
    if (T == XlxPragmaParam::Unknown) {
      P.Diag(MaybeArg.Loc, diag::warn_unexpected_pragma_parameter) << ArgName;
      return false;
    }

    auto &Arg = ArgMap[ArgName.lower()];
    if (!Arg.isNull()) {
      P.Diag(MaybeArg.Loc, diag::warn_repeated_pragma_parameter) << ArgName;
      return false;
    }

    if (T == XlxPragmaParam::Present) {
      if (!Presented.insert(ArgName.lower()).second) {
        P.Diag(MaybeArg.Loc, diag::warn_repeated_pragma_parameter) << ArgName;
        return false;
      }

      // Check mutual exclusive options
      if (Param.PresentGroup) {
        auto r = PresentGroups.insert({Param.PresentGroup, MaybeArg});
        if (!r.second) {
          P.Diag(MaybeArg.Loc, diag::warn_confilict_pragma_parameter)
              << ArgName << str(PresentGroups[Param.PresentGroup]);
          return false;
        }
      }
      continue;
    }

    if (T == XlxPragmaParam::PresentID) {
      if (PresentedID.count(ArgName.lower())) {
        P.Diag(MaybeArg.Loc, diag::warn_repeated_pragma_parameter) << ArgName;
        return false;
      }

      // Check mutual exclusive options
      if (Param.PresentGroup) {
        auto r = PresentGroups.insert({Param.PresentGroup, MaybeArg});
        if (!r.second) {
          P.Diag(MaybeArg.Loc, diag::warn_confilict_pragma_parameter)
              << ArgName << str(PresentGroups[Param.PresentGroup]);
          return false;
        }
      }
      PresentedID.insert(std::make_pair(
          ArgName.lower(), createIdentLoc(MaybeArg.Ident, MaybeArg.Loc)));
      continue;
    }

    //// ID, Enum, Expression Parameter are all with following uniform format
    ///    identifer '=' identifer
    ///    identifer '=' integer
    /// We should see a '='
    if (!P.TryConsumeToken(tok::equal)) {
      const auto &Tok = P.getCurToken();
      if (Tok.getKind() == tok::annot_pragma_XlxHLS_end) { 
        P.Diag(Tok, diag::warn_unexpected_token_in_pragma_argument)
           << "End Of Pramga Line" << tok::equal;
      }
      else { 
        P.Diag(Tok, diag::warn_unexpected_token_in_pragma_argument)
           << PP.getSpelling(Tok) << tok::equal;
      }
      return false;
    }

    switch (T) {
    case XlxPragmaParam::Enum:
      Arg = parseEnumIdentifier(Param);
      break;
    case XlxPragmaParam::Id:
      Arg = parseIdentifierLoc();
      break;
    case XlxPragmaParam::ICEExpr:
      Arg = parseICEExpression();
      break;
    case XlxPragmaParam::VarRefExpr:
      Arg = parseVarRefExpression(ArgName);
      break;
    case XlxPragmaParam::CallBackParser:
      Arg = Param.callback(*this, P, PragmaLoc);
      break;
    default:
      llvm_unreachable("Unexpected type");
      break;
    }
    if (Arg.isNull())
      return false;
  }

  // Check the subject
  if (SubjectList.empty() && !SubjectParam.empty()) {
    P.Diag(P.getCurToken(), diag::warn_pragma_named_argument_missing)
        << SubjectParam;
    return false;
  }

  // Check required arguments
  for (const auto &Param : NamedParams) {
    if (Param.second.Required && !ArgMap.count(Param.first())) {
      P.Diag(PragmaLoc, diag::warn_pragma_named_argument_missing)
          << Param.first();
      return false;
    }
  }

  PragmaRange = SourceRange(PragmaLoc, P.getEndOfPreviousToken());
  return true;
}

bool XlxPragmaArgParser::collectArguments(ArgsVector &Args) {
  // Collect the argument list
  for (auto Param : ParamList) {
    if (auto Arg = this->operator[](Param)) {
      Args.push_back(Arg);
      continue;
    }

    return false;
  }

  return true;
}

static SourceLocation FinishPragmaHLS(Parser &P) {
  P.SkipUntil(tok::annot_pragma_XlxHLS_end, Parser::StopBeforeMatch);
  // Consume tok::annot_pragma_XlxHLS_end terminator.
  return P.ConsumeAnyToken();
}

static bool IsHLSStreamType(VarDecl *Var) {
  if (!Var)
    return false;
  auto Ty = isa<ParmVarDecl>(Var)
                ? cast<ParmVarDecl>(Var)->getOriginalType().getCanonicalType()
                : Var->getType().getCanonicalType();

  auto *BTy = Ty->isReferenceType() ? Ty->getPointeeType().getTypePtr()
                                    : Ty->getPointeeOrArrayElementType();

  if (BTy->isClassType() && !BTy->getAsCXXRecordDecl()
                                 ->getCanonicalDecl()
                                 ->getQualifiedNameAsString()
                                 .compare("hls::stream"))
    return true;

  // hls::stream<type> with template in type
  if (dyn_cast<TemplateSpecializationType>(BTy)) {
    TemplateName name =
        dyn_cast<TemplateSpecializationType>(BTy)->getTemplateName();
    if (TemplateDecl *decl = name.getAsTemplateDecl()) {
      std::string base_name = decl->getQualifiedNameAsString();
      if (base_name == "hls::stream")
        return true;
    }
  }
  return false;
}

static void HandleXlxDataflowPragma(Parser &P, Scope *CurScope,
                                    SourceLocation PragmaLoc) {
  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(
      P, CurScope, "", false,
      {present("interval"), present("disable_start_propagation")}, PragmaLoc,
      SubjectList);
  if (!PAP.parse())
    return;
  if (enableXilinxPragmaChecker()) {
    if (PAP.lookup("interval")) {
      auto ii = PAP.lookup("interval").get<IdentifierLoc *>();
      P.Diag(ii->Loc, diag::warn_extra_pragma_hls_token_ignored) << "interval"
                                                                 << "dataflow";
    }
  }

  auto PropagationType = "start_propagation";
  if (PAP.presented("disable_start_propagation"))
    PropagationType = "disable_start_propagation";

  ArgsUnion Type = PAP.createIdentLoc(PropagationType);
  PAP.addDependenceAttribute("xcl_dataflow", Type);

  return;
}

static void HandleXlxPipelinePragma(Parser &P, Scope *CurScope,
                                    SourceLocation PragmaLoc) {
  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(P, CurScope, "", false,
                         {optICEExpr("ii", -1), present("rewind"),
                          present("enable_flush"), present("off"),
                          optEnum("style", {"stp", "flp", "frp"})},
                         PragmaLoc, SubjectList);
  if (!PAP.parse())
    return;

  // Obtain style option value
  IdentifierLoc *StyleMode = nullptr;
  if (auto Sty = PAP.lookup("style"))
    StyleMode = Sty.get<IdentifierLoc *>();

  if (StyleMode) {
    // Checkers for conflict
    if (PAP.presented("off")) {
      // error out
      P.Diag(P.getCurToken().getLocation(), diag::error_pipeline_style_conflict)
          << "'off' option.";
    }
    if (PAP.presented("enable_flush")) {
      // error out
      P.Diag(P.getCurToken().getLocation(), diag::error_pipeline_style_conflict)
          << "'enable_flush' option.";
    }
    if (PAP.presented("rewind") && StyleMode->Ident->getName().equals_lower("stp") == false) {
      // error out
      P.Diag(P.getCurToken().getLocation(), diag::error_pipeline_style_conflict)
          << "'rewind' option.";
    }
  }

  // following with use style ID instead
  int64_t styleID = -1; // default value, which will be changed in LLVM
  if (PAP.presented("enable_flush")) {
    // enable_flush is same with style=flp, so we make them together
    styleID = 1; //same with style=flp
    // warning message to deprecate
    P.Diag(P.getCurToken().getLocation(),
           diag::warn_pipeline_enable_flush_deprecate);
  }

  if (StyleMode) {
    if (StyleMode->Ident->getName().equals_lower("stp"))
      styleID = 0;
    else if (StyleMode->Ident->getName().equals_lower("flp"))
      styleID = 1;
    else if (StyleMode->Ident->getName().equals_lower("frp"))
      styleID = 2;
  }

  ArgsUnion II = PAP["ii"];

  if (auto Off = PAP.presented("off"))
    II = ArgsUnion(PAP.createIntegerLiteral(0));

  ArgsUnion args[] = {II, PAP.createIntegerLiteral(styleID), PAP["rewind"]};
  PAP.addDependenceAttribute("xlx_pipeline", args);
}

static void HandleXlxUnrollPragma(Parser &P, Scope *CurScope,
                                  SourceLocation PragmaLoc) {
  auto *S = CurScope;
  if (!IsHoistScope(S)) {
    P.Diag(PragmaLoc, diag::warn_xlx_pragma_applied_in_wrong_scope)
        << "unroll" << 1;
    return;
  }

  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(P, CurScope, "", false,
                         {optICEExpr("factor", 0), present("region", 1),
                          present("skip_exit_check"), present("complete", 1),
                          present("partial", 1)},
                         PragmaLoc, SubjectList);
  if (!PAP.parse())
    return;

  // complete/partial is no longed supported
  if (enableXilinxPragmaChecker()) {
    if (PAP.presented("complete") || PAP.presented("partial") ||
        PAP.presented("region")) {
      P.Diag(P.getCurToken().getLocation(),
             diag::err_xlx_pragma_option_not_supported_by_HLS_WarnOut)
          << "Unroll"
          << "complete/partial/region";
    }
  }

  if (auto Arg = PAP.lookup("factor")) {
    if (auto *E = Arg.get<Expr *>()) {
      if (auto *I = dyn_cast<IntegerLiteral>(E)) {
        llvm::APInt Factor = I->getValue();
        if (Factor.getSExtValue() < 0 || Factor.getSExtValue() > 4294967295) {
          P.Diag(E->getExprLoc(), diag::err_xlx_pragma_invalid_unroll_factor)
              << "Option 'factor' is too big, the valid range is 0 ~ 2^32-1";
          return;
        }
      }
    }
  }
  ArgsUnion factor;

  if (PAP.lookup("factor")) {
    factor = PAP["factor"];
  }

  ArgsUnion Args[] = {factor, PAP["skip_exit_check"]};
  PAP.addDependenceAttribute("xlx_unroll_hint", Args);
}

static void HandleXlxFlattenPragma(Parser &P, Scope *CurScope,
                                   SourceLocation PragmaLoc) {
  auto *S = CurScope;
  if (!IsHoistScope(S)) {
    P.Diag(PragmaLoc, diag::warn_xlx_pragma_applied_in_wrong_scope)
        << "loop_flatten" << 1;
    return;
  }

  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(P, CurScope, "", false, {present("off")}, PragmaLoc,
                         SubjectList);
  if (!PAP.parse())
    return;

  auto FlattenOff = PAP["off"];
  PAP.addAttribute("xcl_flatten_loop", FlattenOff);

  return;
}

static void HandleXlxMergePragma(Parser &P, Scope *CurScope,
                                 SourceLocation PragmaLoc) {
  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(P, CurScope, "", false, {present("force")}, PragmaLoc,
                         SubjectList);
  if (!PAP.parse())
    return;

  auto Force = PAP["force"];
  PAP.addAttribute("xlx_merge_loop", Force);

  return;
}

static void HandleLoopTripCountPragma(Parser &P, Scope *CurScope,
                                      SourceLocation PragmaLoc) {
  auto *S = CurScope;
  if (!IsHoistScope(S)) {
    P.Diag(PragmaLoc, diag::warn_xlx_pragma_applied_in_wrong_scope)
        << "loop_tripcount" << 1;
    return;
  }

  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(
      P, CurScope, "", false,
      {optICEExpr("min", 0), reqICEExpr("max"), optICEExpr("avg", 0)},
      PragmaLoc, SubjectList);

  if (!PAP.parse())
    return;

  ArgsVector Args = {PAP["min"], PAP["max"]};
  if (PAP.lookup("avg"))
    Args.emplace_back(PAP["avg"]);
  PAP.addDependenceAttribute("xlx_loop_tripcount", Args);
}

static bool
IsInvalidCore(Parser &P, StringRef &CoreStr,
              llvm::ArrayRef<std::pair<Decl *, SourceLocation>> VarSet,
              SourceLocation PragmaLoc) {

  // Ignore axi related resource
  if (CoreStr.startswith_lower("axi")) {
    P.Diag(PragmaLoc, diag::warn_obsolete_pragma_replaced)
        << "#pragma HLS RESOURCE core=axi"
        << "#pragma HLS INTERFACE axi";
    return true;
  }

  // FIFO core only work on hls::stream
  if (CoreStr.startswith_lower("fifo"))
    for (auto &S : VarSet) {
      bool IsStream =
          isa<VarDecl>(S.first) && IsHLSStreamType(cast<VarDecl>(S.first));
      if (!IsStream) {
        P.Diag(S.second, diag::warn_implicit_hls_stream) << "resource";
        return true;
      }
    }

  return false;
}

static void HandleResourcePragma(Parser &P, Scope *CurScope,
                                 SourceLocation PragmaLoc) {
  // Just warning to user this old resource pragma will be deleted in future
  P.Diag(PragmaLoc,
         diag::warn_deprecated_pragma_ignored_by_scout_skip_strict_mode)
      << "Resource pragma"
      << "bind_op/bind_storage pragma";

  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(
      P, CurScope, "", true,
      {reqVarRefExpr("variable"), reqId("core"), optICEExpr("latency", -1),
       optEnum("ecc_mode", {"none", "encode", "decode", "both"}),
       present("auto", 1), present("distribute", 1), present("block", 1),
       present("uram", 1), optId("metadata")},
      PragmaLoc, SubjectList);
  if (!PAP.parse())
    return;

  IdentifierLoc *coreII = NULL;
  StringRef core_name;
  SourceLocation core_loc;

  coreII = PAP.lookup("core").get<IdentifierLoc *>();
  core_name = str(coreII);
  core_loc = coreII->Loc;

  if (IsInvalidCore(P, core_name, PAP.subjects(), PragmaLoc))
    return;

  // Deal with old feature: XPM_MEMORY
  if (core_name.equals_lower("xpm_memory")) {
    // Just warning to user this old option will be deleted in future
    P.Diag(P.getCurToken().getLocation(),
           diag::warn_deprecated_pragma_option_ignored_by_scout)
        << "xpm_memory"
        << "Resource"
        << "Bind_Storage Pragma";

    SmallString<32> Name("xpm_memory");
    unsigned bitselect = 0;
    if (PAP.presented("auto")) { /* do nothing */
    }
    if (PAP.presented("distribute"))
      bitselect = bitselect | 1;
    if (PAP.presented("block"))
      bitselect = bitselect | 2;
    if (PAP.presented("uram"))
      bitselect = bitselect | 4;

    switch (bitselect) {
    case 0: /* do nothing */
      break;
    case 1:
      Name += "_distribute";
      break;
    case 2:
      Name += "_block";
      break;
    case 4:
      Name += "_uram";
      break;
    default:
      P.Diag(PragmaLoc, diag::err_resource_pragma_xpm_memory_option_conflict);
    }
    core_name = Name;
  }

  // Obtain op+impl based on platform API via core_name
  const platform::PlatformBasic *xilinxPlatform =
      platform::PlatformBasic::getInstance();
  std::vector<std::pair<platform::PlatformBasic::OP_TYPE,
                        platform::PlatformBasic::IMPL_TYPE>>
      OpImpls = xilinxPlatform->getOpImplFromCoreName(core_name.str());
  for (auto om : OpImpls) {
    if (om.first == platform::PlatformBasic::OP_MEMORY) {
      Expr* var_expr = PAP["variable"].get<Expr*>();
        // parse "expr1, expr2, expr3"
      SmallVector<Expr *, 4> subExprs;
      getSubExprOfVariable(subExprs, var_expr, P);

      for (auto e : subExprs) {
        if (isa<clang::FunctionType>(e->getType().getTypePtr())) {
          P.getActions().Diag(e->getExprLoc(),
                              diag::err_xlx_attribute_invalid_option)
              << "return"
              << "variable";
          continue;
        }

        ArgsUnion Args[] = {e, PAP.createIntegerLiteral(om.first, coreII->Loc),
                            PAP.createIntegerLiteral(om.second, coreII->Loc),
                            PAP["latency"]};
        PAP.addDependenceAttribute("xlx_bind_storage", Args);
      }
    } else {
      Expr* var_expr = PAP["variable"].get<Expr*>();
      // parse "expr1, expr2, expr3"
      SmallVector<Expr *, 4> subExprs;
      getSubExprOfVariable(subExprs, var_expr, P);
      for (auto e : subExprs) {
        FunctionDecl *func_decl = nullptr;
        if (isa<DeclRefExpr>(e)) {
          Decl *decl = dyn_cast<DeclRefExpr>(e)->getDecl();
          if (isa<FunctionDecl>(decl)) {
            func_decl = dyn_cast<FunctionDecl>(decl);
          }
        }

        if (func_decl) {
          // handle IPCore resource, blackbox feature need it!!
          if (om.first != platform::PlatformBasic::OP_VIVADO_IP) {
            P.getActions().Diag(e->getExprLoc(),
                                diag::err_xlx_attribute_invalid_option)
                << "return"
                << "variable";
            continue;
          } else {
            auto opName = xilinxPlatform->getOpName(om.first);
            auto implName = xilinxPlatform->getImplName(om.second);
            ArgsUnion args[] = {PAP.createIdentLoc(opName, coreII->Loc),
                                PAP.createIdentLoc(implName, coreII->Loc)};
            auto *A = PAP.createAttribute("xlx_resource_ipcore", args);
            P.getActions().ProcessDeclAttributeList(CurScope, func_decl, A,
                                                    false);
          }
        } else {
          ArgsUnion Args[] = {
              e, PAP.createIntegerLiteral(om.first, coreII->Loc),
              PAP.createIntegerLiteral(om.second, coreII->Loc), PAP["latency"]};
          PAP.addDependenceAttribute("xlx_bind_op", Args);
        }
      }
    }
  }
}

static void HandleTopFunctionPragma(Parser &P, Scope *CurScope,
                                    SourceLocation PragmaLoc) {
  // Do not use class method as top function
  auto CurFn = P.getActions().getCurFunctionDecl();
  if (CurFn->isCXXClassMember()) {
    P.Diag(PragmaLoc, diag::err_top_pragma_appiled_in_wrong_scope);
    return;
  }

  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(P, CurScope, "", false, {optId("name")}, PragmaLoc,
                         SubjectList);

  ArgsVector Args;
  if (!PAP.parse(Args))
    return;

  PAP.addAttribute("sdx_kernel", Args);
}

static void HandleUpwardInlineFunctionPragma(Parser &P, Scope *CurScope,
                                             SourceLocation PragmaLoc,
                                             bool NoInline) {
  auto &PP = P.getPreprocessor();
  auto PragmaEndLoc = P.getEndOfPreviousToken();

  // noinline and always_inline only apply for function
  if (!CurScope->isFunctionScope())
    CurScope = CurScope->getFnParent();
  auto &Attrs = CurScope->getParsedHLSPragmasRef();

  if (NoInline) {
    auto *NoInlineII = PP.getIdentifierInfo("noinline");
    Attrs.addNew(NoInlineII, SourceRange(PragmaLoc, PragmaEndLoc), nullptr,
                 PragmaLoc, nullptr, 0, AttributeList::AS_GNU);
    return;
  }

  auto *AlwaysInline = PP.getIdentifierInfo("always_inline");
  Attrs.addNew(AlwaysInline, SourceRange(PragmaLoc, PragmaEndLoc), nullptr,
               PragmaLoc, nullptr, 0, AttributeList::AS_GNU);
}

static void HandleDownwardInlineFunctionPragma(Parser &P, Scope *CurScope,
                                               SourceLocation PragmaLoc,
                                               bool IsRecursive) {
  auto &PP = P.getPreprocessor();
  auto PragmaEndLoc = P.getEndOfPreviousToken();
  auto &Attrs = CurScope->getParsedHLSPragmasRef();

  auto *DownwardInline = PP.getIdentifierInfo("xcl_inline");
  auto &Ctx = P.getActions().getASTContext();
  ArgsUnion Recursive[] = {IntegerLiteral::Create(
      Ctx, APInt(Ctx.getIntWidth(Ctx.IntTy), IsRecursive), Ctx.IntTy,
      PragmaLoc)};

  Attrs.addNew(DownwardInline, SourceRange(PragmaLoc, PragmaEndLoc), nullptr,
               PragmaLoc, Recursive, 1, AttributeList::AS_GNU);
}

static void HandleInlinePragma(Parser &P, Scope *CurScope,
                               SourceLocation PragmaLoc) {
  // Parse Self|Region|All as scope
  auto InlineScope = TryConsumeWords(P, {"self", "region", "all"});
  auto RecursiveInline = TryConsumeWords(P, {"recursive"});
  auto InlineOff = TryConsumeWords(P, {"off"});

  const auto &Tok = P.getCurToken();
  while (Tok.isNot(tok::annot_pragma_XlxHLS_end)) {
    StringRef tokstr = Tok.isAnyIdentifier()
                           ? Tok.getIdentifierInfo()->getName()
                           : Tok.getName();
    P.Diag(Tok, diag::warn_extra_pragma_hls_token_ignored)
        << tokstr << "INLINE" << FixItHint::CreateRemoval(Tok.getLocation());
    P.ConsumeToken();
  }

  if (!InlineScope.Ident) {
    if (RecursiveInline.Ident) {
      // Inline everything in the compound statement
      HandleDownwardInlineFunctionPragma(P, CurScope, PragmaLoc, true);
      if (InlineOff.Ident)
        HandleUpwardInlineFunctionPragma(P, CurScope, PragmaLoc, true);
      return;
    }

    // Inline the current function, make sure we are in a function scope
    HandleUpwardInlineFunctionPragma(P, CurScope, PragmaLoc, InlineOff.Ident);
    return;
  }

  if (enableXilinxPragmaChecker()) {
    // self/all is no longed supported, error out
    if (str(InlineScope).equals_lower("self") ||
        str(InlineScope).equals_lower("all")) {
      P.Diag(P.getCurToken().getLocation(),
             diag::err_xlx_pragma_option_not_supported_by_HLS_WarnOut)
          << "Inline"
          << "self/all";
    }

    // region option is warn, maybe ignored in future
    if (str(InlineScope).equals_lower("region")) {
      P.Diag(P.getCurToken().getLocation(),
             diag::warn_deprecated_pragma_option_ignored_by_scout)
          << "region"
          << "Inline"
          << "Inline Pragma";
    }
  }

  if (str(InlineScope).equals_lower("self")) {
    // P.Diag(InlineScope.Loc, diag::warn_extra_pragma_hls_token_ignored)
    //    << str(InlineScope) << "INLINE"
    //    << FixItHint::CreateRemoval(InlineScope.Loc);

    if (RecursiveInline.Ident) {
      // recursively inline everything in the compound statement
      HandleDownwardInlineFunctionPragma(P, CurScope, PragmaLoc, true);
      if (InlineOff.Ident)
        HandleUpwardInlineFunctionPragma(P, CurScope, PragmaLoc, true);
      return;
    }

    // Inline the current function, make sure we are in a function scope
    HandleUpwardInlineFunctionPragma(P, CurScope, PragmaLoc, InlineOff.Ident);
    return;
  }

  // Generate a warning to ask users to replace 'all' by 'region'
  if (str(InlineScope).equals_lower("all")) {
    // P.Diag(InlineScope.Loc, diag::warn_obsolete_pragma_hls_token_replaced)
    //    << str(InlineScope) << "INLINE"
    //    << "region" << FixItHint::CreateReplacement(InlineScope.Loc,
    //    "region");
  }

  // Inline region
  if (InlineOff.Ident)
    HandleUpwardInlineFunctionPragma(P, CurScope, PragmaLoc, true);

  if (RecursiveInline.Ident) {
    // Recursively inline everything in the compound statement
    HandleDownwardInlineFunctionPragma(P, CurScope, PragmaLoc, true);
    return;
  }

  // Inline callsite in a none recursive way
  HandleDownwardInlineFunctionPragma(P, CurScope, PragmaLoc, false);
  return;
}

static void HandleResetPragma(Parser &P, Scope *CurScope,
                              SourceLocation PragmaLoc) {
  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(P, CurScope, "variable", false, {present("off")},
                         PragmaLoc, SubjectList);

  ArgsVector Args;
  if (!PAP.parse(Args))
    return;

  auto ResetOff = PAP["off"];
  auto *A = PAP.createAttribute("xlx_var_reset", ResetOff);

  auto &Actions = P.getActions();
  for (auto &S : PAP.subjects()) {
    auto Var = dyn_cast<VarDecl>(S.first);
    if (!Var->hasGlobalStorage()) {
      P.Diag(S.second, diag::warn_invalid_pragma_variable)
          << "reset"
          << "static or global";
      continue;
    }
    Actions.ProcessDeclAttributeList(CurScope, S.first, A, false);
  }
  return;
}
static void HandleFunctionAllocationPragma(Parser &P, Scope *CurScope,
                                           SourceLocation PragmaLoc) {
  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(P, CurScope, "", false,
                         {reqVarRefExpr("instances"), reqICEExpr("limit")},
                         PragmaLoc, SubjectList);
  if (!PAP.parse())
    return;

  Expr *func = PAP["instances"].get<Expr *>();
  ArgsUnion Args[] = {func, PAP["limit"]};
  PAP.addDependenceAttribute("xlx_function_allocation", Args);

  return;
}

static void HandleOperationAllocationPragma(Parser &P, Scope *CurScope,
                                            IdentifierLoc *allocType,
                                            SourceLocation PragmaLoc) {

  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(P, CurScope, "", false,
                         {reqId("instances"), reqICEExpr("limit")}, PragmaLoc,
                         SubjectList);
  if (!PAP.parse())
    return;

  ArgsUnion Args[] = {allocType, PAP["instances"], PAP["limit"]};
  PAP.addDependenceAttribute("fpga_resource_limit_hint", Args);
  return;
}

static void HandleAllocationPragma(Parser &P, Scope *CurScope,
                                   SourceLocation PragmaLoc) {

  auto &Ctx = P.getActions().getASTContext();
  auto &PP = P.getPreprocessor();
  auto &Actions = P.getActions();

  IdentifierLoc *AllocType = nullptr;
  auto allocToken = P.getCurToken();
  if (allocToken.getIdentifierInfo()->getName().equals_lower("function")) {
    AllocType = IdentifierLoc::create(Ctx, allocToken.getLocation(),
                                      allocToken.getIdentifierInfo());
    P.ConsumeToken();
    HandleFunctionAllocationPragma(P, CurScope, PragmaLoc);
  } else if (allocToken.getIdentifierInfo()->getName().equals_lower(
                 "operation")) {
    AllocType = IdentifierLoc::create(Ctx, allocToken.getLocation(),
                                      allocToken.getIdentifierInfo());
    P.ConsumeToken();
    HandleOperationAllocationPragma(P, CurScope, AllocType, PragmaLoc);
  } else {
    // Error out, we expect first option is "allocationype"
    P.Diag(allocToken.getLocation(),
           diag::warn_unexpected_token_in_pragma_argument)
        << allocToken.getIdentifierInfo()->getName() << "function/operation";
    return;
  }

  /*
  Expr* limit_expr = nullptr;
  Expr* func_pointer = nullptr;
  IdentifierLoc* instance_id = nullptr;

  while (P.getCurToken().isNot(tok::annot_pragma_XlxHLS_end)) {
    auto option = P.getCurToken();
    if (option.getIdentifierInfo()->getName().equals_lower("limit")) {
      P.ConsumeToken();
      if (!P.TryConsumeToken(tok::equal)) {
        const auto &Tok = P.getCurToken();
        P.Diag(Tok.getLocation(),
  diag::warn_unexpected_token_in_pragma_argument)
            << PP.getSpelling(Tok) << tok::equal;
        return ;
      }
      ExprResult ArgExpr = P.ParseConstantExpression();
      ArgExpr = Actions.CheckOrBuildPartialConstExpr(ArgExpr.get());
      ArgExpr = Actions.CorrectDelayedTyposInExpr(ArgExpr);
      limit_expr = ArgExpr.get();
    }
    else if (option.getIdentifierInfo()->getName().equals_lower("instances")){
      P.ConsumeToken();
      if (!P.TryConsumeToken(tok::equal)) {
        const auto &Tok = P.getCurToken();
        P.Diag(option.getLocation(),
  diag::warn_unexpected_token_in_pragma_argument)
            << PP.getSpelling(Tok) << tok::equal;
        return ;
      }
      if (AllocType->Ident->getName().equals_lower("function")) {
        SourceLocation loc = P.getCurToken().getLocation();
        ExprResult ArgExpr = P.ParseAssignmentExpression();
        ArgExpr = Actions.CorrectDelayedTyposInExpr(ArgExpr);
        if (!ArgExpr.isInvalid()) {
          Expr *expr = ArgExpr.get();
          func_pointer = expr;
        } else {
          P.Diag(loc, diag::warn_invalid_variable_expr);
          return;
        }
      }
      else{
        const Token &t = P.getCurToken();
        instance_id = IdentifierLoc::create(Ctx, t.getLocation(),
  t.getIdentifierInfo()); P.ConsumeToken();
      }
    }
    else {
      P.Diag(option.getLocation(), diag::warn_unexpected_pragma_parameter) <<
  option.getIdentifierInfo()->getName(); return;
    }
  }


  if (AllocType->Ident->getName().equals_lower("function")) {
    ArgsUnion Args[] = { func_pointer, limit_expr };
    ParsedAttributes &depAttrs = CurScope->getDependencePragmasRef();
    auto *II = PP.getIdentifierInfo("xlx_function_allocation");
    auto &Pool = depAttrs.getPool();
    SourceRange  PragmaRange = SourceRange(PragmaLoc,
  P.getEndOfPreviousToken()); auto *A = Pool.create(II, PragmaRange, nullptr,
  PragmaLoc, Args, 2, AttributeList::AS_GNU); A->setNext(nullptr);
    depAttrs.add(A);
  }
  else {
    ArgsUnion Args[] = {AllocType, instance_id, limit_expr};
    ParsedAttributes &depAttrs = CurScope->getDependencePragmasRef();
    auto *II = PP.getIdentifierInfo("fpga_resource_limit_hint");
    auto &Pool = depAttrs.getPool();
    SourceRange  PragmaRange = SourceRange(PragmaLoc,
  P.getEndOfPreviousToken()); auto *A = Pool.create(II, PragmaRange, nullptr,
  PragmaLoc, Args, 3, AttributeList::AS_GNU); A->setNext(nullptr);
    depAttrs.add(A);
  }
  */
}

static void HandleExpressionBanlancePragma(Parser &P, Scope *CurScope,
                                           SourceLocation PragmaLoc) {
  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(P, CurScope, "", false, {present("off")}, PragmaLoc,
                         SubjectList);

  ArgsVector Args;
  if (!PAP.parse(Args))
    return;

  auto BalanceOff = PAP["off"];
  PAP.addAttribute("xlx_expr_balance", BalanceOff);

  return;
}

static void HandleClockPragma(Parser &P, Scope *CurScope,
                              SourceLocation PragmaLoc) {

  if (enableXilinxPragmaChecker()) {
    P.Diag(PragmaLoc, diag::warn_deprecated_pragma_ignored_by_scout) << "clock";
    return;
  }
  return;
}

static void HandleDataPackPragma(Parser &P, Scope *CurScope,
                              SourceLocation PragmaLoc) {

  char *tmp = std::getenv("XILINX_VITIS_HLS_TRANSLATE_DATA_PACK_PRAGMA_TO_AGGREGATE");
  if (tmp == nullptr ) { 
    if (enableXilinxPragmaChecker()) {
      P.Diag(PragmaLoc, diag::err_xlx_pragma_not_supported_by_scout_HLS_WarnOut);
      return;
    }
  }
  else { 
    SubjectListTy SubjectList;
    XlxPragmaArgParser PAP(P, CurScope, "", false, {reqVarRefExpr("variable"), optId("instance"), presentId("struct_level"), presentId("field_level")}, 
        PragmaLoc,
        SubjectList);
    if (!PAP.parse()) { 
      return ;
    } 
  
    ArgsUnion byte_pad;
    if (auto id = PAP.presentedId("struct_level")) { 
      //errout , can not support it now
      P.Diag(id->Loc, diag::err_xlx_attribute_invalid_option_and_because)
        << "struct_level" << "Visti HLS doesn't support it";
  
    }
    else if (auto id = PAP.presentedId("field_level")) { 
      byte_pad = id;
    }
  
    Expr *var_expr = PAP["variable"].get<Expr *>();
    SmallVector<Expr *, 4> subExprs;
    getSubExprOfVariable(subExprs, var_expr, P);
  
    for (auto var : subExprs) {
      ArgsUnion Args[] = {var, byte_pad};
      PAP.addDependenceAttribute("xlx_data_pack", Args);
    }
  }
}

// static void HandleArrayMapPragma(Parser &P, Scope *CurScope,
//                                 SourceLocation PragmaLoc) {
//
//   P.Diag( PragmaLoc, diag::warn_deprecated_pragma_ignored_by_scout)
//      <<"array_map";
//   return;
//}

static void HandleFunctionInstantiatePragma(Parser &P, Scope *CurScope,
                                            SourceLocation PragmaLoc) {
#if 0
  if (enableXilinxPragmaChecker()) {
    P.Diag(PragmaLoc, diag::warn_deprecated_pragma_ignored_by_scout)
        << "function_instantiate";
  }
#endif

  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(P, CurScope, "variable", false, {}, PragmaLoc,
                         SubjectList);

  ArgsVector Args;
  if (!PAP.parse(Args))
    return;
  auto *A = PAP.createAttribute("xlx_func_instantiate", {});

  auto &Actions = P.getActions();

  for (auto &S : PAP.subjects()) {
    if (!isa<ParmVarDecl>(S.first)) {
      P.Diag(S.second, diag::warn_invalid_pragma_variable)
          << "function_instantiate"
          << "function parameter";
      continue;
    }
    Actions.ProcessDeclAttributeList(CurScope, S.first, A, false);
  }
  return;
}

static void HandleOccurrencePragma(Parser &P, Scope *CurScope,
                                   SourceLocation PragmaLoc) {
  // occurrence can not be inserted in function scope
  if (CurScope->isFunctionScope()) {
    P.Diag(PragmaLoc, diag::warn_xlx_pragma_applied_in_wrong_scope)
        << "occurrence" << 2;
    return;
  }

  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(P, CurScope, "", false, {optICEExpr("cycle", 1)},
                         PragmaLoc, SubjectList);

  ArgsVector Args;
  if (!PAP.parse(Args))
    return;

  ArgsUnion Cycle = PAP["cycle"];
  PAP.addAttribute("xlx_occurrence", Cycle);
}

static void HandleProtocolPragma(Parser &P, Scope *CurScope,
                                 SourceLocation PragmaLoc) {
  // Remove warning for protocol for temp
  // P.Diag( PragmaLoc, diag::warn_deprecated_pragma_ignored_by_scout )
  //   <<"protocol";

  // protocol can not be inserted in function scope
  if (CurScope->isFunctionScope()) {
    P.Diag(PragmaLoc, diag::warn_xlx_pragma_applied_in_wrong_scope)
        << "protocol" << 2;
    return;
  }

  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(P, CurScope, "", false,
                         {present("floating", 1), present("fixed", 1)},
                         PragmaLoc, SubjectList);

  ArgsVector Args;
  if (!PAP.parse(Args))
    return;

  auto Mode = "floating";
  if (PAP.presented("fixed"))
    Mode = "fixed";

  ArgsUnion Arg = PAP.createIdentLoc(Mode);
  PAP.addAttribute("xlx_protocol", Arg);
}

static void HandleLatencyPragma(Parser &P, Scope *CurScope,
                                SourceLocation PragmaLoc) {

  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(P, CurScope, "", false,
                         {optICEExpr("min", 0), optICEExpr("max", 65535)},
                         PragmaLoc, SubjectList);

  ArgsVector Args;
  if (!PAP.parse(Args))
    return;

  if (!PAP.lookup("min") && !PAP.lookup("max"))
    return;

  ArgsUnion Arg[] = {PAP["min"], PAP["max"]};
  PAP.addDependenceAttribute("xcl_latency", Arg);
}

static void HandleArrayPartitionPragma(Parser &P, Scope *CurScope,
                                       SourceLocation PragmaLoc) {
  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(P, CurScope, "", false,
                         {reqVarRefExpr("variable"), optICEExpr("factor", 0),
                          optICEExpr("dim", 1), present("cyclic", 1),
                          present("block", 1), present("complete", 1)},
                         PragmaLoc, SubjectList);

  if (!PAP.parse())
    return;

  StringRef Type = "complete";
  if (PAP.presented("block"))
    Type = "block";
  else if (PAP.presented("cyclic"))
    Type = "cyclic";

  ArgsUnion AType = PAP.createIdentLoc(Type);
  ArgsUnion Dim = PAP["dim"];
  ArgsUnion Factor;

  if (Type.equals_lower("complete")) {
    if (auto Arg = PAP.lookup("factor")) {
      auto *E = Arg.get<Expr *>();
      P.Diag(E->getExprLoc(), diag::warn_extra_pragma_hls_token_ignored)
          << "factor"
          << "array_partition";
    }
  } else {
    if (!PAP.lookup("factor")) {
      P.Diag(PragmaLoc, diag::warn_pragma_named_argument_missing) << "factor";
      return;
    }

    // Have to insert the factor next to AType as the format of attribute
    // has a werid definition e.g. xcl_array_partition(block, 2, 1)
    // where the second parameter is the factor.
    Factor = PAP["factor"];
  }

  Expr *var_expr = PAP["variable"].get<Expr *>();
  SmallVector<Expr *, 4> subExprs;
  getSubExprOfVariable(subExprs, var_expr, P);

  for (auto var : subExprs) {
    ArgsUnion Args[] = {var, AType, Factor, Dim};
    PAP.addDependenceAttribute("xlx_array_partition", Args);
  }

  return;
}

static void HandleArrayReshapePragma(Parser &P, Scope *CurScope,
                                     SourceLocation PragmaLoc) {
  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(P, CurScope, "", false,
                         {reqVarRefExpr("variable"), optICEExpr("factor", 0),
                          optICEExpr("dim", 1), present("cyclic", 1),
                          present("block", 1), present("complete", 1),
                          present("object", 2)},
                         PragmaLoc, SubjectList);

  if (!PAP.parse())
    return;

  StringRef Type = "complete";
  if (PAP.presented("block"))
    Type = "block";
  else if (PAP.presented("cyclic"))
    Type = "cyclic";

  ArgsUnion AType = PAP.createIdentLoc(Type);
  ArgsUnion Dim = PAP["dim"];

  // ignore all other options
  if (PAP.presented("object")) {
    Type = "complete";
    AType = PAP.createIdentLoc(Type);
    Dim = PAP.createIntegerLiteral(0);
  }

  ArgsUnion factor;

  if (Type.equals_lower("complete")) {
    // don't expect factor option
    if (auto Arg = PAP.lookup("factor")) {
      auto *E = Arg.get<Expr *>();
      P.Diag(E->getExprLoc(), diag::warn_extra_pragma_hls_token_ignored)
          << "factor"
          << "array_reshape";
    }
  } else {
    // expect factor option
    if (!PAP.lookup("factor")) {
      // TODO, add error out message
      return;
    }

    // Have to insert the factor next to AType as the format of attribute
    // has a werid definition e.g. xlx_array_reshape(block, 2, 1)
    // where the second parameter is the factor.
    factor = PAP["factor"];
  }

  Expr *var_expr = PAP["variable"].get<Expr *>();
  // parse "expr1, expr2, expr3"
  SmallVector<Expr *, 4> subExprs;
  getSubExprOfVariable(subExprs, var_expr, P);
  for (auto var : subExprs) {
    ArgsUnion args[] = {var, AType, factor, Dim};

    PAP.addDependenceAttribute("xlx_array_reshape", args);
  }

  return;
}

static void HandleStreamPragma(Parser &P, Scope *CurScope,
                               SourceLocation PragmaLoc) {
  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(P, CurScope, "", false,
                         {reqVarRefExpr("variable"), optICEExpr("depth", 0),
                          optICEExpr("dim"), present("off")},
                         PragmaLoc, SubjectList);

  ArgsVector Args;
  if (!PAP.parse(Args))
    return;

  ArgsUnion Depth, /*Dim,*/ Off;
  Depth = PAP["depth"];
  if (PAP.presented("off")) {
    Off = PAP.createIntegerLiteral(1);
  } else {
    Off = PAP.createIntegerLiteral(0);
  }

  // Error out for 'dim' option
  if (enableXilinxPragmaChecker()) {
    if (PAP.lookup("dim"))
      P.Diag(P.getCurToken().getLocation(),
             diag::err_xlx_pragma_option_not_supported_by_HLS_WarnOut)
          << "Stream"
          << "dim";
  }
  Expr *var_expr = PAP["variable"].get<Expr *>();
  // parse "expr1, expr2, expr3"
  SmallVector<Expr *, 4> subExprs;
  getSubExprOfVariable(subExprs, var_expr, P);

  for (auto expr : subExprs) {
    ArgsUnion args[] = {expr, Depth, Off};
    PAP.addDependenceAttribute("xlx_reqd_pipe_depth", args);
  }
}

static bool isBurstMAXIStruct(QualType type) { 

  return (type->isClassType() && !type->getAsCXXRecordDecl()
                                 ->getCanonicalDecl()
                                 ->getQualifiedNameAsString()
                                 .compare("hls::burst_maxi"));
}

static VarDecl *CheckInterfacePort(Parser &P,
                                   const std::pair<Decl *, SourceLocation> S,
                                   StringRef Mode) {
  auto *Var = dyn_cast<VarDecl>(S.first);
  if (!Var || !isa<ParmVarDecl>(Var)) {
    P.Diag(S.second, diag::warn_invalid_interface_port);
    return nullptr;
  }

  // Check hls::stream type first
  const bool StreamMode =
      Mode.equals_lower("axis") || Mode.equals_lower("ap_fifo");
  const bool StreamType = IsHLSStreamType(Var);
  if (StreamType && !StreamMode) {
    P.Diag(S.second, diag::warn_unsupported_interface_port_data_type) << Mode;
    return nullptr;
  }

  auto Type = cast<ParmVarDecl>(Var)->getOriginalType().getCanonicalType();
  bool Handle =
      StringSwitch<bool>(Mode)
          // AXI
          .CaseLower("m_axi", Type->isArrayType() || Type->isPointerType() ||
                                  Type->isReferenceType() || isBurstMAXIStruct(Type))
          .CaseLower("axis", true)
          .CaseLower("s_axilite", true)
          // RAM/FIFO
          .CaseLower("ap_memory", Type->isArrayType() ||
                                      Type->isPointerType() ||
                                      Type->isReferenceType())
          .CaseLower("bram", Type->isArrayType() || Type->isPointerType() ||
                                 Type->isReferenceType())
          .CaseLower("ap_fifo", true)
          // Scalars
          .CaseLower("ap_none", !Type->isArrayType())
          // Handshake
          .CaseLower("ap_hs", true)
          .CaseLower("ap_ack", !Type->isArrayType())
          .CaseLower("ap_vld", !Type->isArrayType())
          .CaseLower("ap_ovld", !Type->isArrayType())
          // Stable
          .CaseLower("ap_stable", !Type->isArrayType())
          // Calling conventions
          .CaseLower("ap_ctrl_none", true)
          .CaseLower("ap_ctrl_hs", true)
          .CaseLower("ap_ctrl_chain", true)
          .Default(false);

  if (!Handle) {
    P.Diag(S.second, diag::warn_unsupported_interface_port_data_type) << Mode;
    return nullptr;
  }

  // Array of size one is not supported and treated as pointer to scalar.
  if (Mode.equals_lower("bram") || Mode.equals_lower("ap_memory")) {
    if (auto *AT = dyn_cast<ConstantArrayType>(Type)) {
      auto EleTy = AT->getElementType().getCanonicalType();
      // If the array element is another array, it's legal to have size one.
      // FIXME: what if the element array is also of size one?
      if (!EleTy->isArrayType() && AT->getSize().isOneValue()) {
        P.Diag(S.second, diag::warn_unsupported_interface_bram_type) << Mode;
        return nullptr;
      }
    }
  }

  return Var;
}

 static void HandleApBusInterfacePragma(Parser &P, Scope *CurScope,
                                        IdentifierLoc Mode,
                                        SourceLocation PragmaLoc,
                                        XlxPragmaArgParser &PAP) {
  //error out and do nothing
  P.Diag(P.getCurToken().getLocation(),
         diag::err_xlx_pragma_option_not_supported_by_HLS)
      << "Interface"
      << "ap_bus mode"
      << "m_axi";
  return;

}

static void HandleGenericInterfacePragma(Parser &P, Scope *CurScope,
                                         IdentifierLoc Mode,
                                         SourceLocation PragmaLoc,
                                         XlxPragmaArgParser &PAP) {

                         
  StringMap<XlxPragmaParam> ParmMap = {

      presentId("ap_stable", 6),
      presentId("ap_fifo", 6), 

      presentId("ap_none", 6),

      presentId("ap_hs", 6), 
      presentId("ap_ack", 6), 
      presentId("ap_vld", 6), 
      presentId("ap_ovld", 6),

    optVarRefExpr("port"), presentId("register"), optId("name"),
   optICEExpr("depth"), optICEExpr("latency")};

  if (!PAP.CheckAndFilter(ParmMap))
    return;

  // Latency is not documented
  if (auto Arg = PAP.lookup("latency")) {
    auto *E = Arg.get<Expr *>();
    P.Diag(E->getExprLoc(), diag::warn_extra_pragma_hls_token_ignored)
        << "latency"
        << "INTERFACE";
  }

  auto *ModeId = PAP.createIdentLoc(Mode.Ident, Mode.Loc);
  auto ModeStr = ModeId->Ident->getName();
  StringRef InterfaceMode = "fpga_scalar_interface";
  if (ModeStr.equals_lower("ap_fifo"))
    InterfaceMode = "fpga_address_interface";

  auto *AdaptorName = PAP.createIdentLoc("");
  ArgsUnion InterfaceArgs[] = {ModeId, AdaptorName};
  auto *A = PAP.createAttribute(InterfaceMode, InterfaceArgs);

  if (auto Arg = PAP.lookup("name"))
    A = PAP.createAttribute("fpga_signal_name", Arg, A);

  if (auto Arg = PAP.lookup("depth"))
    A = PAP.createAttribute("fpga_foot_print_hint", Arg, A);

  if (PAP.presentedId("register")) {
    A = PAP.createAttribute("fpga_register", None, A);
  }

  auto &Actions = P.getActions();

  Expr *var_expr = PAP["port"].get<Expr*>();
  SmallVector<Expr *, 4> subExprs;
  getSubExprOfVariable(subExprs, var_expr, P);
 
  for (auto port_ref : subExprs) {
    if (isa<DeclRefExpr>(port_ref)){ 
      Decl *decl = cast<DeclRefExpr>(port_ref)->getDecl();
      VarDecl* port_decl = CheckInterfacePort(P, std::make_pair(decl, port_ref->getExprLoc()), str(Mode));
      if (!port_decl)
        continue;

      Actions.ProcessDeclAttributeList(CurScope, port_decl, A, false);
    }
    else { 
      if (ModeStr == "ap_fifo") { 
        ArgsUnion args[] = { port_ref, PAP.presentedId("register"),  PAP.lookup("depth"), PAP.lookup("name")};
        PAP.addDependenceAttribute("ap_fifo", args) ;
      }
      else { 
        ArgsUnion args[] = { port_ref, ModeId, PAP.presentedId("register"), PAP.lookup("name")};
        PAP.addDependenceAttribute("ap_scalar", args) ;
      }
    }
  }
}

static void HandleBRAMInterfacePragma(Parser &P, Scope *CurScope,
                                      IdentifierLoc Mode,
                                      SourceLocation PragmaLoc,
                                      XlxPragmaArgParser &PAP) {
  StringMap<XlxPragmaParam> ParmMap = 
                         {
                          presentId("ap_memory", 6),
                          presentId("bram", 6),

                          optVarRefExpr("port"), optICEExpr("depth"), optICEExpr("latency"),
                          optId("storage_type", "default"), optId("name")};

  if (!PAP.CheckAndFilter(ParmMap)) { 
    return;
  }

  auto *ModeId = PAP.createIdentLoc(Mode.Ident, Mode.Loc);
  auto ModeStr = ModeId->Ident->getName();

  // Default storage_type configuration is "ram_2p" if "stroage_type" is not
  // specified.
  Expr *ram_type = nullptr;
  Expr *ram_impl = nullptr;
  if (auto C = PAP.lookup("storage_type")) {
    auto storage_type_id = C.get<IdentifierLoc *>();
    const platform::PlatformBasic *xilinxPlatform =
        platform::PlatformBasic::getInstance();
    std::pair<platform::PlatformBasic::OP_TYPE,
              platform::PlatformBasic::IMPL_TYPE>
        mem_impl_type;
    if (!xilinxPlatform->verifyInterfaceStorage(
            storage_type_id->Ident->getName().str(), &mem_impl_type)) {
      P.Diag(storage_type_id->Loc, diag::err_xlx_attribute_invalid_option)
          << storage_type_id->Ident->getName()
          << "interface BRAM's option 'storage_type'";
    }
    ram_type =
        PAP.createIntegerLiteral(mem_impl_type.first, storage_type_id->Loc);
    ram_impl =
        PAP.createIntegerLiteral(mem_impl_type.second, storage_type_id->Loc);
  } else {
    ram_type = PAP.createIntegerLiteral(platform::PlatformBasic::OP_UNSUPPORTED,
                                        Mode.Loc);
    ram_impl = PAP.createIntegerLiteral(platform::PlatformBasic::UNSUPPORTED,
                                        Mode.Loc);
  }

  // verify latency after template Instantiation
  auto Latency = PAP.lookup("latency");
  if (!Latency) { 
    Latency = PAP.createIntegerLiteral(-1);
  }

  AttributeList *A = nullptr;
  if (auto Arg = PAP.lookup("name"))
    A = PAP.createAttribute("fpga_signal_name", Arg, A);
  if (auto Arg = PAP.lookup("depth"))
    A = PAP.createAttribute("fpga_foot_print_hint", Arg, A);

  auto &Actions = P.getActions();

  Expr *var_expr = PAP["port"].get<Expr*>();
  SmallVector<Expr *, 4> subExprs;
  getSubExprOfVariable(subExprs, var_expr, P);
 
  for (auto port_ref : subExprs) {
    if (isa<DeclRefExpr>(port_ref)) {
      Decl *decl = cast<DeclRefExpr>(port_ref)->getDecl();
      VarDecl *port_decl = CheckInterfacePort(P, std::make_pair(decl, port_ref->getExprLoc()), str(Mode));
      if (!port_decl)
        continue;
  
      // Create an adaptor for each BRAM port.
      auto AdaptorName = PAP.createIdentLoc(port_decl->getName());
      ArgsUnion AdaptorArgs[] = {AdaptorName, ModeId, ram_type, ram_impl,
                                 Latency};
      PAP.addAttribute("bram_adaptor", AdaptorArgs);
  
      // Create argument attribute for each BRAM port.
      ArgsUnion InterfaceArgs[] = {ModeId, AdaptorName};
      Actions.ProcessDeclAttributeList(
          CurScope, port_decl,
          PAP.createAttribute("fpga_address_interface", InterfaceArgs, A), false);
    }
    else { 
      ArgsUnion args[] = { port_ref, ModeId, PAP.lookup("storage_type"), PAP.lookup("latency"), PAP.lookup("name")};
      PAP.addDependenceAttribute( "xlx_memory", args);
    }
  }
}

static void HandleSAXIInterfacePragma(Parser &P, Scope *CurScope,
                                      IdentifierLoc Mode,
                                      SourceLocation PragmaLoc,
                                      XlxPragmaArgParser &PAP) {
  
  StringMap<XlxPragmaParam> ParamMap = 
                         {
                          presentId("s_axilite", 6),
                          optVarRefExpr("port"), optId("bundle", "0"), presentId("register"),
                          optICEExpr("offset"), optId("clock"), optId("name")};
  if (!PAP.CheckAndFilter(ParamMap))
    return;

  auto &Attrs = CurScope->getParsedHLSPragmasRef();

  // Create the attribute for the adaptor
  auto *AdaptorName = PAP["bundle"].get<IdentifierLoc *>();

  ArgsUnion AdaptorArgs[] = {AdaptorName, PAP["clock"]};
  PAP.addAttribute("s_axi_adaptor", AdaptorArgs);

  ArgsVector InterfaceArgs = {PAP.createIdentLoc(Mode.Ident, Mode.Loc),
                              AdaptorName};
  if (auto Offset = PAP.lookup("offset"))
    InterfaceArgs.push_back(Offset);

  // Create the interface attribute for the ParamDecl
  auto *A = PAP.createAttribute("fpga_interface_wrapper", InterfaceArgs);

  if (PAP.presentedId("register"))
    A = PAP.createAttribute("fpga_register", None, A);

  if (auto Arg = PAP.lookup("name"))
    A = PAP.createAttribute("fpga_signal_name", Arg, A);

  auto &Actions = P.getActions();
  ArgsUnion bundleName = PAP.lookup("bundle");
  ArgsUnion offset = PAP.lookup("offset");
  ArgsUnion isRegister;
  if (PAP.presentedId("register")) { 
    isRegister = PAP.createIntegerLiteral(1);
  }

  Expr *var_expr = PAP["port"].get<Expr*>();
  SmallVector<Expr *, 4> subExprs;
  getSubExprOfVariable(subExprs, var_expr, P);
 
  for (auto port_ref : subExprs) {
    if (isa<DeclRefExpr>(port_ref)){ 
      SourceLocation loc = cast<DeclRefExpr>(port_ref)->getLocation();
      Decl* decl = cast<DeclRefExpr>(port_ref)->getDecl();

      //if port = return 
      if (isa<FunctionDecl>(decl)) {
        Attrs.addAll(A);
        continue;
      }

      VarDecl * var = CheckInterfacePort( P, std::make_pair(decl, loc), str(Mode));
      if (!var) 
        continue;
      Actions.ProcessDeclAttributeList( CurScope, var, A, false);
    }
    else{ 
      ArgsUnion args[] = {port_ref, bundleName, offset, isRegister, PAP.lookup("name")};
      PAP.addDependenceAttribute("s_axilite", args);
    }
  }
}

static void HandleMAXIInterfacePragma(Parser &P, Scope *CurScope,
                                      IdentifierLoc Mode,
                                      SourceLocation PragmaLoc,
                                      XlxPragmaArgParser &PAP) {

  StringMap<XlxPragmaParam> ParamList = 
  {
      presentId("m_axi", 6), 
      optVarRefExpr("port"), optId("bundle"), optICEExpr("depth"), optId("offset"), optId("name"),
       optICEExpr("num_read_outstanding"), optICEExpr("num_write_outstanding"),
       optICEExpr("max_read_burst_length"),
       optICEExpr("max_write_burst_length"), optICEExpr("latency"),
       optICEExpr("max_widen_bitwidth")};

  if (!PAP.CheckAndFilter(ParamList)) {
    return;
  }

  auto PragmaEndLoc = P.getEndOfPreviousToken();
  SourceRange AttrRange(PragmaLoc, PragmaEndLoc);

  // Create the m_axi adaptor for the function decl
  auto *AdaptorName = PAP["bundle"].get<IdentifierLoc *>();
  auto AdaptorStr = str(AdaptorName);
  auto OffsetMode = PAP.lookup("offset");
  auto *ModeId = PAP.createIdentLoc(Mode.Ident, Mode.Loc);

  AttributeList *A = nullptr;
  if (AdaptorStr.empty()) {
    // Default value will be determined in LLVM phase
    AdaptorStr = "0";
    AdaptorName = PAP.createIdentLoc(AdaptorStr);
  }

  if (!AdaptorStr.empty()) {
    if (!OffsetMode) { 
      OffsetMode = PAP.createIdentLoc("");
    }
    // Create the interface attribute for the ParamDecl
    ArgsUnion InterfaceArgs[] = {ModeId, AdaptorName, OffsetMode};
    A = PAP.createAttribute("fpga_address_interface", InterfaceArgs);
  }

  if (auto Arg = PAP.lookup("name"))
    A = PAP.createAttribute("fpga_signal_name", Arg, A);

  if (auto Arg = PAP.lookup("depth"))
    A = PAP.createAttribute("fpga_foot_print_hint", Arg, A);

  if (auto Arg = PAP.lookup("latency"))
    A = PAP.createAttribute("fpga_maxi_latency", Arg, A);

  if (auto Arg = PAP.lookup("num_read_outstanding"))
    A = PAP.createAttribute("fpga_maxi_num_read_outstanding", Arg, A);

  if (auto Arg = PAP.lookup("num_write_outstanding"))
    A = PAP.createAttribute("fpga_maxi_num_write_outstanding", Arg, A);

  if (auto Arg = PAP.lookup("max_read_burst_length"))
    A = PAP.createAttribute("fpga_maxi_max_read_burst_length", Arg, A);

  if (auto Arg = PAP.lookup("max_write_burst_length"))
    A = PAP.createAttribute("fpga_maxi_max_write_burst_length", Arg, A);

  if (auto Arg = PAP.lookup("max_widen_bitwidth"))
    A = PAP.createAttribute("fpga_maxi_max_widen_bitwidth", Arg, A);

  auto &Actions = P.getActions();

  Expr *var_expr = PAP.lookup("port").get<Expr*>();
  SmallVector<Expr *, 4> subExprs;
  getSubExprOfVariable(subExprs, var_expr, P);
  for (auto port_ref : subExprs) {
    if (isa<DeclRefExpr>(port_ref)) { 
      Decl * decl = cast<DeclRefExpr>(port_ref)->getDecl();
      VarDecl *port_decl = CheckInterfacePort(P, std::make_pair(decl, port_ref->getExprLoc()), str(Mode));

      if (!port_decl) { 
        continue;
      }
      Actions.ProcessDeclAttributeList(CurScope, port_decl, A, false);
    }
    else {
      ArgsUnion args [] = { port_ref, PAP.lookup("bundle"), PAP.lookup("depth"), PAP.lookup("offset"), 
                          PAP.lookup("name"), PAP.lookup("num_read_outstanding"), PAP.lookup("num_write_outstanding"), 
                          PAP.lookup("max_read_burst_length"), PAP.lookup("max_write_burst_length"), PAP.lookup("latency"),
                          PAP.lookup("max_widen_bitwidth")};
      PAP.addDependenceAttribute("m_axi", args);
    }
  }
}

static void HandleAXISInterfacePragma(Parser &P, Scope *CurScope,
                                      IdentifierLoc Mode,
                                      SourceLocation PragmaLoc,
                                      XlxPragmaArgParser &PAP) {
  StringMap<XlxPragmaParam> ParamMap = 
      {
       presentId("axis", 6),
       optVarRefExpr("port"), presentId("register", 0),
       optEnum("register_mode", {"forward", "reverse", "both", "off"}),
       presentId("forward", 2), presentId("reverse", 2), presentId("both", 2),
       presentId("off", 2), optICEExpr("depth"), optId("name")};

  if (!PAP.CheckAndFilter(ParamMap)) { 
    return;
  }

  auto *ModeId = PAP.createIdentLoc(Mode.Ident, Mode.Loc);

  AttributeList *A = nullptr;
  if (auto Arg = PAP.lookup("name"))
    A = PAP.createAttribute("fpga_signal_name", Arg);

  if (auto Arg = PAP.lookup("depth"))
    A = PAP.createAttribute("fpga_foot_print_hint", Arg, A);

  // register default apply to axis
  // register_mode only apply to axis
  IdentifierLoc *RegMode;
  if (auto Reg = PAP.lookup("register_mode"))
    RegMode = Reg.get<IdentifierLoc *>();
  else if (auto id_loc = PAP.presentedId("both")) {
    RegMode = id_loc;
  } else if (auto id_loc = PAP.presentedId("forward")) {
    RegMode = id_loc;
  } else if (auto id_loc = PAP.presentedId("reverse")) {
    RegMode = id_loc;
  } else if (auto id_loc = PAP.presentedId("off")) {
    RegMode = id_loc;
  } else {
    RegMode = PAP.createIdentLoc("both");
  }

  auto &Actions = P.getActions();

  Expr *var_expr = PAP.lookup("port").get<Expr*>();
  SmallVector<Expr *, 4> subExprs;
  getSubExprOfVariable(subExprs, var_expr, P);
  for (auto port_ref : subExprs) {
    if (isa<DeclRefExpr>(port_ref)) { 
      Decl * decl = cast<DeclRefExpr>(port_ref)->getDecl();
      VarDecl *port_decl = CheckInterfacePort(P, std::make_pair(decl, port_ref->getExprLoc()), str(Mode));
      if (!port_decl)
        continue;
      Actions.ProcessDeclAttributeList(CurScope, port_decl, A, false);
  
      auto *AdaptorName = PAP.createIdentLoc(port_decl->getName());
      ArgsUnion AdaptorArgs[] = {AdaptorName, RegMode, PAP.createIdentLoc("")};
      PAP.addAttribute("axis_adaptor", AdaptorArgs);
  
      ArgsUnion InterfaceArgs[] = {ModeId, AdaptorName};
      Actions.ProcessDeclAttributeList(
          CurScope, port_decl,
          PAP.createAttribute("fpga_address_interface", InterfaceArgs), false);
    }
    else { 
      ArgsUnion args[] = {port_ref, PAP.presentedId("register"), RegMode, PAP.lookup("depth"), PAP.lookup("name")};
      PAP.addDependenceAttribute("axis", args);
    }
  }
}

static void HandleAPStableInterfacePragma(Parser &P, Scope *CurScope,
                                        IdentifierLoc Mode,
                                        SourceLocation PragmaLoc,
                                        XlxPragmaArgParser &PAP) 
{
  P.Diag(Mode.Loc, diag::warn_deprecated_pragma_option_ignored_by_scout)
    << "Ap_stable" 
    << "INTERFACE" 
    << "Stable Pragma" ;
  HandleGenericInterfacePragma(P, CurScope, Mode, PragmaLoc, PAP);
}

static void HandleUnknownInterfacePragma(Parser &P, Scope *CurScope,
                                         IdentifierLoc Mode,
                                         SourceLocation PragmaLoc,
                                         XlxPragmaArgParser &PAP) {
  P.Diag(Mode.Loc, diag::error_unknown_interface_mode);
}

static void HandleCallingConvInInterfacePragma(Parser &P, Scope *CurScope,
                                               IdentifierLoc Mode,
                                               SourceLocation PragmaLoc,
                                               XlxPragmaArgParser &PAP) {
  StringMap<XlxPragmaParam> ParamMap = 
                         {
                           presentId("ap_ctrl_chain"),
                           presentId("ap_ctrl_hs"),
                           presentId("ap_ctrl_none"),
                           reqVarRefExpr("port"), optId("name")};
  if (!PAP.CheckAndFilter(ParamMap))
    return;

  auto &Attrs = CurScope->getParsedHLSPragmasRef();

  // Before: ap_ctrl_none not reject 'register' option
  // Now: we remove it. But not delete the attribute, to not affect reflow APIs
  ArgsUnion InterfaceArgs[] = {PAP.createIdentLoc(Mode.Ident, Mode.Loc),
                               PAP.createIntegerLiteral(0), PAP["name"]};
  auto *A = PAP.createAttribute("fpga_function_ctrl_interface", InterfaceArgs);

  Expr *var_expr = PAP.lookup("port").get<Expr*>();
  SmallVector<Expr *, 4> subExprs;
  getSubExprOfVariable(subExprs, var_expr, P);
  for (auto port_ref : subExprs) {
    if (isa<DeclRefExpr>(port_ref)) { 
      Decl * decl = cast<DeclRefExpr>(port_ref)->getDecl();
      if (isa<FunctionDecl>(decl)) { 
        Attrs.addAll(A);
        continue;
      }
      P.Diag(port_ref->getExprLoc(), diag::warn_invalid_funtion_level_interface_port);
    }
    else { 
      assert(false &&"unexpected, only port = return is expected");
    }
  }
}

static void HandleInterfacePragmaWithPAP(Parser &P, Scope *CurScope,
                                              IdentifierLoc Mode,
                                              SourceLocation PragmaLoc,
                                              XlxPragmaArgParser &PAP) {

  Preprocessor &PP = P.getPreprocessor();
  std::string ModeName = Mode.Ident->getName().lower();
  Mode.Ident = PP.getIdentifierInfo(ModeName);


  // TODO: Generate a warning to say "register" is repaced by "ap_none"
  if (str(Mode).equals_lower("register"))
    return;

  typedef void (*Handler)(Parser &, Scope *, IdentifierLoc, SourceLocation, XlxPragmaArgParser &);

  // NOTE: ap_bus is not supported now. Not yet have a protocol spec
  auto *Handle =
      StringSwitch<Handler>(str(Mode))
          // AXI
          .CaseLower("m_axi", HandleMAXIInterfacePragma)
          .CaseLower("axis", HandleAXISInterfacePragma)
          .CaseLower("s_axilite", HandleSAXIInterfacePragma)
          // RAM/FIFO
          .CaseLower("ap_memory", HandleBRAMInterfacePragma)
          .CaseLower("bram", HandleBRAMInterfacePragma)
          .CaseLower("ap_fifo", HandleGenericInterfacePragma)
          // Scalars
          .CaseLower("ap_none", HandleGenericInterfacePragma)
          // Handshake
          .CaseLower("ap_hs", HandleGenericInterfacePragma)
          .CaseLower("ap_ack", HandleGenericInterfacePragma)
          .CaseLower("ap_vld", HandleGenericInterfacePragma)
          .CaseLower("ap_ovld", HandleGenericInterfacePragma)
          // Stable
          .CaseLower("ap_stable", HandleAPStableInterfacePragma)
          // Calling conventions
          .CaseLower("ap_ctrl_none", HandleCallingConvInInterfacePragma)
          .CaseLower("ap_ctrl_hs", HandleCallingConvInInterfacePragma)
          .CaseLower("ap_ctrl_chain", HandleCallingConvInInterfacePragma)
          // empty and do noting, i.e. ap_auto instead
          .CaseLower("ap_bus", HandleApBusInterfacePragma)
          // unknown
          .Default(HandleUnknownInterfacePragma);

  (*Handle)(P, CurScope, Mode, PragmaLoc, PAP);
}

static ArgsUnion ParseOffsetOptionForMAXIOrSAXI(XlxPragmaArgParser &PAP, Parser &P, SourceLocation PragmaLoc) 
{
  if (auto id = PAP.presentedId("s_axilite")) { 
    //parse offset as ICEExpression
    return PAP.parseICEExpression();
  }
  else if (auto id = PAP.presentedId("m_axi")) { 
    return PAP.parseIdentifierLoc();
  }
  else if (auto id = PAP.presentedId("ap_bus")) { 
    // This function is only to verify offset option
    // However, if ap_bus mode together with offset by mistake,
    // the checker of error_unknown_interface_mode brings confusing message.
    // So for ap_bus, do nothing here, will error out
    P.Diag(P.getCurToken().getLocation(),
           diag::err_xlx_pragma_option_not_supported_by_HLS)
        << "Interface"
        << "ap_bus mode"
        << "m_axi";
  }
  else { 
    P.Diag(PragmaLoc, diag::error_unknown_interface_mode);
  }
  return ArgsUnion();
}

static void HandleInterfacePragma(Parser &P, Scope *CurScope,
                                  SourceLocation PragmaLoc) {
  // FIXME Interface pragma can only be applied in function scope
  if (!CurScope->isFunctionScope()) {
    P.Diag(PragmaLoc, diag::warn_xlx_pragma_applied_in_wrong_scope)
        << "interface" << 0;
    return;
  }

  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP( P, CurScope, "", true, 
      {reqVarRefExpr("port"), 

      presentId("m_axi", 6), 

      presentId("axis", 6),

      presentId("s_axilite", 6),

      presentId("ap_memory", 6),
      presentId("bram", 6),

      presentId("ap_none", 6), 

      presentId("ap_fifo", 6), 
      presentId("ap_hs", 6), 
      presentId("ap_ack", 6), 
      presentId("ap_vld", 6), 
      presentId("ap_ovld", 6),

      presentId("ap_stable", 6), 
      presentId("ap_bus", 6), 

      presentId("ap_ctrl_none", 6),
      presentId("ap_ctrl_hs", 6),
      presentId("ap_ctrl_chain", 6),

      //MAXI
       optId("bundle", "0"), optICEExpr("depth"), optCallBackParser("offset", ParseOffsetOptionForMAXIOrSAXI), optId("name"),
       optICEExpr("num_read_outstanding"), optICEExpr("num_write_outstanding"),
       optICEExpr("max_read_burst_length"),
       optICEExpr("max_write_burst_length"), optICEExpr("latency"),
       optICEExpr("max_widen_bitwidth"),

     //SAXI
       optId("bundle", "0"), presentId("register"),
       optCallBackParser("offset", ParseOffsetOptionForMAXIOrSAXI), optId("clock"), optId("name"), 

     //AXIS
       presentId("register"),
       optEnum("register_mode", {"forward", "reverse", "both", "off"}),
       presentId("forward", 2), presentId("reverse", 2), presentId("both", 2),
       presentId("off", 2), optICEExpr("depth"), optId("name"),


     //BRAM, AP_MEMORY
       optICEExpr("depth"), optICEExpr("latency"),
       optId("storage_type", "default"), optId("name"), 

     //AP_NONE, AP_STABLE, AP_FIFO, AP_HS, AP_VLD, AP_OVLD, AP_ACK 
       presentId("register"), optId("name"),
       optICEExpr("depth"), optICEExpr("latency"), 
                         
    //AP_CTRL_HS, AP_CTRL_CHAIN, AP_CTRL_NONE
       optId("name")
      }, PragmaLoc, SubjectList);
      

  if (!PAP.parse()) { 
    return;
  }

  IdentifierLoc Mode ;
  if (auto id = PAP.presentedId("m_axi")) { 
    Mode = *id;
  }
  else if (auto id = PAP.presentedId("axis")) { 
    Mode = *id;
  }
  else if (auto id = PAP.presentedId("s_axilite")) { 
    Mode = *id;
  }
  else if (auto id = PAP.presentedId("ap_memory")) { 
    Mode = *id;
  }
  else if (auto id = PAP.presentedId("bram")) { 
    Mode = *id;
  }
  else if (auto id = PAP.presentedId("ap_none")) { 
    Mode = *id;
  }
  else if (auto id = PAP.presentedId("ap_fifo")) { 
    Mode = *id;
  }
  else if (auto id = PAP.presentedId("ap_hs")) { 
    Mode = *id;
  }
  else if (auto id = PAP.presentedId("ap_ack")) { 
    Mode = *id;
  }
  else if (auto id = PAP.presentedId("ap_vld")) { 
    Mode = *id;
  }
  else if (auto id = PAP.presentedId("ap_ovld")) { 
    Mode = *id;
  }
  else if (auto id = PAP.presentedId("ap_stable")) { 
    Mode = *id;
  }
  else if (auto id = PAP.presentedId("ap_bus")) { 
    Mode = *id;
  }
  else if (auto id = PAP.presentedId("ap_ctrl_none")) { 
    Mode = *id;
  }
  else if (auto id = PAP.presentedId("ap_ctrl_hs")) { 
    Mode = *id;
  }
  else if (auto id = PAP.presentedId("ap_ctrl_chain")) { 
    Mode = *id;
  }
  else {
    //for "$pragma HLS  register port = return " 
    //or  "pramga HLS register port = xxx " 
    //we need warning and ignore , use need use "$pramga HLS Latency min = 1 max = 1"
    if (PAP.presentedId("register") && PAP.lookup("port")) { 
      P.Diag(PragmaLoc, diag::warn_obsolete_pragma_replaced)
          << "#pragma HLS INTERFACE port=return register"
          << "#pragma HLS LATENCY min=1 max=1";
      return;
    }
    else { 
      P.Diag(PragmaLoc, diag::error_unknown_interface_mode);
    }
    return;
  }

  HandleInterfacePragmaWithPAP(P, CurScope, Mode, PragmaLoc,
                                    PAP);
}

static void HandleMAXIAliasPragma(Parser &P, Scope *CurScope, SourceLocation PragmaLoc) { 
  // FIXME Interface pragma can only be applied in function scope
  if (!CurScope->isFunctionScope()) {
    P.Diag(PragmaLoc, diag::warn_xlx_pragma_applied_in_wrong_scope)
        << "alias" << 0;
    return;
  }

  SubjectListTy SubjectList;
  XlxPragmaArgParser PAP(P, CurScope, "ports", true,
                         {optVarRefExpr("offset"), optVarRefExpr("distance")},
                         PragmaLoc, SubjectList);

  if (!PAP.parse())
    return;


  ArgsUnion offset_arg = PAP.lookup("offset");
  ArgsUnion distance_arg = PAP.lookup("distance");

  if(offset_arg && distance_arg) { 
    P.Diag(PragmaLoc, diag::warn_confilict_pragma_parameter)
        << "offset" << "distance" ;
    return;
  }
  else if (!offset_arg && !distance_arg) { 
    P.Diag(PragmaLoc, diag::warn_at_least_one_parameter_required)
      <<"offset" << "distance";
    return;
  }

  llvm::SmallVector<Expr*, 4> offsets;
  if (PAP.lookup("offset")) { 
    //TODO, this is ugly, we need rewrite pragma args parser
    Expr* var_expr = PAP.lookup("offset").get<Expr*>();

    SmallVector<Expr *, 4> subExprs;
    getSubExprOfVariable(subExprs, var_expr, P);

    for (auto sub : subExprs) {
      offsets.push_back(sub);
    }

    if (offsets.size() != PAP.subjects().size()) {
      P.Diag(var_expr->getExprLoc(), diag::err_xlx_attribute_invalid_option_and_because)
          <<"'Offset'"
          << "The number of offset values should match the number of ports";
      return ;
    }
  }
  else if (PAP.lookup("distance")) { 
    auto distance = PAP.lookup("distance").get<Expr*>();
    if (isa<IntegerLiteral>(distance)) { 
      auto val = static_cast<IntegerLiteral*>(distance)->getValue();
      int64_t d = val.getSExtValue();
      int offset = 0;
      for ( int i = 0; i < PAP.subjects().size(); i++) { 
        offsets.push_back(PAP.createIntegerLiteral(i *d, distance->getExprLoc()));
      }
    }
  }


  auto &Actions = P.getActions();

  for (int i = 0; i < PAP.subjects().size(); i++) {
    auto S = PAP.subjects()[i];
    Decl *decl = S.first;
    ArgsUnion args[] = { offsets[i] };
    auto *A = PAP.createAttribute("m_axi_alias", args);
    Actions.ProcessDeclAttributeList(CurScope, decl, A, false);
  }
  return;
}


static void HandleXlxStableContentPragma(Parser &P, Scope *CurScope,
                                         SourceLocation PragmaLoc) {
  SubjectListTy Subjects;
  XlxPragmaArgParser PAP(P, CurScope, "", false, {reqVarRefExpr("variable")},
                         PragmaLoc, Subjects);
  if (!PAP.parse()) {
    return;
  }
  Expr* var_expr = PAP["variable"].get<Expr*>();
  // parse "expr1, expr2, expr3"
  SmallVector<Expr *, 4> subExprs;
  getSubExprOfVariable(subExprs, var_expr, P);
  for (auto var: subExprs) {
    ArgsUnion args[] = {var};
    PAP.addDependenceAttribute("xlx_stable_content", args);
  }
}

static void HandleXlxStablePragma(Parser &P, Scope *CurScope,
                                  SourceLocation PragmaLoc) {
  SubjectListTy Subjects;
  XlxPragmaArgParser PAP(P, CurScope, "", false, {reqVarRefExpr("variable")},
                         PragmaLoc, Subjects);
  if (!PAP.parse())
    return;
  Expr* var_expr = PAP["variable"].get<Expr*>();
  // parse "expr1, expr2, expr3"
  SmallVector<Expr *, 4> subExprs;
  getSubExprOfVariable(subExprs, var_expr, P);
  for (auto var: subExprs) { 
    ArgsUnion args[] = {var};
    PAP.addDependenceAttribute("xlx_stable", args);
  }
}

static void HandleXlxSharedPragma(Parser &P, Scope *CurScope,
                                  SourceLocation PragmaLoc) {
  SubjectListTy Subjects;
  XlxPragmaArgParser PAP(P, CurScope, "", false, {reqVarRefExpr("variable")},
                         PragmaLoc, Subjects);
  if (!PAP.parse()) {
    return;
  }
  Expr* var_expr = PAP["variable"].get<Expr*>();
  // parse "expr1, expr2, expr3"
  SmallVector<Expr *, 4> subExprs;
  getSubExprOfVariable(subExprs, var_expr, P);
  for (auto var: subExprs) { 
    ArgsUnion args[] = {var};
    PAP.addDependenceAttribute("xlx_shared", args);
  }
}

static void HandleXlxDisaggrPragma(Parser &P, Scope *CurScope,
                                   SourceLocation PragmaLoc) {
  SubjectListTy Subjects;
  XlxPragmaArgParser PAP(P, CurScope, "", false, {reqVarRefExpr("variable")},
                         PragmaLoc, Subjects);
  if (!PAP.parse()) {
    return;
  }
  Expr* var_expr = PAP["variable"].get<Expr*>();
  // parse "expr1, expr2, expr3"
  SmallVector<Expr *, 4> subExprs;
  getSubExprOfVariable(subExprs, var_expr, P);
  for (auto var: subExprs) { 
    ArgsUnion args[] = {var};
    PAP.addDependenceAttribute("xlx_disaggregate", args);
  }
}

static void HandleXlxAggregatePragma(Parser &P, Scope *CurScope,
                                     SourceLocation PragmaLoc) {
  SubjectListTy Subjects;
  XlxPragmaArgParser PAP(P, CurScope, "", false, 
      {reqVarRefExpr("variable"), presentId("bit", 1), presentId("byte", 1), presentId("none", 1)},
                         PragmaLoc, Subjects);
  if (!PAP.parse()) {
    return;
  }
  IdentifierLoc *data_pack_compact = nullptr;
  if (auto id = PAP.presentedId("none")) {
    data_pack_compact = id;
  }
  else if (auto id = PAP.presentedId("bit")) { 
    data_pack_compact = id;
  }
  else if (auto id = PAP.presentedId("byte")) { 
    data_pack_compact = id;
  }

  Expr* var_expr = PAP["variable"].get<Expr*>();
  // parse "expr1, expr2, expr3"
  SmallVector<Expr *, 4> subExprs;
  getSubExprOfVariable(subExprs, var_expr, P);
  for( auto var: subExprs) { 
    ArgsUnion args[] = {var, data_pack_compact};
    PAP.addDependenceAttribute("xlx_aggregate", args);
  }
}

static void HandleXlxBindStoragePragma(Parser &P, Scope *CurScope,
                                       SourceLocation PragmaLoc) {

  const platform::PlatformBasic *xilinxPlatform =
      platform::PlatformBasic::getInstance();
  SubjectListTy Subjects;

  XlxPragmaArgParser PAP(P, CurScope, "", false,
                         {reqVarRefExpr("variable"), reqId("type"), optId("impl"),
                          optICEExpr("latency", -1)},
                         PragmaLoc, Subjects);

  if (!PAP.parse()) {
    return;
  }

  auto type_ii = PAP["type"].get<IdentifierLoc *>();
  auto impl_ii = PAP["impl"].get<IdentifierLoc *>();

  std::pair<platform::PlatformBasic::OP_TYPE,
            platform::PlatformBasic::IMPL_TYPE>
      mem_impl_type = xilinxPlatform->verifyBindStorage(
          type_ii->Ident->getName().str(), impl_ii->Ident->getName().str());

  std::string implName = "unsupported";
  if (mem_impl_type.first == platform::PlatformBasic::OP_UNSUPPORTED ||
      mem_impl_type.second == platform::PlatformBasic::UNSUPPORTED) {
    P.Diag(type_ii->Loc, diag::err_xlx_attribute_invalid_option)
        << type_ii->Ident->getName().str() + " + " +
               impl_ii->Ident->getName().str()
        << "BIND_STORAGE's option 'type + impl'";
    return;
  }

  auto mem_enum_expr = PAP.createIntegerLiteral(mem_impl_type.first);
  auto impl_enum_expr = PAP.createIntegerLiteral(mem_impl_type.second);

  Expr* var_expr = PAP["variable"].get<Expr*>();
  // parse "expr1, expr2, expr3"
  SmallVector<Expr *, 4> subExprs;
  getSubExprOfVariable(subExprs, var_expr, P);

  for (auto &e : subExprs) {
    // non-functional resource can not used in function
    ArgsUnion args[] = {e, mem_enum_expr, impl_enum_expr, PAP["latency"]};
    PAP.addDependenceAttribute("xlx_bind_storage", args);
  }
}

static void HandleXlxBindOpPragma(Parser &P, Scope *CurScope,
                                  SourceLocation PragmaLoc) {
  SubjectListTy Subjects;

  XlxPragmaArgParser PAP(P, CurScope, "", false,
                         {reqVarRefExpr("variable"), reqId("op"), optId("impl"),
                          optICEExpr("latency", -1)},
                         PragmaLoc, Subjects);

  if (!PAP.parse())
    return;

  const platform::PlatformBasic *xlxPlatform =
      platform::PlatformBasic::getInstance();
  auto opName = PAP["op"].get<IdentifierLoc *>()->Ident->getName().str();
  auto implName = PAP["impl"].get<IdentifierLoc *>()->Ident->getName().str();
  std::pair<platform::PlatformBasic::OP_TYPE,
            platform::PlatformBasic::IMPL_TYPE>
      op_impl = xlxPlatform->verifyBindOp(opName, implName);
  if (op_impl.first == platform::PlatformBasic::OP_UNSUPPORTED ||
      op_impl.second == platform::PlatformBasic::UNSUPPORTED) {
    P.Diag(PragmaLoc, diag::err_xlx_invalid_resource_option)
        << opName + " + " + implName;
    return;
  }
  auto op_enum_expr = PAP.createIntegerLiteral(op_impl.first);
  auto impl_enum_expr = PAP.createIntegerLiteral(op_impl.second);

  Expr* var_expr = PAP["variable"].get<Expr*>();
   // parse "expr1, expr2, expr3"
  SmallVector<Expr *, 4> subExprs;
  getSubExprOfVariable(subExprs, var_expr, P);
  for (Expr *expr : subExprs) {
    ArgsUnion args[] = {expr, op_enum_expr, impl_enum_expr, PAP["latency"]};
    PAP.addDependenceAttribute("xlx_bind_op", args);
  }
}

// TODO, rewrite it support template argument expression in Distance option
static void HandleXlxDependencePragma(Parser &P, Scope *CurScope,
                                      SourceLocation PragmaLoc) {

  SubjectListTy Subjects;

  XlxPragmaArgParser PAP(
      P, CurScope, "", false,
      {optVarRefExpr("cross_variables"), optVarRefExpr("variable"), presentId("pointer", 1),
       presentId("array", 1), presentId("intra", 2), presentId("inter", 2),
       presentId("raw", 3), presentId("war", 3), presentId("waw", 3),
       optICEExpr("distance"), presentId("false", 0), presentId("true", 0)},
      PragmaLoc, Subjects);

  if (!PAP.parse())
    return;

  IntegerLiteral *isDep = NULL;
  if (auto false_id = PAP.presentedId("false")) {
    // Just warning to user this old option will be deleted in future
    isDep = PAP.createIntegerLiteral(0, false_id->Loc);
  } else if (auto true_id = PAP.presentedId("true")) {
    isDep = PAP.createIntegerLiteral(1);
  }

  IdentifierLoc *dep_class, *dep_type, *dep_direction;

  if (auto pointer_id = PAP.presentedId("pointer")) {
    dep_class = pointer_id;
  } else if (auto array_id = PAP.presentedId("array")) {
    dep_class = array_id;
  } else {
    dep_class = nullptr;
  }

  if (auto intra_id = PAP.presentedId("intra")) {
    dep_type = intra_id;
  } else if (auto inter_id = PAP.presentedId("inter")) {
    dep_type = inter_id;
  } else {
    dep_type = nullptr;
  }

  if (auto raw_id = PAP.presentedId("raw")) {
    dep_direction = PAP.createIdentLoc("RAW");
  } else if (auto war_id = PAP.presentedId("war")) {
    dep_direction = PAP.createIdentLoc("WAR");
  } else if (auto raw_id = PAP.presentedId("raw")) {
    dep_direction = PAP.createIdentLoc("RAW");
  } else {
    dep_direction = nullptr;
  }

  Expr *distance = PAP["distance"].get<Expr *>();
  // ignore distance, if it is intra depenence
  if (PAP.presentedId("intra") && PAP.lookup("distance")) {
    P.Diag(PragmaLoc, diag::warn_conflict_pragma_parameter_and_ignored)
        << "intra"
        << "distance"
        << "distance";
    distance = nullptr;
  }

  auto cross_variables = PAP["cross_variables"].get<Expr*>();
  auto var_expr = PAP["variable"].get<Expr*>();
  if (cross_variables && var_expr)  {
    P.Diag(PragmaLoc, diag::warn_confilict_pragma_parameter)
        << "cross_variable" << "variable" ;
    return ;
  }

  if (var_expr) {
    SmallVector<Expr *, 4> subExprs;
    getSubExprOfVariable(subExprs, var_expr, P);
    for (unsigned i = 0; i < subExprs.size(); i++) {
      ArgsUnion args[] = {subExprs[i],   dep_class, dep_type,
                          dep_direction, distance,  isDep};
      PAP.addDependenceAttribute("xlx_dependence", args);
    }
  } 
  else if (cross_variables) { 
    // parse "expr1, expr2, expr3"
    SmallVector<Expr *, 4> subExprs;
    getSubExprOfVariable(subExprs, cross_variables, P);
    if (subExprs.size() != 2) { 
      //TODO, warning + ignore
      return ;
    }
    ArgsUnion args[] = { subExprs[0], subExprs[1], dep_class, dep_type, dep_direction, distance, isDep };
    PAP.addDependenceAttribute("xlx_cross_dependence", args );
  }
  else {
    ArgsUnion args[] = {(Expr*)nullptr, dep_class, dep_type,
                        dep_direction,   distance,  isDep};
    PAP.addDependenceAttribute("xlx_dependence", args);
  }
}

static void UnsupportPragma(Parser &P, Scope *CurScope,
                            SourceLocation PragmaLoc) {
  if (enableXilinxPragmaChecker()) {
    P.Diag(PragmaLoc, diag::err_xlx_pragma_not_supported_by_scout_HLS_WarnOut);
    return;
  }
  return;
}

static void HandleUnkwonPragma(Parser &P, Scope *CurScope,
                               SourceLocation PragmaLoc) {
  P.Diag(PragmaLoc, diag::warn_pragma_ignored) ;
}

/// \brief Handle the annotation token produced for #pragma HLS|AP|AUTOPILOT ...
void Parser::HandleXlxPragma() {

  getActions().EnterHLSParsing();
  auto RAII = llvm::make_scope_exit([this]() { FinishPragmaHLS(*this); });

  assert(Tok.is(tok::annot_pragma_XlxHLS));
  SourceLocation PragmaLoc =
      ConsumeAnnotationToken(); // Consume tok::annot_pragma_XlxHLS.

  if (getLangOpts().OpenCL) {
    Diag(PragmaLoc, diag::warn_pragma_ignored);
    return;
  }

  // Do not fail if the pragma end right after the beginning
  if (Tok.is(tok::annot_pragma_XlxHLS_end))
    return;

  auto CurScope = getCurScope();

  auto MaybeName = ParseXlxPragmaArgument(*this);
  if (!MaybeName.Ident)
    return;

  typedef void (*Handler)(Parser & P, Scope * CurScope,
                          SourceLocation PragmaLoc);

  auto *Handle =
      StringSwitch<Handler>(str(MaybeName))
          .CaseLower("dataflow", HandleXlxDataflowPragma)
          .CaseLower("pipeline", HandleXlxPipelinePragma)
          .CaseLower("unroll", HandleXlxUnrollPragma)
          .CaseLower("loop_flatten", HandleXlxFlattenPragma)
          .CaseLower("loop_merge", HandleXlxMergePragma)
          .CaseLower("loop_tripcount", HandleLoopTripCountPragma)
          .CaseLower("inline", HandleInlinePragma)
          .CaseLower("interface", HandleInterfacePragma)
          .CaseLower("resource", HandleResourcePragma)
          .CaseLower("stream", HandleStreamPragma)
          .CaseLower("reset", HandleResetPragma)
          .CaseLower("allocation", HandleAllocationPragma)
          .CaseLower("expression_balance", HandleExpressionBanlancePragma)
          .CaseLower("function_instantiate", HandleFunctionInstantiatePragma)
          .CaseLower("array_partition", HandleArrayPartitionPragma)
          .CaseLower("array_reshape", HandleArrayReshapePragma)
          .CaseLower("top", HandleTopFunctionPragma)
          .CaseLower("occurrence", HandleOccurrencePragma)
          .CaseLower("protocol", HandleProtocolPragma)
          .CaseLower("latency", HandleLatencyPragma)
          .CaseLower("dependence", HandleXlxDependencePragma)
          .CaseLower("stable", HandleXlxStablePragma)
          .CaseLower("stable_content", HandleXlxStableContentPragma)
          .CaseLower("shared", HandleXlxSharedPragma)
          .CaseLower("disaggregate", HandleXlxDisaggrPragma)
          .CaseLower("aggregate", HandleXlxAggregatePragma)
          .CaseLower("bind_op", HandleXlxBindOpPragma)
          .CaseLower("bind_storage", HandleXlxBindStoragePragma)
          .CaseLower("extract", UnsupportPragma)
          .CaseLower("REGION", UnsupportPragma)
          .CaseLower("array_map", UnsupportPragma)
          .CaseLower("clock", HandleClockPragma)
          .CaseLower("alias", HandleMAXIAliasPragma)
          .CaseLower("data_pack", HandleDataPackPragma)
          .Default(HandleUnkwonPragma);

  (*Handle)(*this, CurScope, PragmaLoc);

  getActions().ExitHLSParsing();

  // if don't consume left tokens in pramga decl, parser will automatically
  // eating left tokens in pragma until to  pragma end token ( general, pragma
  // end  is a special line change char without prefix "\" ? )
  //
  // following code will cause  parse failed
  // SkipUntil(tok::annot_pragma_XlxHLS_end,
  //        Parser::StopBeforeMatch);
  // ConsumeAnnotationToken();
  //
}
