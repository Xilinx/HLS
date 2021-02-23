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

#include "MCTargetDesc/FPGAMCTargetDesc.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

Target llvm::TheFPGA32Target, llvm::TheFPGA64Target;

extern "C" void LLVMInitializeFPGATargetInfo() {
  RegisterTarget<Triple::fpga32, /*HasJIT=*/false> X(
      TheFPGA32Target, "fpga32",
      "FPGA: Xilinx 7series and above (32-bit address)", "FPGA");
  RegisterTarget<Triple::fpga64, /*HasJIT=*/false> Y(
      TheFPGA64Target, "fpga64",
      "FPGA: Xilinx 7series and above (64-bit address)", "FPGA");
}
