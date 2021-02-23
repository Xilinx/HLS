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

#include "Directive2pragmaCheck.h"
#include "XilinxTidyCommon.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/LLVM.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include <utility>

#define DEBUG_TYPE "clang-tidy-d2p"

using namespace clang::ast_matchers;
using clang::tidy::xilinx::DirectiveHandle;
using clang::tidy::xilinx::OptionHandle;
using clang::tidy::xilinx::PragmaHandle;
using clang::tidy::xilinx::SrcDirectiveHandle;
using clang::tidy::xilinx::SrcHandle;

LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(OptionHandle)
LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(DirectiveHandle)
LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(SrcDirectiveHandle)

namespace llvm {
namespace yaml {

template <> struct MappingTraits<OptionHandle> {
  static void mapping(IO &IO, OptionHandle &KeyValue) {
    IO.mapRequired("name", KeyValue.Name);
    IO.mapRequired("value", KeyValue.Value);
    IO.mapOptional("positionalBoolean", KeyValue.PositionalBoolean);
  }
};

template <> struct MappingTraits<PragmaHandle> {
  static void mapping(IO &IO, PragmaHandle &KeyValue) {
    IO.mapRequired("name", KeyValue.Name);
    IO.mapOptional("option", KeyValue.OptionList);
  }
};

template <> struct MappingTraits<DirectiveHandle> {
  static void mapping(IO &IO, DirectiveHandle &KeyValue) {
    IO.mapRequired("functionName", KeyValue.FunctionName);
    IO.mapRequired("label", KeyValue.Label);
    IO.mapRequired("functionLabel", KeyValue.FunctionLabel);
    IO.mapRequired("pragma", KeyValue.PragmaItem);
  }
};

template <> struct MappingTraits<SrcDirectiveHandle> {
  static void mapping(IO &IO, SrcDirectiveHandle &KeyValue) {
    IO.mapRequired("name", KeyValue.Name);
    IO.mapOptional("directive", KeyValue.DirectiveList);
  }
};

template <> struct MappingTraits<SrcHandle> {
  static void mapping(IO &IO, SrcHandle &KeyValue) {
    IO.mapOptional("sourceFile", KeyValue.SrcDirectiveList);
  }
};
}
}

namespace clang {
namespace tidy {
namespace xilinx {

static const Stmt *getParentStmt(ASTContext *Context, const Stmt *stmt) {
  const auto &parents = Context->getParents(*stmt);
  if (parents.empty())
    return nullptr;
  const Stmt *aStmt = parents[0].get<Stmt>();
  if (aStmt)
    return aStmt;
  return nullptr;
}

static const Decl *getDeclContextFromStmt(ASTContext *Context,
                                          const Stmt *stmt) {
  const Stmt *aStmt = getParentStmt(Context, stmt);
  if (!aStmt) {
    const clang::Decl *aDecl = Context->getParents(*stmt).begin()->get<Decl>();
    if (aDecl)
      return aDecl;
  } else {
    return getDeclContextFromStmt(Context, aStmt);
  }
  return nullptr;
}

static void transformVariableName(std::string &str) {
  std::string::size_type pos = 0;

  // remove the &/*
  if ((pos = str.find_last_of("&")) != std::string::npos)
    str.erase(0, pos + 1);
  if ((pos = str.find_last_of("*")) != std::string::npos)
    str.erase(0, pos + 1);

  // only collect the base object
  if ((pos = str.find_first_of("->")) != std::string::npos)
    str.erase(pos);
  if ((pos = str.find_first_of(".")) != std::string::npos)
    str.erase(pos);
  if ((pos = str.find_first_of("[")) != std::string::npos)
    str.erase(pos);

  return;
}

static bool splitString(const std::string &s, std::vector<std::string> &v,
                        const StringRef &c) {
  std::string::size_type pos1, pos2;
  pos2 = s.find(c);
  pos1 = 0;
  while (std::string::npos != pos2) {
    v.push_back(s.substr(pos1, pos2 - pos1));

    pos1 = pos2 + c.size();
    pos2 = s.find(c, pos1);
  }
  if (pos1 != s.length())
    v.push_back(s.substr(pos1));

  // do not consider about ::a
  return v.size() == 2;
}

static std::string removeTemplate(const std::string Name) {
  if (Name.find("<") == std::string::npos)
    return Name;
  if (Name.find("::") == std::string::npos) {
    std::vector<std::string> tmp;
    splitString(Name, tmp, "<");
    return tmp[0];
  } else {
    std::vector<std::string> tmp;
    splitString(Name, tmp, "::");
    std::vector<std::string> tmp1;
    splitString(tmp[0], tmp1, "<");
    std::vector<std::string> tmp2;
    splitString(tmp[1], tmp2, "<");
    return tmp1[0] + "::" + tmp2[0];
  }
}

bool OptionHandle::hasVariableorPort() {
  if ((!Name.compare("variable") || !Name.compare("port")) && !Value.empty() &&
      (Value.compare("*") && Value.compare("return") && Value.compare("void") &&
       Value.compare("this")))
    return true;

  return false;
}

bool OptionHandle::isthisPointer() {
  if ((!Name.compare("variable") || !Name.compare("port")) &&
      !Value.compare("this"))
    return true;

  return false;
}

DeclarationMatcher
Directive2pragmaCheck::FieldMatchers(MatchFinder *Finder, std::string Fieldname,
                                     std::string Recordname) {

  auto recorddecl = !Recordname.empty()
                        ? recordDecl(hasName(Recordname)).bind("record")
                        : recordDecl().bind("record");
  auto memberdecl =
      fieldDecl(hasName(Fieldname), hasAncestor(recorddecl)).bind("member");
  Finder->addMatcher(memberdecl, this);

  return memberdecl;
}

void Directive2pragmaCheck::AssignMatchers(MatchFinder *Finder,
                                           DeclarationMatcher &vardecl,
                                           struct DirectiveHandle *Directived) {
  // Matcher for assign expr
  auto assignOp =
      anyOf(hasOperatorName("="), hasOperatorName("+="), hasOperatorName("-="),
            hasOperatorName("|="), hasOperatorName("~="), hasOperatorName("&="),
            hasOperatorName("/="), hasOperatorName("%="), hasOperatorName("*="),
            hasOperatorName(">>="), hasOperatorName("<<="));
  auto cxxassignOp =
      anyOf(hasOverloadedOperatorName("="), hasOverloadedOperatorName("+="),
            hasOverloadedOperatorName("-="), hasOverloadedOperatorName("|="),
            hasOverloadedOperatorName("~="), hasOverloadedOperatorName("&="),
            hasOverloadedOperatorName("/="), hasOverloadedOperatorName("%="),
            hasOverloadedOperatorName("*="), hasOverloadedOperatorName(">>="),
            hasOverloadedOperatorName("<<="));
  // initiali PragmaWithVar
  if (!Directived->PragmaItem.Name.compare("RESOURCE")) {
    // third variable has assignment stmt
    auto assignment = binaryOperator(assignOp, hasLHS(declRefExpr(to(vardecl))))
                          .bind("assignment");
    Finder->addMatcher(assignment, this);
    auto cxxassignment =
        cxxOperatorCallExpr(cxxassignOp, has(declRefExpr(to(vardecl))))
            .bind("assignment");
    Finder->addMatcher(cxxassignment, this);
  }

  return;
}

void Directive2pragmaCheck::registerMatchers(MatchFinder *Finder) {
  // firstly, paser directive file
  if (!tryReadDirectiveFile() || SrcDirective.empty())
    return;
  // remove directive for other tu
  if (SrcDirective.size() > 1)
    eliminateDirective();

  // Add matchers for each function and each loop/label.
  // FIXME consider function label for alias ?
  std::set<std::string> visit;
  std::set<std::string> visitlabel;
  std::set<std::string> visitvar;

  for (unsigned i = 0; i < SrcDirective[0].DirectiveList.size(); i++) {
    auto it = &SrcDirective[0].DirectiveList[i];
    if (it->FunctionName.empty()) // report erro in location of tcl file
      continue;
	// initial DirectiveInfo class
	PragmaInfo[it] = new DirectiveInfo();

    if (it->Label.empty() && visit.find(it->FunctionName) == visit.end()) {
      std::string notemp = removeTemplate(it->FunctionName);
      // match function decl
      bool IsMember = notemp.find("::") != std::string::npos;
      if (IsMember)
        Finder->addMatcher(functionDecl(isDefinition(), unless(isImplicit()),
                                        cxxMethodDecl(), hasName(notemp))
                               .bind("X"),
                           this);
      else
        Finder->addMatcher(functionDecl(isDefinition(), unless(isImplicit()),
                                        unless(cxxMethodDecl()),
                                        hasName(notemp))
                               .bind("X"),
                           this);
      visit.insert(it->FunctionName);
    } else if (!it->Label.empty() &&
               visitlabel.find(it->Label) == visitlabel.end()) {
      // match label stmt
      Finder->addMatcher(
          labelStmt(hasDeclaration(labelDecl(hasName(it->Label)))).bind("L"),
          this);
      visitlabel.insert(it->Label);
    }

    // match variable decl
    for (unsigned i = 0; i < it->PragmaItem.OptionList.size(); i++) {
      auto Opt = it->PragmaItem.OptionList[i];
      if (Opt.hasVariableorPort()) {

        // initial VariableInfo for pragma with variable
        PragmaInfo[it]->VarInfo = new VariableInfo();
        // set variable name in VariableInfo
        PragmaInfo[it]->VarInfo->VarName = Opt.Value;

        std::string str = Opt.Value;
        transformVariableName(str);
        // skip varibale like .arr and repeat match
        if (str.empty() || visitvar.find(str) != visitvar.end())
          break;
        visitvar.insert(str);

        // variable format change
        if (Opt.Value.compare(str)) {
          // it->PragmaItem.OptionList[i].Value = str;
          DEBUG(llvm::dbgs() << "change variable check format from "
                             << Opt.Value << " to " << str << "\n");
        }

        // variable with ::, used for member inside class/struct scope
        if (str.find_first_of("::") != std::string::npos) {
          std::string notemp = removeTemplate(str);
          std::vector<std::string> strsub;
          // do not care about ::a
          if (!splitString(notemp, strsub, "::") || strsub[0].empty()) {
            DEBUG(llvm::dbgs() << "variable invalid format in"
                               << it->PragmaItem.Name << "\n");
            break;
          }

          auto memberdecl = FieldMatchers(Finder, strsub[1], strsub[0]);
          // add assign checker for fielddecl
          AssignMatchers(Finder, memberdecl, it);

        } else {
          // check if variable without :: is fielddecl
          auto memberdecl = FieldMatchers(Finder, str, "");
          // add assign checker for fielddecl
          AssignMatchers(Finder, memberdecl, it);
        }
        // first make sure variable/static member exists
        auto declaration = varDecl(hasName(str)).bind("var");
        Finder->addMatcher(declaration, this);
        // match str as declrefexpr in assignment or declaration
        // second variable has decl stmt
        auto define = declStmt(has(declaration)).bind("define");
        Finder->addMatcher(define, this);
        // add assign checker for vardecl
        AssignMatchers(Finder, declaration, it);

        break;
      }
    }
  }
}

static bool isSameRecord(const CXXRecordDecl *RecDecl, std::string &Name) {
  std::vector<std::string> funsub;
  if (splitString(Name, funsub, "::")) {
    // take care of ClassTemplate
    std::vector<std::string> classsub;
    splitString(funsub[0], classsub, "<");

    // take care of FunctionTemplate
    std::vector<std::string> funcsub;
    splitString(funsub[1], funcsub, "<");

    if (!classsub[0].compare(RecDecl->getNameAsString()))
      return true;
  } else {
    if (funsub.size() != 1)
      return false;
    // take care of FunctionTemplate
    std::vector<std::string> tempsub;
    splitString(Name, tempsub, "<");
    for (auto mi = RecDecl->method_begin(); mi != RecDecl->method_end(); mi++) {
      if (!tempsub[0].compare(mi->getNameAsString())) {
        return true;
      }
    }
  }
  return false;
}

static bool isSameFunction(const FunctionDecl *MatchedFunction,
                           std::string &Name) {
  // FIXME distinguish ClassTemplateDecl and ClassTemplateSpecializationDecl
  // FIXME distinguish FunctionTemplateDecl and FunctionTemplateSpecialization
  std::vector<std::string> funsub;
  if (splitString(Name, funsub, "::")) {
    if (!isa<CXXMethodDecl>(MatchedFunction))
      return false;
    auto MedDecl = cast<CXXMethodDecl>(MatchedFunction);
    auto RecDecl = MedDecl->getParent();

    // take care of ClassTemplate
    std::vector<std::string> classsub;
    splitString(funsub[0], classsub, "<");

    // take care of FunctionTemplate
    std::vector<std::string> funcsub;
    splitString(funsub[1], funcsub, "<");

    if (!funcsub[0].compare(MedDecl->getNameAsString()) &&
        !classsub[0].compare(RecDecl->getNameAsString()))
      return true;
  } else {
    if (funsub.size() != 1)
      return false;
    // take care of FunctionTemplate
    std::vector<std::string> tempsub;
    splitString(Name, tempsub, "<");
    if (!tempsub[0].compare(MatchedFunction->getNameAsString()))
      return true;
  }
  return false;
}

static bool isSameRecordMember(const FieldDecl *MemberDecl,
                               const RecordDecl *RecDecl, std::string &VarName,
                               std::string &FunName) {
  // FIXME distinguish ClassTemplateDecl and ClassTemplateSpecializationDecl
  std::vector<std::string> strsub;
  if (splitString(VarName, strsub, "::")) {
    // directive has record member with ::
    // take care of ClassTemplate
    std::vector<std::string> classsub;
    splitString(strsub[0], classsub, "<");

    if (!strsub[1].compare(MemberDecl->getNameAsString()) &&
        !classsub[0].compare(RecDecl->getNameAsString()))
      return true;
  } else {
    if (strsub.size() != 1)
      return false;
    std::vector<std::string> funsub;
    if (!splitString(FunName, funsub, "::")) {
      // FIXME
      // directive has record member without ::, also as functionname
      if (funsub.size() != 1)
        return false;
      if (!VarName.compare(MemberDecl->getNameAsString()))
        return true;
    } else {
      // directive has record member without ::, but functionname has ::
      // take care of ClassTemplate
      std::vector<std::string> classsub;
      splitString(funsub[0], classsub, "<");

      if (!VarName.compare(MemberDecl->getNameAsString()) &&
          !classsub[0].compare(RecDecl->getNameAsString()))
        return true;
    }
  }

  return false;
}

static const Type *hasMemberinRecordDecl(std::string &MemberName,
                                         StringRef SplitChar,
                                         const VarDecl *VariableDecl) {
  // check a.b a[3].b a->b  but not a[3]->b
  std::vector<std::string> tmp;
  if (!splitString(MemberName, tmp, SplitChar))
    return nullptr;

  MemberName = tmp[1];
  const Type *vartype = nullptr;

  // getOriginalType will get the type before we decay the type
  if (isa<ParmVarDecl>(VariableDecl))
    vartype = cast<ParmVarDecl>(VariableDecl)->getOriginalType().getTypePtr();
  else
    vartype = VariableDecl->getType().getTypePtr();
  
  if (vartype->isReferenceType())
	  vartype = vartype->getAs<ReferenceType>()->getPointeeType().getTypePtr();
  else if (vartype->isArrayType() || vartype->isPointerType())
    vartype = vartype->getPointeeOrArrayElementType();

  return vartype;
}

static bool checkDataMemberValidity(std::string &MemberName,
                                    const RecordDecl *RecDecl) {
  if (llvm::any_of(RecDecl->fields(), [MemberName](FieldDecl *F) {
        return !MemberName.compare(F->getName());
      }))
    // found named fielddecl
    return true;
  return false;
}

bool Directive2pragmaCheck::hasInvalidDataMember(
    std::string OrignalName, const VarDecl *VariableDecl,
    struct DirectiveHandle *Directived) {
  SmallVector<StringRef, 2> splitchar = {".", "->"};
  for (auto schar : splitchar) {
    auto membername = OrignalName;
    if (auto membertype =
            hasMemberinRecordDecl(membername, schar, VariableDecl)) {
    
      if (!membertype->isRecordType()) {
        return 1;
      }

	  // find b in a.b a[3].b a->b
      const auto *recorddecl = membertype->getAs<RecordType>()->getDecl();
      if (!checkDataMemberValidity(membername, recorddecl)) {
        return 1;
      }
    }
  }
  return 0;
}

void Directive2pragmaCheck::collectFunctionInfo(
    struct DirectiveHandle *Directived, SourceManager *SM, ASTContext *Context,
    const FunctionDecl *MatchedFunction, const LabelDecl *MatchedLabel,
    SourceLocation &Loc) {
  /*
  For directive with variable or port, the pragma can not be generated right
  now.
  There is a map std::map<struct DirectiveHandle *, class DirectiveWithVar *>,
  for each directive with variable, collect function and vardecl matcher
  infomation
  and stored into  DirectiveWithVar.
  At the end of checker, following the information the generate the pragma
  */
  if (!Directived->PragmaItem.Name.compare("RESOURCE")) {
    // check core
    std::string corename;
    if (hasPragmaOptionCore(Directived, corename)) {
      PragmaInfo[Directived]->Kind = VAR_ResourceCore;
      PragmaInfo[Directived]->CoreName = corename;
    } else {
      PragmaInfo[Directived]->Kind = VAR_Resource;
    }
  } else {
    PragmaInfo[Directived]->Kind = VAR_NoResource;
  }
  // collect info
  PragmaInfo[Directived]->Context = Context;
  PragmaInfo[Directived]->SM = SM;
  if (PragmaInfo[Directived]->LabDecl) {
    // matched multiple label in same function
    PragmaInfo[Directived]->Type = WARN_Ambiguous_Label;
    PragmaInfo[Directived]->ErrLoc = Loc;
  }
  PragmaInfo[Directived]->LabDecl = const_cast<LabelDecl *>(MatchedLabel);
  if (PragmaInfo[Directived]->FunDecl) {
    // matched multiple function
    PragmaInfo[Directived]->Type = WARN_Ambiguous_Function;
    PragmaInfo[Directived]->ErrLoc = Loc;
  }
  PragmaInfo[Directived]->FunDecl = const_cast<FunctionDecl *>(MatchedFunction);

  PragmaInfo[Directived]->TmpLoc = Loc;

  return;
}

static void setAssignment(class DirectiveInfo *PragmaInfo,
                          const Stmt *AssignStmt) {
  auto VarInfo = PragmaInfo->VarInfo;
  if (!VarInfo->AssignStmt) {
    // initialize
    VarInfo->AssignStmt = const_cast<Stmt *>(AssignStmt);
  } else {
    // check sourcelocation , update the first assignment
    PragmaInfo->Type = WARN_RESOURCE_ASSIGNMENT_ISSUE;
    PragmaInfo->ErrLoc = AssignStmt->getLocStart();
    if (AssignStmt->getLocStart() < VarInfo->AssignStmt->getLocStart()) {
      VarInfo->AssignStmt = const_cast<Stmt *>(AssignStmt);
    }
  }
}

void Directive2pragmaCheck::collectVarInfo(struct DirectiveHandle *Directived,
                                           ASTContext *Context,
                                           const NamedDecl *VariableDecl,
                                           const DeclStmt *VariableDeclStmt,
                                           const Stmt *AssignStmt) {
  PragmaInfo[Directived]->VarInfo->VarorMemDecl =
      const_cast<NamedDecl *>(VariableDecl);
  // set for declstmt
  if (VariableDeclStmt)
    PragmaInfo[Directived]->VarInfo->VariableDeclStmt =
        const_cast<DeclStmt *>(VariableDeclStmt);
  // check if assignment's declcontext is same funtion with directive
  if (AssignStmt && getParentStmt(Context, AssignStmt) &&
      isa<CompoundStmt>(getParentStmt(Context, AssignStmt))) {
    const Decl *tdecl = getDeclContextFromStmt(Context, AssignStmt);
    if (isa<FunctionDecl>(tdecl) &&
        isSameFunction(cast<FunctionDecl>(tdecl), Directived->FunctionName)) {
      // set assignment
      setAssignment(PragmaInfo[Directived], AssignStmt);
    }
  }
}

static void HandleUnkwonPragma(Directive2pragmaCheck *Check,
                               const FunctionDecl *FD,
                               struct DirectiveHandle *Directived) {
  // do nothing
}

static void HandleTopPragma(Directive2pragmaCheck *Check,
                            const FunctionDecl *FD,
                            struct DirectiveHandle *Directived) {
  // add sdx_kernel attribute for each top function decl, such as:
  // __attribute__((sdx_kernel("name", 0)))
  std::string attr_str = "__attribute__((sdx_kernel(\"";
  if (Directived->PragmaItem.OptionList.size())
    attr_str += Directived->PragmaItem.OptionList[0].Value;
  attr_str += "\", 0))) ";
  for (auto CandidateDecl : FD->redecls()) {
    Check->diag(CandidateDecl->getLocStart(),
                "add sdx_kernel attribute for top function %0")
        << FD->getNameAsString()
        << FixItHint::CreateInsertion(CandidateDecl->getInnerLocStart(),
                                      attr_str);
  }
}

static void extraJob(Directive2pragmaCheck *Check, const FunctionDecl *FD,
                     struct DirectiveHandle *Directived) {
  // set sdx_kernel attribute for each top function
  StringRef PragmaName = Directived->PragmaItem.Name;

  typedef void (*Handler)(Directive2pragmaCheck * Check, const FunctionDecl *FD,
                          struct DirectiveHandle *Directived);

  auto *Handle = llvm::StringSwitch<Handler>(PragmaName)
                     .CaseLower("top", HandleTopPragma)
                     .Default(HandleUnkwonPragma);

  (*Handle)(Check, FD, Directived);
}

void Directive2pragmaCheck::check(const MatchFinder::MatchResult &Result) {
  assert(SrcDirective.size() == 1);

  const FunctionDecl *MatchedFunction;
  const LabelDecl *MatchedLabel;
  const LabelStmt *MatchedLabelStmt;
  ASTContext *Context = Result.Context;
  MatchedFunction = Result.Nodes.getNodeAs<FunctionDecl>("X");
  MatchedLabelStmt = Result.Nodes.getNodeAs<LabelStmt>("L");
  const VarDecl *VariableDecl = Result.Nodes.getNodeAs<VarDecl>("var");
  const FieldDecl *MemberDecl = Result.Nodes.getNodeAs<FieldDecl>("member");
  const RecordDecl *RecDecl = Result.Nodes.getNodeAs<RecordDecl>("record");
  const DeclStmt *VariableDeclStmt = Result.Nodes.getNodeAs<DeclStmt>("define");

  const Stmt *AssignStmt;
  if (Result.Nodes.getNodeAs<CXXOperatorCallExpr>("assignment")) {
    // for CXXOperatorCallExpr wiht operator=, need find parent stmt
    AssignStmt = getParentStmt(
        Context, Result.Nodes.getNodeAs<CXXOperatorCallExpr>("assignment"));
  } else {
    AssignStmt = Result.Nodes.getNodeAs<BinaryOperator>("assignment");
  }

  if (MatchedFunction) {
    for (unsigned i = 0; i < SrcDirective[0].DirectiveList.size(); i++) {
      auto it = &SrcDirective[0].DirectiveList[i];

      if (isSameFunction(MatchedFunction, it->FunctionName) &&
          it->Label.empty()) {
        auto Errcode = insertPragmaintoFunction(it, MatchedFunction, Result);
        if (Errcode) {
          // insert failed
          PragmaInfo[it]->Type = (enum ErrorType)Errcode;
          PragmaInfo[it]->ErrLoc = MatchedFunction->getLocStart();
        } else
          extraJob(this, MatchedFunction, it);
      }
    }
    return;
  }

  if (MatchedLabelStmt) {
    MatchedLabel = MatchedLabelStmt->getDecl();
    auto FD = dyn_cast<FunctionDecl>(MatchedLabel->getDeclContext());
    if (!FD)
      return;

    for (unsigned i = 0; i < SrcDirective[0].DirectiveList.size(); i++) {
      auto it = &SrcDirective[0].DirectiveList[i];

      if (!it->Label.empty() && !it->Label.compare(MatchedLabel->getName()) &&
          isSameFunction(FD, it->FunctionName)) {
        auto Errcode = insertPragmaintoLabel(it, MatchedLabel, Result);
        if (Errcode) {
          // insert failed
          PragmaInfo[it]->Type = (enum ErrorType)Errcode;
          PragmaInfo[it]->ErrLoc = MatchedLabel->getLocStart();
        } else
          extraJob(this, FD, it);
      }
    }
    return;
  }

  // matched variable from one of the directive, find it out.
  // also store vardecl releated infomation into PragmaWithVar map
  if (VariableDecl) {
    for (unsigned i = 0; i < SrcDirective[0].DirectiveList.size(); i++) {
      auto it = &SrcDirective[0].DirectiveList[i];
      if (!PragmaInfo[it]->VarInfo)
        continue;
      for (auto Opt : it->PragmaItem.OptionList) {
        if (Opt.hasVariableorPort()) {
          std::string str = Opt.Value;
          transformVariableName(str);

          // first check static data member
          if (VariableDecl->isStaticDataMember()) {
            // maybe need remove ::
            std::string::size_type pos = 0;
            if ((pos = str.find_last_of("::")) != std::string::npos)
              str.erase(0, pos + 1);

            if (!str.compare(VariableDecl->getName())) {
              // keep function and static record member belong to same record
              auto recdecl =
                  dyn_cast<CXXRecordDecl>(VariableDecl->getDeclContext());
              if (recdecl && isSameRecord(recdecl, it->FunctionName)) {
                PragmaInfo[it]->VarInfo->Scope = VAR_Field;
                collectVarInfo(it, Context, cast<NamedDecl>(VariableDecl),
                               VariableDeclStmt, AssignStmt);
              }
            }
          } else {
            // second check local/global variable
            if (!str.compare(VariableDecl->getName()) &&
                VariableDecl->isDefinedOutsideFunctionOrMethod()) {
				// check field decl in vardecl if exsits
                                if (hasInvalidDataMember(Opt.Value,
                                                         VariableDecl, it)) {
                                  PragmaInfo[it]->Type = ERR_Variable_Invalid;
                                  PragmaInfo[it]->ErrLoc =
                                      VariableDecl->getLocation();
                                  continue;
                                }
                                // check name for global variable
                                PragmaInfo[it]->VarInfo->Scope = VAR_Global;
                                collectVarInfo(it, Context,
                                               cast<NamedDecl>(VariableDecl),
                                               VariableDeclStmt, AssignStmt);

            } else if (!str.compare(VariableDecl->getName()) &&
                       !VariableDecl->isDefinedOutsideFunctionOrMethod()) {
              // check local variable in right function
              const FunctionDecl *decl =
                  cast<FunctionDecl>(VariableDecl->getParentFunctionOrMethod());
              if (isSameFunction(decl, it->FunctionName)) {
				  // check field decl in vardecl if exsits
                                  if (hasInvalidDataMember(Opt.Value,
                                                           VariableDecl, it)) {
                                    PragmaInfo[it]->Type = ERR_Variable_Invalid;
                                    PragmaInfo[it]->ErrLoc =
                                        VariableDecl->getLocation();
                                    continue;
                                  }
                                  PragmaInfo[it]->VarInfo->Scope =
                                      isa<ParmVarDecl>(VariableDecl)
                                          ? VAR_Param
                                          : VAR_Local;
                                  collectVarInfo(it, Context,
                                                 cast<NamedDecl>(VariableDecl),
                                                 VariableDeclStmt, AssignStmt);
              }
            }
          }
        }
      }
    }
    return;
  }

  if (MemberDecl) {
    for (unsigned i = 0; i < SrcDirective[0].DirectiveList.size(); i++) {
      auto it = &SrcDirective[0].DirectiveList[i];
      if (!PragmaInfo[it]->VarInfo)
        continue;
      for (auto Opt : it->PragmaItem.OptionList) {
        if (Opt.hasVariableorPort()) {
          std::string str = Opt.Value;
          transformVariableName(str);
          // recordtype maybe mismatched with some local variable
          if (isSameRecordMember(MemberDecl, RecDecl, str, it->FunctionName)) {
            PragmaInfo[it]->VarInfo->Scope = VAR_Field;
            collectVarInfo(it, Context, cast<NamedDecl>(MemberDecl),
                           VariableDeclStmt, AssignStmt);
          }
        }
      }
    }

    return;
  }

  assert(0 && "clang-tidy directive2pragma checker broken\n");
  return;
}

void Directive2pragmaCheck::onEndOfTranslationUnit() {

  insertPragmaWithVar();

  for (auto item : PragmaInfo)
    setDiagMsg(item.first, item.second);

  /*
  for (unsigned i = 0; i < SrcDirective[0].DirectiveList.size(); i++) {
    auto it = &SrcDirective[0].DirectiveList[i];
    if (InsertedPragma.find(it) != InsertedPragma.end())
      continue;
    // report warning in directive location of tcl file  when pragma has not
    // been  generated
     llvm::errs() << "Directive " << it->PragmaItem.Name
               << " has not been generated in " << getCurrentMainFile()
               << "\n";
  }
  */
  return;
}

void Directive2pragmaCheck::eliminateDirective() {
  // only keep directive data for current tu
  StringRef mainfile = getCurrentMainFile();
  for (SrcDirectiveHandles::iterator it = SrcDirective.begin();
       it != SrcDirective.end();) {
    std::size_t found = mainfile.rfind((*it).Name);
    if (found == std::string::npos) {
      it = SrcDirective.erase(it);
    } else {
      ++it;
    }
  }

  return;
}

ValidVariableKind
Directive2pragmaCheck::checkVariableValidity(struct DirectiveHandle *Directived,
                                             const FunctionDecl *FuncDecl,
                                             std::string &Name) {
  ValidVariableKind hasVariable = VAR_None;
  for (auto Opt : Directived->PragmaItem.OptionList) {
    if (Opt.hasVariableorPort()) {
      Name = Opt.Value;
      hasVariable = VAR_Has;
      break;
    }
    // for "this" variable, function should be c++ non-static member
    if (Opt.isthisPointer()) {
      if (isa<CXXMethodDecl>(FuncDecl) &&
          FuncDecl->getStorageClass() == SC_Static) {
        return VAR_Invalid;
      }
      break;
    }

    // FIXME for other named variable, check scope, or just leave to clang?
  }
  return hasVariable;
}

void Directive2pragmaCheck::setDiagMsg(DirectiveHandle *Pragma,
                                       DirectiveInfo *Info) {
  if (!Info->Type)
    return;
  std::string Str = "Directive " + Pragma->PragmaItem.Name + ":";
  std::string Str1 =
      "Directive " + Pragma->PragmaItem.Name + " can not be applyed:";

  switch (Info->Type) {
  case WARN_Ambiguous_Function:
    diag(Info->ErrLoc, "%0 Multiple Functions %1 found")
        << Str << Info->FunDecl;
    break;
  case WARN_Ambiguous_Variable:
    diag(Info->ErrLoc, "%0 Multiple Variable %1 found")
        << Str << Info->VarInfo->VarorMemDecl;
    break;
  case WARN_Ambiguous_Label:
    diag(Info->ErrLoc, "%0 Multiple Label %1 found in %2")
        << Str << Info->LabDecl << Info->FunDecl;
    break;
  case WARN_RESOURCE_ASSIGNMENT_ISSUE:
    diag(Info->ErrLoc,
         "%0 Variable %1 has no assignment or multiple assignments in %2")
        << Str << Info->VarInfo->VarorMemDecl << Info->FunDecl;
    break;
  case ERR_CONTAINS_GOTO_STMT:
    diag(Info->ErrLoc, "%0 Loop contains 'goto' statement") << Str1;
    break;
  case ERR_CALSS_MEMBER_ACCESS:
    diag(Info->ErrLoc, "%0 Invalid Class Member access") << Str1;
    break;
  case ERR_Variable_Invalid:
    diag(Info->ErrLoc, "%0 Variable %1 is invalid")
        << Str1 << Info->VarInfo->VarName;
    break;
  case ERR_Variable_Not_Exist:
    diag(Info->ErrLoc, "%0 Variable %1 is not exists in current file")
        << Str1 << Info->VarInfo->VarName;
    break;
  case ERR_Function_Not_Exist:
    diag(Info->ErrLoc, "%0 Function inserted does not exists in current file")
        << Str1;
    break;
  case ERR_Label_Not_Exist:
  default:
    break;
  }
  return;
}

void Directive2pragmaCheck::SetPragma(struct DirectiveHandle *Directived,
                                      SourceLocation Loc, SourceManager *SM,
                                      ASTContext *Context, StringRef Name,
                                      bool before) {

  // generatePragma
  std::string generatePragma = dumpPragma(Directived);

  // insert pragma under Function StartLoc
  auto BodyStart = Loc;
  if (!before)
    BodyStart = Lexer::getLocForEndOfToken(Loc, 0, *SM, Context->getLangOpts());

  generatePragma += dumpLineInfo(Loc, SM);

  diag(Loc, "add %0 into %1 ")
      << generatePragma << Name
      << FixItHint::CreateInsertion(BodyStart, generatePragma);

  InsertedPragma[Directived] = generatePragma;
  return;
}

void Directive2pragmaCheck::SetPragmawithResource(
    struct DirectiveHandle *Directived, DirectiveInfo *DInfo) {
  auto VarInfo = DInfo->VarInfo;
  if (!VarInfo->AssignStmt || DInfo->LabDecl) {
    // FIXME for resource pragma in label, we just insert into label
    // insert at the start of function/label for global/parm vardecl and
    // fielddecl or the declaration for local vardecl if location is before
    // label
    SourceLocation Loc;
    if (!VarInfo->VariableDeclStmt)
      Loc = DInfo->TmpLoc;
    else
      Loc = DInfo->TmpLoc < VarInfo->VariableDeclStmt->getEndLoc()
                ? VarInfo->VariableDeclStmt->getEndLoc()
                : DInfo->TmpLoc;
    // no assignment
    if (!VarInfo->AssignStmt) {
      PragmaInfo[Directived]->Type = WARN_RESOURCE_ASSIGNMENT_ISSUE;
      PragmaInfo[Directived]->ErrLoc = Loc;
    }

    SetPragma(Directived, Loc, DInfo->SM, DInfo->Context,
              DInfo->FunDecl->getName());
    return;
  }

  /* with assignment,accroding to clang resource pragma parser.
  pragma need to be inserted before assignment
  */
  SourceLocation Loc = VarInfo->AssignStmt->getLocStart();
  std::string generatePragma = dumpPragma(Directived);
  generatePragma += dumpLineInfo(Loc, DInfo->SM);

  diag(Loc, "add %0 into %1 ")
      << generatePragma << DInfo->FunDecl->getName()
      << FixItHint::CreateInsertion(Loc, generatePragma);
  InsertedPragma[Directived] = generatePragma;
  return;
}

void Directive2pragmaCheck::insertPragmaWithVar() {
  for (auto item : PragmaInfo) {
    class DirectiveInfo *DInfo = item.second;
    // directive has error type, can not apply
    if (!DInfo->VarInfo || DInfo->Type > WARN_END)
      continue;

    auto VarInfo = DInfo->VarInfo;
    if (!VarInfo->VarorMemDecl) {
      // maybe multifile directive in one json
      if (DInfo->FunDecl) {
        // not found vardecl in current tu
        DInfo->Type = ERR_Variable_Not_Exist;
        DInfo->ErrLoc = DInfo->FunDecl->getLocation();
      }
      continue;
    }

    if (VarInfo->VarorMemDecl && !DInfo->FunDecl) {
      // found vardecl, but not functiondecl
      continue;
    }

    if (DInfo->Kind == VAR_ResourceCore || DInfo->Kind == VAR_Resource) {
      // insert pargma for resource directive
      SetPragmawithResource(item.first, DInfo);

    } else if (DInfo->Kind == VAR_NoResource) {
      // insert at the start of function/label for global/parm vardecl and
      // fielddecl or the declaration for local vardecl if location is before
      // label
      SourceLocation Loc;
      if (!VarInfo->VariableDeclStmt)
        Loc = DInfo->TmpLoc;
      else
        Loc = DInfo->TmpLoc < VarInfo->VariableDeclStmt->getEndLoc()
                  ? VarInfo->VariableDeclStmt->getEndLoc()
                  : DInfo->TmpLoc;

      SetPragma(item.first, Loc, DInfo->SM, DInfo->Context,
                DInfo->FunDecl->getName());
    } else {
      // invalid Kind of Variable
      DInfo->Type = ERR_Variable_Invalid;
      DInfo->ErrLoc = VarInfo->VarorMemDecl->getLocation();
    }
  }
}

bool Directive2pragmaCheck::hasPragmaOptionCore(
    struct DirectiveHandle *Directived, std::string &core) {
  for (auto Opt : Directived->PragmaItem.OptionList) {
    std::string name = Opt.Name;
    std::string value = Opt.Value;
    if (!name.compare("core")) {
      std::transform(value.begin(), value.end(), value.begin(), toupper);
      for (auto funcunit : ListCore) {
        std::transform(funcunit.begin(), funcunit.end(), funcunit.begin(),
                       toupper);
        if (!value.compare(funcunit)) {
          core = funcunit;
          return true;
        }
      }
    }
  }
  return false;
}

int Directive2pragmaCheck::insertPragmaintoFunction(
    struct DirectiveHandle *Directived, const FunctionDecl *MatchedFunction,
    const MatchFinder::MatchResult &Result) {

  ASTContext *Context = Result.Context;
  SourceManager *SM = Result.SourceManager;

  // check  if pragma contains variable and its validity
  std::string varname;
  ValidVariableKind hasVariable =
      checkVariableValidity(Directived, MatchedFunction, varname);
  if (hasVariable == VAR_Invalid) {
    return ERR_CALSS_MEMBER_ACCESS;
  }

  SourceLocation Loc =
      cast<CompoundStmt>(MatchedFunction->getBody())->getLBracLoc();
  collectFunctionInfo(Directived, SM, Context, MatchedFunction, nullptr, Loc);
  if (!hasVariable)
    // insert pragma under Function StartLoc
    SetPragma(Directived, Loc, SM, Context, MatchedFunction->getName());

  return 0;
}

static Stmt *StripAttributedStmt(Stmt *StripedStmt) {
  if (auto wrapedstmt = dyn_cast<AttributedStmt>(StripedStmt))
    StripedStmt = wrapedstmt->getSubStmt();
  return StripedStmt;
}

int Directive2pragmaCheck::insertPragmaintoLabel(
    DirectiveHandle *Directived, const LabelDecl *MatchedLabel,
    const MatchFinder::MatchResult &Result) {
  ASTContext *Context = Result.Context;
  SourceManager *SM = Result.SourceManager;

  LabelStmt *MatchedStmt = MatchedLabel->getStmt();
  if (!MatchedStmt)
    return 0; // Wrong label location
  Stmt *NestedStmt = MatchedStmt->getSubStmt();
  if (!NestedStmt)
    return 0; // Wrong label location

  // hls will sink label attr into loop, change loop into attributedstmt
  // also some attribute will be attached into compoundstmt, change compoundstmt
  // into
  // attributedstmt
  NestedStmt = StripAttributedStmt(NestedStmt);
  // do not consider goto stmt
  if (auto *stmt = dyn_cast<ForStmt>(NestedStmt))
    if (containGotoStmt(stmt->getBody()))
      return ERR_CONTAINS_GOTO_STMT;

  if (auto *stmt = dyn_cast<DoStmt>(NestedStmt))
    if (containGotoStmt(stmt->getBody()))
      return ERR_CONTAINS_GOTO_STMT;

  if (auto *stmt = dyn_cast<WhileStmt>(NestedStmt))
    if (containGotoStmt(stmt->getBody()))
      return ERR_CONTAINS_GOTO_STMT;

  // check  if pragma contains variable and its validity
  std::string varname;
  ValidVariableKind hasVariable = checkVariableValidity(
      Directived, cast<FunctionDecl>(MatchedLabel->getDeclContext()), varname);
  if (hasVariable == VAR_Invalid)
    return ERR_CALSS_MEMBER_ACCESS;

  // for label with for/do/while and the body is compoundstmt, add pragma into
  // compoundstmt
  // or put pragma right after nested statement
  SourceLocation Loc;
  // if the nested stmt is loop, Loc is set for non compoundstmt in
  // needInsertIntoBrace
  auto *bodyStmt = needInsertIntoBrace(NestedStmt, Result, Loc);
  if (bodyStmt) {
    // hls will attach attribute in compoundstmt, change compoundstmt into
    // attributedstmt
    bodyStmt = StripAttributedStmt(bodyStmt);
    if (isa<CompoundStmt>(bodyStmt)) {
      Loc = cast<CompoundStmt>(bodyStmt)->getLBracLoc();
    }
  } else {
    Loc = isa<CompoundStmt>(NestedStmt)
              ? cast<CompoundStmt>(NestedStmt)->getLBracLoc()
              : (NestedStmt->getLocEnd());
  }

  // for pragma with label & variable like dependence
  collectFunctionInfo(Directived, SM, Context,
                      cast<FunctionDecl>(MatchedLabel->getDeclContext()),
                      MatchedLabel, Loc);
  if (!hasVariable)
    // insert pragma under label StartLoc or loop startloc
    SetPragma(Directived, Loc, SM, Context, MatchedLabel->getName());

  return 0;
}

bool Directive2pragmaCheck::containGotoStmt(Stmt *stmt) {
  if (!stmt)
    return false;

  stmt = StripAttributedStmt(stmt);
  if (auto *com_stmt = dyn_cast<CompoundStmt>(stmt)) {
    for (Stmt *var : com_stmt->body()) {
      if (isa<GotoStmt>(var))
        return true;
      if (auto *stm = dyn_cast<CompoundStmt>(var))
        if (containGotoStmt(stm))
          return true;
      if (auto *stm = dyn_cast<ForStmt>(var))
        if (containGotoStmt(stm->getBody()))
          return true;
      if (auto *stm = dyn_cast<DoStmt>(var))
        if (containGotoStmt(stm->getBody()))
          return true;
      if (auto *stm = dyn_cast<WhileStmt>(var))
        if (containGotoStmt(stm->getBody()))
          return true;
      if (auto *stm = dyn_cast<IfStmt>(var)) {
        if (containGotoStmt(stm->getThen()))
          return true;
        if (containGotoStmt(stm->getElse()))
          return true;
      }
    }
  } else
    return false;
  return false;
}

Stmt *Directive2pragmaCheck::needInsertIntoBrace(
    Stmt *stmt, const MatchFinder::MatchResult &Result, SourceLocation &Loc) {
  if (!stmt)
    return nullptr;

  if (isa<CompoundStmt>(stmt))
    return nullptr;
  else if (auto *stm = dyn_cast<ForStmt>(stmt)) {
    Loc = stm->getRParenLoc();
    return stm->getBody();
  } else if (auto *stm = dyn_cast<DoStmt>(stmt)) {
    Loc = stm->getDoLoc();
    return stm->getBody();
  } else if (auto *stm = dyn_cast<WhileStmt>(stmt)) {
    auto Tok = Lexer::findNextToken(Loc, *Result.SourceManager,
                                    Result.Context->getLangOpts());
    if (!Tok || Tok->isNot(tok::r_paren))
      // We get an invalid location if the while cond not end with ')'
      Loc = Loc.getLocWithOffset(1);
    else
      Loc = Tok->getLocation();
    return stm->getBody();
  }

  return nullptr;
}

std::string
Directive2pragmaCheck::dumpPragma(struct DirectiveHandle *Directived) {
  std::string PragmaStr = "\n#pragma HLS ";
  PragmaStr += Directived->PragmaItem.Name;

  for (auto Opt : Directived->PragmaItem.OptionList) {
    PragmaStr += " ";
    std::string name = Opt.Name;
    std::string val = Opt.Value;
    bool needAnalyzeDt = false;
    if (!name.empty()) {
      if ((!name.compare("variable") || !name.compare("port"))) {
        needAnalyzeDt = true;
      }
      PragmaStr += name;
    }
    if (!val.empty()) {
      if (needAnalyzeDt) {
        // NeedReferenceParameter
        PragmaStr += "=" + val;
      } else {
        PragmaStr += "=" + val;
      }
    }
  }
  PragmaStr += "\n";
  return PragmaStr;
}

static bool tryReadFile(std::string filename, std::string &content) {

  SmallString<128> ReadedFile(filename);
  bool IsFile = false;
  // Ignore errors from is_regular_file: we only need to know if we can read
  // the file or not.
  llvm::sys::fs::is_regular_file(Twine(ReadedFile), IsFile);
  if (!IsFile)
    return false;

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Text =
      llvm::MemoryBuffer::getFile(ReadedFile.c_str());
  if (std::error_code EC = Text.getError()) {
    llvm::errs() << "Can't read " << ReadedFile << " : " << EC.message()
                 << "\n";
    return false;
  }
  // Skip empty files
  if ((*Text)->getBuffer().empty())
    return false;
  content = (*Text)->getBuffer();
  return true;
}

bool Directive2pragmaCheck::tryReadDirectiveFile() {
  if (!FileName.hasValue() || FileName.getValue().empty())
    return false;

  std::string Text;
  if (!tryReadFile(FileName.getValue(), Text))
    return false;

  if (std::error_code Err = parseDirective(Text)) {
    llvm::errs() << "Parse Wrong: " << Err.message() << "\n";
    return false;
  }

  // fill ListCore from solution.funcunit file
  auto funcstr = FileName.getValue();
  auto pos = 0;

  if ((pos = funcstr.find(".json")) != std::string::npos) {
    if (!tryReadFile(funcstr.erase(pos) + ".funcunit", Text)) {
      return true;
    }
    splitString(Text, ListCore, " ");
  }
  return true;
}

std::error_code Directive2pragmaCheck::parseDirective(StringRef DirectiveBuff) {
  llvm::yaml::Input Input(DirectiveBuff);
  struct SrcHandle SrcBuff;
  Input >> SrcBuff;
  SrcDirective = std::move(SrcBuff.SrcDirectiveList);

  return Input.error();
}

} // namespace xilinx
} // namespace tidy
} // namespace clang
