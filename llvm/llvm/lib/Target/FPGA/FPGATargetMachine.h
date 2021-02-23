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
// This file declares the FPGA specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef REFLOW_LIB_TARGET_FPGA_FPGATARGETMACHINE_H
#define REFLOW_LIB_TARGET_FPGA_FPGATARGETMACHINE_H
#include "FPGASubtarget.h"
#include "MCTargetDesc/FPGAMCTargetDesc.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

class StringRef;

class FPGATargetMachine final : public LLVMTargetMachine {
  FPGASubtarget Subtarget;

  mutable StringMap<std::unique_ptr<FPGASubtarget>> SubtargetMap;

public:
  FPGATargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                    StringRef FS, const TargetOptions &Options,
                    Optional<Reloc::Model> RM, Optional<CodeModel::Model> CM,
                    CodeGenOpt::Level OL, bool JIT);
  ~FPGATargetMachine() override;
  const FPGASubtarget *getSubtargetImpl(const Function &F) const override;

  TargetTransformInfo getTargetTransformInfo(const Function &F) override;

  // Set up the pass pipeline.
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;
};

} // End llvm namespace

#endif
