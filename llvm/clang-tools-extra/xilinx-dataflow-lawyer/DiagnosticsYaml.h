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
#ifndef LLVM_CLANG_DATAFLOW_DIAGNOSTICSYAML_H
#define LLVM_CLANG_DATAFLOW_DIAGNOSTICSYAML_H

#include "Diags.h"
#include "llvm/Support/YAMLTraits.h"
#include <string>
LLVM_YAML_IS_SEQUENCE_VECTOR(PragmaRewriteDiag)

namespace llvm {
namespace yaml {
template <> struct MappingTraits<PragmaRewriteDiag> {
  /// \brief Helper to (de)serialize a Diagnostic since we don't have direct
  /// access to its data members.
  class NormalizedDiagnostic {
  public:
    NormalizedDiagnostic(const IO &)
        : DiagLevel(clang::DiagnosticsEngine::Level::Warning) {}

    NormalizedDiagnostic(const IO &, const PragmaRewriteDiag &D)
        : DiagnosticName(D.DiagnosticName), Message(D.Message),
          DiagLevel(D.DiagLevel) {}

    PragmaRewriteDiag denormalize(const IO &) {
      return PragmaRewriteDiag(DiagnosticName, Message, DiagLevel);
    }

    std::string DiagnosticName;
    PragmaRewriteMessage Message;
    clang::DiagnosticsEngine::Level DiagLevel;
  };

  static void mapping(IO &Io, PragmaRewriteDiag &D) {
    MappingNormalization<NormalizedDiagnostic, PragmaRewriteDiag> Keys(Io, D);
    Io.mapRequired("DiagnosticName", Keys->DiagnosticName);
    Io.mapRequired("Message", Keys->Message.Message);
    Io.mapRequired("FileOffset", Keys->Message.FileOffset);
    Io.mapRequired("FilePath", Keys->Message.FilePath);
  }
};

/// \brief Specialized MappingTraits to describe how a
/// TranslationUnitDiagnostics is (de)serialized.
template <> struct MappingTraits<TUDiagnostics> {
  static void mapping(IO &Io, TUDiagnostics &Doc) {
    Io.mapRequired("MainSourceFile", Doc.MainSourceFile);
    Io.mapRequired("Diagnostics", Doc.Diagnostics);
  }
};
}
}
#endif // LLVM_CLANG_DATAFLOW_DIAGNOSTICSYAML_H
