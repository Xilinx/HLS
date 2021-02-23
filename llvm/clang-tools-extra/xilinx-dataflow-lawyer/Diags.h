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
#ifndef LLVM_CLANG_DATAFLOW_DIAGS_H
#define LLVM_CLANG_DATAFLOW_DIAGS_H

#include "clang/Basic/Diagnostic.h"
#include "llvm/ADT/StringRef.h"
#include <string>
#include <vector>

struct PragmaRewriteMessage {
  PragmaRewriteMessage(llvm::StringRef Message = "") : Message(Message){};

  /// \brief Constructs a diagnostic message with anoffset to the diagnostic
  /// within the file where the problem occured.
  ///
  /// \param Loc Should be a file location, it is not meaningful for a macro
  /// location.
  ///
  PragmaRewriteMessage(llvm::StringRef Message, llvm::StringRef FilePath,
                       llvm::StringRef FileOffset)
      : Message(Message), FilePath(FilePath), FileOffset(FileOffset){};
  std::string Message;
  std::string FilePath;
  std::string FileOffset; // format(Line:Col)
};

/// \brief Represents the diagnostic with the level of severity and possible
/// fixes to be applied.
struct PragmaRewriteDiag {
  PragmaRewriteDiag() = default;

  PragmaRewriteDiag(llvm::StringRef DiagnosticName,
                    PragmaRewriteMessage Message,
                    clang::DiagnosticsEngine::Level DiagLevel)
      : DiagnosticName(DiagnosticName), Message(Message),
        DiagLevel(DiagLevel){};

  /// \brief Name identifying the Diagnostic.
  std::string DiagnosticName;

  /// \brief Message associated to the diagnostic.
  PragmaRewriteMessage Message;

  clang::DiagnosticsEngine::Level DiagLevel;
};

/// \brief Collection of Diagnostics generated from a single translation unit.
struct TUDiagnostics {
  /// Name of the main source for the translation unit.
  std::string MainSourceFile;
  std::vector<PragmaRewriteDiag> Diagnostics;
};

#endif // LLVM_CLANG_DATAFLOW_DIAGS_H
