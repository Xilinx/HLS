//===------ argvextra.h ------ Command Line Arguments Extra Handling ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// And has the following additional copyright:
//
// (C) Copyright 2016-2020 Xilinx, Inc.
// All Rights Reserved.
//
//===----------------------------------------------------------------------===//
//
// This file implement a helper class to extend and record the command line
// arguments of cc1_main.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DRIVER_ARGVEXTRA_H
#define LLVM_CLANG_DRIVER_ARGVEXTRA_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <vector>

#ifndef LLVM_ON_WIN32
#include <wordexp.h>
#endif

class ArgvExtra {
  ArrayRef<const char *> Argv;
  std::vector<std::string> ExtraArgs;
  std::vector<const char *> AllArgv;

public:
  ArgvExtra(ArrayRef<const char *> Argv) : Argv(Argv) {}

public:
  void appendExtraArgs(const char *Extra) {
#ifndef LLVM_ON_WIN32
    // Perform word expansion (copy the strings to be able to clean-up)
    wordexp_t WEResult;
    wordexp(Extra, &WEResult, 0);
    for (size_t i = 0; i < WEResult.we_wordc; ++i)
      ExtraArgs.emplace_back(WEResult.we_wordv[i]);
    wordfree(&WEResult);

    // Copy all original Argv (as we can't extend an ArrayRef)
    AllArgv = Argv.vec();
    for (const auto &s : ExtraArgs)
      AllArgv.push_back(s.c_str());
#endif
  }

  void recordInit(const char *Filename, ArrayRef<StringRef> UnsetEnv = {}) {
    // Create the file (error out if already exists)
    std::error_code EC;
    llvm::raw_fd_ostream OS(Filename, EC,
                            llvm::sys::fs::F_Excl | llvm::sys::fs::F_Text);

    // File already exists (assume it is initialized)
    if (EC)
      return;

    OS << "#!/bin/bash\n";
    if (!UnsetEnv.empty()) {
      OS << "unset";
      for (auto E : UnsetEnv)
        OS << " " << E;
      OS << "\n";
    }
  }

  void recordCommand(const char *Filename, const char *Argv0,
                     const char *CWD = nullptr) {
    std::error_code EC;
    llvm::raw_fd_ostream OS(Filename, EC,
                            llvm::sys::fs::F_Append | llvm::sys::fs::F_Text);

    // Record:
    //   (cd "$PWD"; "$0" "-cc1" "$@")
    // or
    //   "$0" "-cc1" "$@"

    if (CWD != nullptr) {
      OS << "(cd \"";
      PrintEscapedString(CWD, OS);
      OS << "\" && ";
    }

    OS << "\"";
    PrintEscapedString(Argv0, OS);
    OS << "\" \"-cc1\"";
    for (auto s : getAllArgv()) {
      OS << " \"";
      PrintEscapedString(s, OS);
      OS << "\"";
    }

    if (CWD != nullptr)
      OS << ")";

    OS << "\n";
  }

  ArrayRef<const char *> getAllArgv() {
    if (AllArgv.empty())
      return Argv;

    return ArrayRef<const char *>(AllArgv);
  }
};

#endif
