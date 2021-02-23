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

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_XILINX_DIRECTIVE2PRAGMA_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_XILINX_DIRECTIVE2PRAGMA_H

#include "../ClangTidy.h"

namespace clang {
namespace tidy {
namespace xilinx {

/// FIXME: Write a short description.
///
/// For the user-facing documentation see:
/// http://clang.llvm.org/extra/clang-tidy/checks/xilinx-directive2pragma.html

/// \brief structure for directive option list. These options are read from
/// directive file.
struct OptionHandle {
  std::string Name;
  std::string Value;
  std::string PositionalBoolean; // optional

  // check if option is variable/port
  bool hasVariableorPort();
  // check if variable/port is this pointer
  bool isthisPointer();
};
struct PragmaHandle {
  std::string Name;
  std::vector<struct OptionHandle> OptionList; // optional
};
struct DirectiveHandle {
  std::string FunctionName;
  std::string Label;
  std::string FunctionLabel;
  struct PragmaHandle PragmaItem; //  1 on 1 map
};
typedef std::vector<struct DirectiveHandle> DirectiveLists;
struct SrcDirectiveHandle {
  std::string Name;
  DirectiveLists DirectiveList; // optional
};

typedef std::vector<struct SrcDirectiveHandle> SrcDirectiveHandles;
struct SrcHandle {
  SrcDirectiveHandles SrcDirectiveList;
};

enum ValidVariableKind {
  VAR_None,
  VAR_Has,
  VAR_Invalid,
};

enum PargmaVariableKind {
  VAR_No,
  VAR_NoResource,
  VAR_Resource,
  VAR_ResourceCore,
};

enum VarScope {
  Inv_Scope,
  VAR_Local,
  VAR_Param,
  VAR_Global,
  VAR_Field,
};

enum ErrorType {
  ERR_None,
  WARN_Ambiguous_Function,
  WARN_Ambiguous_Variable,
  WARN_Ambiguous_Label,
  WARN_RESOURCE_ASSIGNMENT_ISSUE,
  WARN_END,
  ERR_CONTAINS_GOTO_STMT,
  ERR_CALSS_MEMBER_ACCESS,
  ERR_Variable_Invalid,
  ERR_Variable_Not_Exist,
  ERR_Function_Not_Exist,
  ERR_Label_Not_Exist,
};
class VariableInfo {
public:
  VariableInfo()
      : Scope(Inv_Scope), VarorMemDecl(nullptr), VariableDeclStmt(nullptr),
        AssignStmt(nullptr) {}

  enum VarScope Scope;
  std::string VarName;
  NamedDecl *VarorMemDecl;
  DeclStmt *VariableDeclStmt;
  Stmt *AssignStmt;
};

class DirectiveInfo {
public:
  DirectiveInfo()
      : Type(ERR_None), Kind(VAR_No), FunDecl(nullptr), LabDecl(nullptr),
        TmpLoc(), Context(nullptr), SM(nullptr), VarInfo(nullptr) {}

  enum ErrorType Type;
  SourceLocation ErrLoc;
  enum PargmaVariableKind Kind;
  std::string CoreName;
  FunctionDecl *FunDecl;
  LabelDecl *LabDecl;
  SourceLocation TmpLoc;
  ASTContext *Context;
  SourceManager *SM;
  VariableInfo *VarInfo;
};

class Directive2pragmaCheck : public ClangTidyCheck {
public:
  Directive2pragmaCheck(StringRef Name, ClangTidyContext *Context)
      : ClangTidyCheck(Name, Context),
        FileName(Context->getOptions().ImportDirective) {}
  ast_matchers::DeclarationMatcher
  FieldMatchers(ast_matchers::MatchFinder *Finder, std::string Fieldname,
                std::string Recordname);
  void AssignMatchers(ast_matchers::MatchFinder *Finder,
                      ast_matchers::DeclarationMatcher &vardecl,
                      DirectiveHandle *Directived);
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  // void onStartOfTranslationUnit() override;
  void onEndOfTranslationUnit() override;
  SrcDirectiveHandles &getSrcHandle() { return SrcDirective; };
  void eliminateDirective();
  ValidVariableKind checkVariableValidity(DirectiveHandle *Directived,
                                          const FunctionDecl *FuncDecl,
                                          std::string &Name);
  void SetPragma(DirectiveHandle *Directived, SourceLocation Loc,
                 SourceManager *SM, ASTContext *Context, StringRef Name,
                 bool before = false);
  void SetPragmawithResource(DirectiveHandle *Directived, DirectiveInfo *DInfo);
  void insertPragmaWithVar();
  bool hasPragmaOptionCore(DirectiveHandle *Directived, std::string &core);
  void collectVarInfo(DirectiveHandle *Directived, ASTContext *Context,
                      const NamedDecl *VariableDecl,
                      const DeclStmt *VariableDeclStmt, const Stmt *AssignStmt);
  bool hasInvalidDataMember(std::string OrignalName,
                            const VarDecl *VariableDecl,
                            DirectiveHandle *Directived);
  void collectFunctionInfo(DirectiveHandle *Directived, SourceManager *SM,
                           ASTContext *Context,
                           const FunctionDecl *MatchedFunction,
                           const LabelDecl *MatchedLabel, SourceLocation &Loc);
  int insertPragmaintoFunction(
      DirectiveHandle *Directived, const FunctionDecl *MatchedFunction,
      const ast_matchers::MatchFinder::MatchResult &Result);
  int insertPragmaintoLabel(
      DirectiveHandle *Directived, const LabelDecl *MatchedLabel,
      const ast_matchers::MatchFinder::MatchResult &Result);
  bool containGotoStmt(Stmt *stmt);
  Stmt *
  needInsertIntoBrace(Stmt *stmt,
                      const ast_matchers::MatchFinder::MatchResult &Result,
                      SourceLocation &Loc);
  std::string dumpPragma(DirectiveHandle *Directived);
  void setDiagMsg(DirectiveHandle *Pragma, DirectiveInfo *Info);

private:
  bool tryReadDirectiveFile();
  std::error_code parseDirective(StringRef DirectiveBuff);

  llvm::Optional<std::string> FileName;
  SrcDirectiveHandles SrcDirective;

  // store default func unit;
  std::vector<std::string> ListCore;
  // DirectiveLists index  map to successful inserted pragma for each tu
  std::map<DirectiveHandle *, std::string> InsertedPragma;
  // for each directive collect info
  std::map<DirectiveHandle *, DirectiveInfo *> PragmaInfo;
};

} // namespace xilinx
} // namespace tidy
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_XILINX_DIRECTIVE2PRAGMA_H
