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

#ifndef LLVM_LIB_TARGET_FPGA_MCTARGETDESC_FPGAMCTARGETDESC_H
#define LLVM_LIB_TARGET_FPGA_MCTARGETDESC_FPGAMCTARGETDESC_H

#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/DataTypes.h"
#include <string>

namespace llvm {
class MCSubtargetInfo;
class Target;
class Triple;
class StringRef;
class raw_ostream;
class raw_pwrite_stream;

extern Target TheFPGA32Target;
extern Target TheFPGA64Target;

namespace FPGA_MC {
std::string ParseFPGATriple(const Triple &TT);

/// Create a FPGA MCSubtargetInfo instance. This is exposed so Asm parser, etc.
/// do not need to go through TargetRegistry.
MCSubtargetInfo *createFPGAMCSubtargetInfo(const Triple &TT, StringRef CPU,
                                           StringRef FS);
}

} // End llvm namespace

#define GET_SUBTARGETINFO_ENUM
#include "FPGAGenSubtargetInfo.inc"

#endif
