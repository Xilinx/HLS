// (C) Copyright 2016-2020 Xilinx, Inc.
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
///
/// This file defines passes to write out IR and commit it into a Git
/// repository. The GitCommitModulePass pass simply initialize a Git repository
/// when created, then write out and commit the entire module when it is
/// executed.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_GITCOMMITMODULE_H
#define LLVM_IR_GITCOMMITMODULE_H

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include <string>

namespace llvm {
class Module;
class PreservedAnalyses;
template <typename IRUnitT, typename... ExtraArgTs> class AnalysisManager;

/// \brief Pass for printing and committing a Module as LLVM's text IR assembly.
///
/// Note: This pass is for use with the new pass manager. Use the create...Pass
/// functions above to create passes for use with the legacy pass manager.
class GitCommitModulePass {
  SmallString<128> GitRepo;
  std::string Message;
  bool ShouldPreserveUseListOrder;

protected:
  bool init();
  bool printModule(Module &M);
  bool commit();
  bool tag();

public:
  // Can be used by any Pass to tag itself:
  //
  // MyPass::runOnFunction(Function &F) {
  //   GitCommitModulePass::setNextTag(Twine(DEBUG_TAG) + "-" + F.getName());
  //
  //   ...
  // }
  static void setNextTag(const Twine &Tag);

public:
  GitCommitModulePass();
  GitCommitModulePass(const std::string &GitRepo, const std::string &Message,
                      bool ShouldPreserveUseListOrder = false);

  PreservedAnalyses run(Module &M, AnalysisManager<Module> &);

  static StringRef name() { return "GitCommitModulePass"; }
};
} // namespace llvm

// Provides the implementation of the wrapper pass (wrap it into a KindPass).
//
// That is split into pieces to handle RegionPass, LoopPass and CallGraphSCCPass
#define GIT_COMMIT_MODULE_PASS_WRAPPER(Kind, GetModule)                        \
  GIT_COMMIT_MODULE_PASS_WRAPPER_INTERNAL(                                     \
      Kind, GetModule, GIT_COMMIT_MODULE_PASS_WRAPPER_RUN_ON)
#define GIT_COMMIT_MODULE_PASS_WRAPPER_RUN_ON(Kind, K) runOn##Kind(Kind &K)
#define GIT_COMMIT_MODULE_PASS_WRAPPER_INTERNAL(Kind, GetModule, RUN_ON)       \
  namespace {                                                                  \
  class GitCommit##Kind##PassWrapper : public Kind##Pass {                     \
    GitCommitModulePass P;                                                     \
                                                                               \
  public:                                                                      \
    static char ID;                                                            \
    GitCommit##Kind##PassWrapper(const std::string &GitRepo,                   \
                                 const std::string &Message,                   \
                                 bool ShouldPreserveUseListOrder = false)      \
        : Kind##Pass(ID), P(GitRepo, Message, ShouldPreserveUseListOrder) {}   \
                                                                               \
    bool RUN_ON(Kind, K) override {                                            \
      ModuleAnalysisManager DummyMAM;                                          \
      P.run(GetModule(K), DummyMAM);                                           \
      return false;                                                            \
    }                                                                          \
                                                                               \
    void getAnalysisUsage(AnalysisUsage &AU) const override {                  \
      AU.setPreservesAll();                                                    \
    }                                                                          \
  };                                                                           \
  }                                                                            \
  char GitCommit##Kind##PassWrapper::ID = 0;

#endif
