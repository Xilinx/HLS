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
//  This file implements XilinxLegacyRewriter tool using the clang tooling.
//
//  Usage:
//  xilinx-legacy-rewriter <cmake-output-dir> <file1> <file2> ...
//
//  Where <cmake-output-dir> is a CMake build directory in which a file named
//  compile_commands.json exists (enable -DCMAKE_EXPORT_COMPILE_COMMANDS in
//  CMake to get this output).
//
//  <file1> ... specify the paths of files in the CMake source tree. This path
//  is looked up in the compile command database. If the path of a file is
//  absolute, it needs to point into CMake's source tree. If the path is
//  relative, the current working directory needs to be in the CMake source
//  tree and the file must be in a subdirectory of the current working
//  directory. "./" prefixes in the relative files will be automatically
//  removed, but the rest of a relative path must be a suffix of a path in
//  the compile command line database.
//
//  For example, to use tool-template on all files in a subtree of the
//  source tree, use:
//
//    /path/in/subtree $ find . -name '*.cpp'|
//        xargs tool-template /path/to/build
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Lexer.h"
#include "clang/Parse/Parser.h"
#include "clang/Rewrite/Frontend/FixItRewriter.h"
#include "clang/Rewrite/Frontend/FrontendActions.h"
#include "clang/Rewrite/Frontend/Rewriters.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Execution.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Refactoring/AtomicChange.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Signals.h"

using namespace clang;
using namespace clang::tooling;
using namespace llvm;

namespace {
class PragmaRewriteAction;
using ParserPtr = std::unique_ptr<Parser>;

/// PragmaXlxHLSHandler - "\#pragma HLS ...".
struct PragmaXlxHandler : public PragmaHandler {
  ParserPtr &P;

  bool ParsePragmaSubjects();
  bool ParseSubject();

  PragmaXlxHandler(StringRef Name, ParserPtr &P) : PragmaHandler(Name), P(P) {}
  void HandlePragma(Preprocessor &PP, PragmaIntroducerKind Introducer,
                    Token &FirstToken) override;
};

class PragmaRewriteAction : public clang::FixItAction {
  void ParseAST(Sema &S);

  void ExecuteAction() override;

public:
  bool BeginSourceFileAction(clang::CompilerInstance &CI) override {
    if (!FixItAction::BeginSourceFileAction(CI))
      return false;

    FixItOpts->FixWhatYouCan = true;
    FixItOpts->FixOnlyWarnings  = true;

    return true;
  }
  PragmaRewriteAction() {}
  ~PragmaRewriteAction() override {}
};
} // end anonymous namespace

bool PragmaXlxHandler::ParseSubject() {
  auto &Actions = P->getActions();
  auto &Tok = P->getCurToken();
  auto Loc = P->getCurToken().getLocation();
  bool SkippedSomthing =
      P->TryConsumeToken(tok::amp, Loc) || P->TryConsumeToken(tok::star, Loc);
  bool Invalid = Tok.isNot(tok::identifier);

  ExprResult ArgExpr = P->ParseAssignmentExpression();
  ArgExpr = Actions.CorrectDelayedTyposInExpr(ArgExpr);

  // Ignore the subject if there is already an "address of" operator
  // or some strange tok
  if (SkippedSomthing || Invalid)
    return true;

  auto &Ctx = P->getActions().getASTContext();
  auto &Diags = Ctx.getDiagnostics();
  unsigned DiagID = Diags.getCustomDiagID(DiagnosticsEngine::Remark,
                                          "expect address of variable");
  Diags.Report(Loc, DiagID) << FixItHint::CreateInsertion(Loc, "&");
  return true;
}

bool PragmaXlxHandler::ParsePragmaSubjects() {
  auto &Tok = P->getCurToken();
  if (Tok.isOneOf(tok::kw_return, tok::kw_void, tok::kw_this)) {
    P->ConsumeToken();
  } else if (!ParseSubject())
    return false;

  if (P->TryConsumeToken(tok::comma))
    return ParsePragmaSubjects();

  return true;
}

void PragmaXlxHandler::HandlePragma(Preprocessor &PP,
                                    PragmaIntroducerKind Introducer,
                                    Token &FirstToken) {
  // Parse each named argument:
  ///  named-argument:
  ///    identifer
  ///    identifer '=' identifer
  ///    identifer '=' integer
  auto &Tok = P->getCurToken();
  do {
    P->ConsumeAnyToken();
    if (Tok.is(tok::identifier)) {
      auto Name = Tok.getIdentifierInfo()->getName();
      if (Name.equals_lower("variable") || Name.equals_lower("port")) {
        P->ConsumeToken();

        if (!P->TryConsumeToken(tok::equal))
          continue;

        // do not return
        ParsePragmaSubjects();
      }
    }
  } while (!Tok.is(tok::eod));
}

void PragmaRewriteAction::ParseAST(Sema &S) {
  auto &Consumer = S.getASTConsumer();
  auto &PP = S.getPreprocessor();
  std::unique_ptr<Parser> P;

  PP.AddPragmaHandler(new PragmaXlxHandler("HLS", P));
  PP.AddPragmaHandler(new PragmaXlxHandler("AP", P));
  PP.AddPragmaHandler(new PragmaXlxHandler("AUTOPILOT", P));

  P.reset(new Parser(PP, S, /*SkipFunctionBodies*/ false));

  PP.EnterMainSourceFile();
  P->Initialize();

  Parser::DeclGroupPtrTy ADecl;
  ExternalASTSource *External = S.getASTContext().getExternalSource();
  if (External)
    External->StartTranslationUnit(&Consumer);

  for (bool AtEOF = P->ParseFirstTopLevelDecl(ADecl); !AtEOF;
       AtEOF = P->ParseTopLevelDecl(ADecl)) {
    // If we got a null return and something *was* parsed, ignore it.  This
    // is due to a top-level semicolon, an action override, or a parse error
    // skipping something.
    if (ADecl && !Consumer.HandleTopLevelDecl(ADecl.get()))
      return;
  }

  // Process any TopLevelDecls generated by #pragma weak.
  for (Decl *D : S.WeakTopLevelDecls())
    Consumer.HandleTopLevelDecl(DeclGroupRef(D));

  Consumer.HandleTranslationUnit(S.getASTContext());
}

void PragmaRewriteAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  assert(CI.hasPreprocessor() && "Need preprocessor!");

  if (!CI.hasSema())
    CI.createSema(getTranslationUnitKind(), /*CompletionConsumer*/ nullptr);

  ParseAST(CI.getSema());
}

// Set up the command line options
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static cl::OptionCategory ToolTemplateCategory("tool-template options");

int main(int argc, const char **argv) {
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);

  auto Executor = clang::tooling::createExecutorFromCommandLineArgs(
      argc, argv, ToolTemplateCategory);

  if (!Executor) {
    llvm::errs() << llvm::toString(Executor.takeError()) << "\n";
    return 1;
  }

  auto Err =
      Executor->get()->execute(newFrontendActionFactory<PragmaRewriteAction>());
  if (Err) {
    llvm::errs() << llvm::toString(std::move(Err)) << "\n";
  }

  return 0;
}
