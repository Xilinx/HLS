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
//  This file implements XilinxDataflowLawyer tool using the clang tooling.
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

#include "DiagnosticsYaml.h"
#include "Diags.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Lexer.h"
#include "clang/Parse/Parser.h"
#include "clang/Frontend/FrontendActions.h"
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

// Set up the command line options
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static cl::OptionCategory ToolTemplateCategory("tool-template options");
static cl::opt<bool> EnableFixes("enable-fixes", cl::desc("Enable FixIt."),
                                 cl::init(false),
                                 cl::cat(ToolTemplateCategory));
// same option as clangtidy
static cl::opt<std::string>
    ExportDiags("export-fixes",
                cl::desc(R"(YAML file to store suggested diagnostics in. 
)"),
                cl::value_desc("filename"), cl::cat(ToolTemplateCategory));

namespace {
class DataflowPragmaAction;
using ParserPtr = std::unique_ptr<Parser>;

/// PragmaXlxHLSHandler - "\#pragma HLS ...".
struct PragmaXlxHandler : public PragmaHandler {
  ParserPtr &P;

  PragmaXlxHandler(StringRef Name, ParserPtr &P) : PragmaHandler(Name), P(P) {}
  void HandlePragma(Preprocessor &PP, PragmaIntroducerKind Introducer,
                    Token &FirstToken) override;
};

class DataflowPragmaAction : public clang::SyntaxOnlyAction {
  void ParseAST(Sema &S);

  void ExecuteAction() override;

public:
  DataflowPragmaAction() {}
  ~DataflowPragmaAction() override {}
};
} // end anonymous namespace

void PragmaXlxHandler::HandlePragma(Preprocessor &PP,
                                    PragmaIntroducerKind Introducer,
                                    Token &FirstToken) {
  SmallVector<Token, 16> Pragma;
  Token Tok;
  Tok.startToken();
  Tok.setKind(tok::annot_pragma_XlxHLS);
  Tok.setLocation(FirstToken.getLocation());

  while (Tok.isNot(tok::eod)) {
    Pragma.push_back(Tok);
    PP.Lex(Tok);
  }

  if (Pragma.size() <= 1)
    return;

  if (!Pragma[1].is(tok::identifier))
    return;

  // Only parse dataflow
  if (Pragma[1].getIdentifierInfo()->getName().compare_lower("dataflow"))
    return;

  SourceLocation EodLoc = Tok.getLocation();
  Tok.startToken();
  Tok.setKind(tok::annot_pragma_XlxHLS_end);
  Tok.setLocation(EodLoc);
  Pragma.push_back(Tok);

  auto Toks = llvm::make_unique<Token[]>(Pragma.size());
  std::copy(Pragma.begin(), Pragma.end(), Toks.get());
  PP.EnterTokenStream(std::move(Toks), Pragma.size(),
                      /*DisableMacroExpansion=*/true);
}

void DataflowPragmaAction::ParseAST(Sema &S) {
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

void DataflowPragmaAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  assert(CI.hasPreprocessor() && "Need preprocessor!");

  if (!CI.hasSema())
    CI.createSema(getTranslationUnitKind(), /*CompletionConsumer*/ nullptr);

  ParseAST(CI.getSema());
}

static std::string FormatMessage(clang::DiagnosticsEngine::Level &DiagLevel,
                                 const clang::Diagnostic &Info) {
  SmallString<100> Message;
  Info.FormatDiagnostic(Message);
  // origin message is Ignore 'dataflow' on loop/'dataflow' ignored: which need
  // be remove in dump yaml file
  auto pos = Message.find(":");
  if (pos != StringRef::npos)
    Message = Message.substr(pos + 1).trim();

  // FIXME: ugly match to add an unique index
  std::map<StringRef, int> MatchedStr = {
      std::pair<StringRef, int>(
          "Only for-loops and functions support the dataflow", 1),
      std::pair<StringRef, int>("The xcl_dataflow attribute requires use of "
                                "the reqd_work_group_size(1, 1, 1) attribute "
                                "value",
                                2),
      std::pair<StringRef, int>(
          "The for-loop body must be enclosed within braces", 3),
      std::pair<StringRef, int>("Since the loop counter does not start from 0, "
                                "the compiler may not successfully process the "
                                "dataflow loop",
                                4),
      std::pair<StringRef, int>("Due to multiple loop counters, the compiler "
                                "may not successfully process the dataflow "
                                "loop",
                                5),
      std::pair<StringRef, int>("As the loop counter is not increased by 1, "
                                "the compiler may not successfully process the "
                                "dataflow loop",
                                6),
      std::pair<StringRef, int>(
          "Unsupported for-loop condition statement for dataflow", 7),
      std::pair<StringRef, int>("As the loop bound is not a constant or "
                                "function argument, the compiler may not "
                                "successfully process the dataflow loop",
                                7),
      std::pair<StringRef, int>(
          "Streams will behave like static variables inside a dataflow region",
          8),
      std::pair<StringRef, int>("Static scalars and arrays declared inside a "
                                "dataflow region will be treated like local "
                                "variables",
                                8),
      std::pair<StringRef, int>("Functions with a return value cannot be used "
                                "inside a dataflow region",
                                9),
      std::pair<StringRef, int>("Either use an argument of the function or "
                                "declare the variable inside the dataflow loop "
                                "body",
                                10),
      std::pair<StringRef, int>("Since the only kind of statements allowed in "
                                "a canonical dataflow region are variable declarations "
                                "and function calls, the compiler may not be "
                                "able to correctly handle the region",
                                11)};

  std::map<StringRef, int> ReversedMatchedStr = {
      std::pair<StringRef, int>("instances of non-canonical statements in the "
                                "dataflow region",
                                12)};

  unsigned MsgID = 0;
  auto id = MatchedStr.find(Message.str());
  if (id == MatchedStr.end()) {
    for (auto &P: ReversedMatchedStr)
      if (Message.str().find(P.first.str()) != std::string::npos) {
        MsgID = P.second;
        break;
      }
  } else {
    MsgID = id->second;
  }

  if (MsgID != 0) {
    // FIXME: change error to warning for first 2 messages
    if (MsgID < 2)
      DiagLevel = clang::DiagnosticsEngine::Level::Warning;

    std::string FormatStr =
        "[xlx-df-check-" + std::to_string(MsgID) + "] " + Message.c_str();
    return FormatStr;
  } else
    return "";
}

class PragmaRewriteConsumer : public clang::DiagnosticConsumer {
public:
  std::vector<PragmaRewriteDiag> Errors;
  void HandleDiagnostic(clang::DiagnosticsEngine::Level DiagLevel,
                        const clang::Diagnostic &Info) override {
    // Count warnings/errors.
    DiagnosticConsumer::HandleDiagnostic(DiagLevel, Info);
    SmallString<100> Message;
    Info.FormatDiagnostic(Message);
    llvm::errs() << Message << '\n';

    if (Info.getLocation().isFileID()) {
      PresumedLoc PLoc =
          Info.getSourceManager().getPresumedLoc(Info.getLocation());

      if (PLoc.isInvalid())
        return;

      // do not care macro expansion
      EmitToolingDiag(DiagLevel, Info, PLoc);
    }
  }

  void EmitToolingDiag(clang::DiagnosticsEngine::Level DiagLevel,
                       const clang::Diagnostic &Info, const PresumedLoc &PLoc) {

    auto Message = FormatMessage(DiagLevel, Info);
    if (Message.empty())
      return;

    auto PragmaMessage =
        Info.getLocation().isInvalid()
            ? PragmaRewriteMessage(Message)
            : PragmaRewriteMessage(Message, PLoc.getFilename(),
                                   std::to_string(PLoc.getLine()) + ":" +
                                       std::to_string(PLoc.getColumn()));
    // collect clang::Diagnostic
    std::string CheckName;
    DiagnosticsEngine::Level Level;
    switch (DiagLevel) {
    case DiagnosticsEngine::Error:
    case DiagnosticsEngine::Fatal:
      CheckName = "clang-diagnostic-error";
      Level = DiagnosticsEngine::Error;
      break;
    case DiagnosticsEngine::Warning:
      CheckName = "clang-diagnostic-warning";
      Level = DiagnosticsEngine::Warning;
      break;
    default:
      CheckName = "clang-diagnostic-warning";
      Level = DiagnosticsEngine::Warning;
      break;
    }

    Errors.emplace_back(CheckName, PragmaMessage, Level);
  }
};

void exportDiags(const llvm::StringRef MainFilePath,
                 const std::vector<PragmaRewriteDiag> &Errors,
                 raw_ostream &OS) {
  TUDiagnostics TUD;
  TUD.MainSourceFile = MainFilePath;
  for (const auto &Error : Errors) {
    auto Diag = Error;
    TUD.Diagnostics.insert(TUD.Diagnostics.end(), Diag);
  }

  yaml::Output YAML(OS);
  YAML << TUD;
  }

  int main(int argc, const char **argv) {
    llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);

    CommonOptionsParser OptionsParser(argc, argv, ToolTemplateCategory,
                                      cl::ZeroOrMore);

    // parse source file path
    StringRef FileName("dummy");
    auto PathList = OptionsParser.getSourcePathList();
    if (!PathList.empty()) {
      FileName = PathList.front();
    } else {
      llvm::errs() << " Error: no input files specified.\n";
      llvm::cl::PrintHelpMessage(/*Hidden=*/false, /*Categorized=*/true);
      return 1;
    }

    SmallString<256> FilePath(FileName);
    if (std::error_code EC = llvm::sys::fs::make_absolute(FilePath)) {
      llvm::errs() << "Can't make absolute path from " << FileName << ": "
                   << EC.message() << "\n";
      return 1;
    }

    ClangTool Tool(OptionsParser.getCompilations(),
                   OptionsParser.getSourcePathList());

    auto PragmaDiagConsumer = new PragmaRewriteConsumer();
    Tool.setDiagnosticConsumer(PragmaDiagConsumer);

    Tool.run(newFrontendActionFactory<DataflowPragmaAction>().get());

    auto Errors = PragmaDiagConsumer->Errors;
    if (!ExportDiags.empty() && !Errors.empty()) {
      std::error_code EC;
      llvm::raw_fd_ostream OS(ExportDiags, EC, llvm::sys::fs::F_None);
      if (EC) {
        llvm::errs() << "Error opening output file: " << EC.message() << '\n';
        return 1;
      }
      exportDiags(FilePath.str(), Errors, OS);
    }

    return 0;
  }
