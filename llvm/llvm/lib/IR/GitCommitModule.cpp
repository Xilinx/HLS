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
// GitCommitModulePass implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/GitCommitModule.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#define DEBUG_TYPE "git-commit-module"
using namespace llvm;

static int TagCount = 0;
static std::string NextTag;

void GitCommitModulePass::setNextTag(const Twine &Tag) { NextTag = Tag.str(); }

GitCommitModulePass::GitCommitModulePass() {}
GitCommitModulePass::GitCommitModulePass(const std::string &GitRepo,
                                         const std::string &Message,
                                         bool ShouldPreserveUseListOrder)
    : GitRepo(GitRepo), Message(Message),
      ShouldPreserveUseListOrder(ShouldPreserveUseListOrder) {
  if(!init())
    report_fatal_error("Folder is not writeable?");
}

#ifndef NDEBUG
static void dumpCommand(std::string Prog, ArrayRef<const char *> Args) {
  dbgs() << "(" << Prog << ")";
  for(auto Arg : Args)
    if (Arg)
      dbgs() << " '" << Arg << "'";
  dbgs() << "\n";
}
#endif

template <typename... T> static bool git(T... Args) {
  auto Git = sys::findProgramByName("git");
  if (!Git) {
    DEBUG(dbgs() << "Cannot find 'git' in PATH: " << Git.getError().message()
                 << "\n");
    return false;
  }
  const char *FullArgs[] = {"git", Args..., nullptr};
  DEBUG(dumpCommand(*Git, FullArgs));
  return (sys::ExecuteAndWait(*Git, FullArgs) == 0);
}

bool GitCommitModulePass::init() {
  return git("init", "--quiet", GitRepo.c_str());
}

bool GitCommitModulePass::printModule(Module &M) {
  SmallString<128> FileName(sys::path::filename(M.getName()));
  sys::path::replace_extension(FileName, "ll");

  SmallString<128> AbsFileName(GitRepo);
  sys::path::append(AbsFileName, FileName);
  std::error_code EC;
  raw_fd_ostream OS(AbsFileName, EC, sys::fs::F_Text);

  if (EC)
    return false;

  if (llvm::isFunctionInPrintList("*"))
    M.print(OS, nullptr, ShouldPreserveUseListOrder);
  else {
    for (const auto &F : M.functions())
      if (llvm::isFunctionInPrintList(F.getName()))
        F.print(OS);
  }

  return git("-C", GitRepo.c_str(), "add", FileName.c_str());
}

bool GitCommitModulePass::commit() {
  return git("-C", GitRepo.c_str(), "commit", "--allow-empty", "--quiet", "-m",
             Message.c_str());
}

bool GitCommitModulePass::tag() {
  if (NextTag.empty())
    return true;

  std::string Tag = "";
  {
    raw_string_ostream OS(Tag);
    OS << format("%02d-", TagCount) << NextTag;
  }

  ++TagCount;
  NextTag = "";

  return git("-C", GitRepo.c_str(), "tag", Tag.c_str());
}

PreservedAnalyses GitCommitModulePass::run(Module &M,
                                           AnalysisManager<Module> &) {
  if (!printModule(M))
    report_fatal_error("File is not writeable?");
  if (!commit())
    report_fatal_error("Git repository is broken?");
  if (!tag())
    report_fatal_error("Tag already exists?");
  return PreservedAnalyses::all();
}
