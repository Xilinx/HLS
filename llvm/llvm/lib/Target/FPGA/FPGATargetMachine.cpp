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
// This file defines the FPGA specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#include "FPGATargetMachine.h"
#include "FPGA.h"
#include "FPGATargetTransformInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Target/TargetOptions.h"
using namespace llvm;

namespace llvm {
void initializeWinEHStatePassPass(PassRegistry &);
}

extern "C" void LLVMInitializeFPGATarget() {
  // Register the target.
  RegisterTargetMachine<FPGATargetMachine> X(TheFPGA32Target);
  RegisterTargetMachine<FPGATargetMachine> Y(TheFPGA64Target);

  // Initialize target specific passes
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  (void)PR;
}

static std::string computeDataLayout(const Triple &TT) {
  if (TT.isArch32Bit())
    // i786: e-m:e-p:32:32-f64:32:64-f80:32-n8:16:32-S128
    // spir: e-p:32:32-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024
    return
      "e-m:e-p:32:32-"
      "i64:64-i128:128-i256:256-i512:512-i1024:1024-i2048:2048-i4096:4096-"
      "n8:16:32:64-S128-"
      "v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024";

  // x86_64: e-m:e-i64:64-f80:128-n8:16:32:64-S128
  // spir64:
  // e-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024
  return
    "e-m:e-"
    "i64:64-i128:128-i256:256-i512:512-i1024:1024-i2048:2048-i4096:4096-"
    "n8:16:32:64-S128-"
    "v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024";
}

static Reloc::Model getEffectiveRelocModel(const Triple &TT,
                                           Optional<Reloc::Model> RM) {
  return Reloc::Static;
}

/// Create an FPGA target.
///
FPGATargetMachine::FPGATargetMachine(const Target &T, const Triple &TT,
                                     StringRef CPU, StringRef FS,
                                     const TargetOptions &Options,
                                     Optional<Reloc::Model> RM,
                                     Optional<CodeModel::Model> CM,
                                     CodeGenOpt::Level OL, bool JIT)
    : LLVMTargetMachine(T, computeDataLayout(TT), TT, CPU, FS, Options,
                        getEffectiveRelocModel(TT, RM), CodeModel::Large, OL),
      Subtarget(TT, CPU, FS, *this) {}

FPGATargetMachine::~FPGATargetMachine() {}

const FPGASubtarget *
FPGATargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute FPGAAttr = F.getFnAttribute("target-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  StringRef FPGA = !FPGAAttr.hasAttribute(Attribute::None)
                       ? FPGAAttr.getValueAsString()
                       : (StringRef)TargetCPU;
  StringRef FS = !FSAttr.hasAttribute(Attribute::None)
                     ? FSAttr.getValueAsString()
                     : (StringRef)TargetFS;

  SmallString<512> Key;
  Key.reserve(FPGA.size() + FS.size());
  Key += FPGA;
  Key += FS;

  auto &I = SubtargetMap[Key];
  if (!I) {
    resetTargetOptions(F);
    I = llvm::make_unique<FPGASubtarget>(TargetTriple, FPGA, FS, *this);
  }

  return I.get();
}

//===----------------------------------------------------------------------===//
// FPGA TTI query.
//===----------------------------------------------------------------------===//

TargetTransformInfo FPGATargetMachine::getTargetTransformInfo(const Function &F) {
  return TargetTransformInfo(FPGATTIImpl(this, F));
}

//===----------------------------------------------------------------------===//
// Pass Pipeline Configuration
//===----------------------------------------------------------------------===//

namespace {
/// FPGA Code Generator Pass Configuration Options.
class FPGAPassConfig : public TargetPassConfig {
public:
  FPGAPassConfig(FPGATargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  FPGATargetMachine &getFPGATargetMachine() const {
    return getTM<FPGATargetMachine>();
  }
};
} // namespace

TargetPassConfig *FPGATargetMachine::createPassConfig(PassManagerBase &PM) {
  return new FPGAPassConfig(*this, PM);
}
