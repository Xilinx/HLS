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
// This file declares FPGA TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_FPGA_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_FPGA_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Compiler.h"

namespace clang {
namespace targets {

static const unsigned FPGAAddrSpaceMap[] = {
    0, // Default
    1, // opencl_global
    3, // opencl_local
    2, // opencl_constant
    0, // opencl_private
    4, // opencl_generic
    0, // cuda_device
    0, // cuda_constant
    0  // cuda_shared
};

class LLVM_LIBRARY_VISIBILITY FPGATargetInfo : public TargetInfo {
  static const Builtin::Info BuiltinInfo[];

public:
  FPGATargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    // assert(getTriple().getOS() == llvm::Triple::UnknownOS &&
    //        "FPGA target must use unknown OS");
    assert((getTriple().getEnvironment() == llvm::Triple::UnknownEnvironment ||
            getTriple().getEnvironment() == llvm::Triple::GNU) &&
           "FPGA target must use unknown or gnu environment type");
    RegParmMax = 6;
    TLSSupported = false;
    LongWidth = LongAlign = 64;
    AddrSpaceMap = &FPGAAddrSpaceMap;
    UseAddrSpaceMapMangling = true;
    // Define available target features
    // These must be defined in sorted order!
    NoAsmVariants = true;
    HasFloat128 = true; // FIXME: Needed to workaround libstdc++ < version 8
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  bool hasFeature(StringRef Feature) const override {
    return Feature == "FPGA";
  }

  ArrayRef<Builtin::Info> getTargetBuiltins() const override;

  const char *getClobbers() const override { return ""; }

  ArrayRef<const char *> getGCCRegNames() const override { return None; }

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &info) const override {
    return true;
  }

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    return None;
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::VoidPtrBuiltinVaList;
  }

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override {
    return (CC == CC_OpenCLKernel || CC == CC_C) ? CCCR_OK : CCCR_Warning;
  }

  CallingConv getDefaultCallingConv(CallingConvMethodType MT) const override {
    return CC_C;
  }

  void setSupportedOpenCLOpts() override {
    // Assume all OpenCL extensions and optional core features are supported
    // for FPGA since it is a generic target.
    getSupportedOpenCLOpts().supportAll();
  }
};

class LLVM_LIBRARY_VISIBILITY FPGA32TargetInfo : public FPGATargetInfo {
public:
  FPGA32TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};

class LLVM_LIBRARY_VISIBILITY FPGA64TargetInfo : public FPGATargetInfo {
public:
  FPGA64TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};
} // namespace targets
} // namespace clang
#endif // LLVM_CLANG_LIB_BASIC_TARGETS_FPGA_H
