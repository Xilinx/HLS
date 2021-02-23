//===--- SPIR.cpp - Implement SPIR target feature support -----------------===//
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
// This file implements SPIR TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "SPIR.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Basic/TargetInfo.h"

using namespace clang;
using namespace clang::targets;

const Builtin::Info SPIRTargetInfo::BuiltinInfo[] = {
#define BUILTIN(ID, TYPE, ATTRS)                                               \
  {#ID, TYPE, ATTRS, nullptr, ALL_OCLC_LANGUAGES, nullptr},
#include "clang/Basic/BuiltinsSPIR.def"
};

void SPIRTargetInfo::getTargetDefines(const LangOptions &Opts,
                                      MacroBuilder &Builder) const {
  DefineStd(Builder, "SPIR", Opts);
}

ArrayRef<Builtin::Info> SPIRTargetInfo::getTargetBuiltins() const {
  return llvm::makeArrayRef(BuiltinInfo, clang::SPIR::LastTSBuiltin -
                                             Builtin::FirstTSBuiltin);
}

void SPIR32TargetInfo::getTargetDefines(const LangOptions &Opts,
                                        MacroBuilder &Builder) const {
  DefineStd(Builder, "SPIR32", Opts);
}

void SPIR64TargetInfo::getTargetDefines(const LangOptions &Opts,
                                        MacroBuilder &Builder) const {
  DefineStd(Builder, "SPIR64", Opts);
}
