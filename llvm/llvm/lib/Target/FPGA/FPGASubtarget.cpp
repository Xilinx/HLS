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
// This file implements the FPGA specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "FPGASubtarget.h"
#include "FPGATargetMachine.h"
#include "MCTargetDesc/FPGAMCTargetDesc.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

using namespace llvm;

#define DEBUG_TYPE "subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "FPGAGenSubtargetInfo.inc"

void FPGASubtarget::initializeEnvironment() {}

FPGASubtarget &FPGASubtarget::initializeSubtargetDependencies(StringRef CPU,
                                                              StringRef FS) {
  initializeEnvironment();
  initSubtargetFeatures(CPU, FS);
  return *this;
}

FPGASubtarget::FPGASubtarget(const Triple &TT, StringRef CPU, StringRef FS,
                             const FPGATargetMachine &TM)
    : FPGAGenSubtargetInfo(TT, CPU, FS), FPGAFamily(UltraScale), TM(TM),
      TargetTriple(TT) {}

void FPGASubtarget::initSubtargetFeatures(StringRef CPU, StringRef FS) {
  std::string FPGAName = CPU;
  if (FPGAName.empty())
    FPGAName = "generic";

  std::string FullFS = FS;

  ParseSubtargetFeatures(FPGAName, FullFS);
}
