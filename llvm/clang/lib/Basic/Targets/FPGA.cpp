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
// This file implements FPGA TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "FPGA.h"
#include "Targets.h"
#include "clang/Basic/TargetBuiltins.h"

using namespace clang;
using namespace clang::targets;

const Builtin::Info FPGATargetInfo::BuiltinInfo[] = {
#define BUILTIN(ID, TYPE, ATTRS) {#ID, TYPE, ATTRS, nullptr, HLS_LANG, nullptr},
#include "clang/Basic/BuiltinsFPGA.def"
};

ArrayRef<Builtin::Info> FPGATargetInfo::getTargetBuiltins() const {
  return llvm::makeArrayRef(BuiltinInfo, clang::FPGA::LastTSBuiltin -
                                             Builtin::FirstTSBuiltin);
}

void FPGATargetInfo::getTargetDefines(const LangOptions &Opts,
                                      MacroBuilder &Builder) const {
  DefineStd(Builder, "FPGA", Opts);

  if (getTriple().isWindowsGNUEnvironment()) {
    Builder.defineMacro("_INT128_DEFINED");
    Builder.defineMacro("__int8", "char");
    Builder.defineMacro("__int16", "short");
    Builder.defineMacro("__int32", "int");
    Builder.defineMacro("__int64", "long");
  }
}

FPGA32TargetInfo::FPGA32TargetInfo(const llvm::Triple &Triple,
                                   const TargetOptions &Opts)
    : FPGATargetInfo(Triple, Opts) {
  PointerWidth = PointerAlign = 32;
  SizeType = TargetInfo::UnsignedInt;
  PtrDiffType = IntPtrType = TargetInfo::SignedInt;
  resetDataLayout(
      "e-m:e-p:32:32-"
      "i64:64-i128:128-i256:256-i512:512-i1024:1024-i2048:2048-i4096:4096-"
      "n8:16:32:64-S128-"
      "v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:"
      "1024");
}

void FPGA32TargetInfo::getTargetDefines(const LangOptions &Opts,
                                        MacroBuilder &Builder) const {
  FPGATargetInfo::getTargetDefines(Opts, Builder);
  DefineStd(Builder, "FPGA32", Opts);
}

FPGA64TargetInfo::FPGA64TargetInfo(const llvm::Triple &Triple,
                                   const TargetOptions &Opts)
    : FPGATargetInfo(Triple, Opts) {
  PointerWidth = PointerAlign = 64;
  SizeType = TargetInfo::UnsignedLong;
  PtrDiffType = IntPtrType = TargetInfo::SignedLong;
  resetDataLayout(
      "e-m:e-"
      "i64:64-i128:128-i256:256-i512:512-i1024:1024-i2048:2048-i4096:4096-"
      "n8:16:32:64-S128-"
      "v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:"
      "1024");
}

void FPGA64TargetInfo::getTargetDefines(const LangOptions &Opts,
                                        MacroBuilder &Builder) const {
  FPGATargetInfo::getTargetDefines(Opts, Builder);
  DefineStd(Builder, "FPGA64", Opts);
  DefineStd(Builder, "x86_64", Opts);
}
