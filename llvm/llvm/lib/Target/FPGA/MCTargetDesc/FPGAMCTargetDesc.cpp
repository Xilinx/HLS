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
// This file provides FPGA specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "FPGAMCTargetDesc.h"
#include "llvm/ADT/Triple.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MachineLocation.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetRegistry.h"

#if _MSC_VER
#include <intrin.h>
#endif

using namespace llvm;

#define GET_SUBTARGETINFO_MC_DESC
#include "FPGAGenSubtargetInfo.inc"

std::string FPGA_MC::ParseFPGATriple(const Triple &TT) {
  std::string FS;
  return FS;
}

MCSubtargetInfo *FPGA_MC::createFPGAMCSubtargetInfo(const Triple &TT,
                                                    StringRef CPU,
                                                    StringRef FS) {
  std::string ArchFS = FPGA_MC::ParseFPGATriple(TT);
  if (!FS.empty()) {
    if (!ArchFS.empty())
      ArchFS = (Twine(ArchFS) + "," + FS).str();
    else
      ArchFS = FS;
  }

  std::string CPUName = CPU;
  if (CPUName.empty())
    CPUName = "generic";

  return createFPGAMCSubtargetInfoImpl(TT, CPUName, ArchFS);
}

// Force static initialization.
extern "C" void LLVMInitializeFPGATargetMC() {
  // No MC info, analysis, parser or printer to register.
}
