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
// This is a target description file for the Xilinx FPGA architecture, referred
// to here as the "FPGA" architecture.
//
//===----------------------------------------------------------------------===//

// Get the target-independent interfaces which we are implementing...
//
include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// FPGA Subtarget state
//


def Ultrascale : SubtargetFeature<"ultrascale", "IsUltrascale", "true",
                                  "Enable Ultrascale features">;

//===----------------------------------------------------------------------===//
// FPGA Subtarget features
//===----------------------------------------------------------------------===//

def FeatureDSP48E1
    : SubtargetFeature<"dsp48e1", "DSP48E1",
                       "true", "Use DSP48E1 slices">;
def FeatureDSP48E2
    : SubtargetFeature<"dsp48e2", "DSP48E2",
                       "true", "Use DSP48E2 slices">;

//===----------------------------------------------------------------------===//
// FPGA processors supported.
//===----------------------------------------------------------------------===//

include "FPGASchedule.td"

class Proc<string Name, list<SubtargetFeature> Features>
 : ProcessorModel<Name, GenericModel, Features>;

class ProcessorFeatures<list<SubtargetFeature> Inherited,
                        list<SubtargetFeature> NewFeatures> {
  list<SubtargetFeature> Value = !listconcat(Inherited, NewFeatures);
}

class ProcModel<string Name, SchedMachineModel Model,
                list<SubtargetFeature> ProcFeatures,
                list<SubtargetFeature> OtherFeatures> :
  ProcessorModel<Name, Model, !listconcat(ProcFeatures, OtherFeatures)>;

def : Proc<"serie7",      [FeatureDSP48E1]>;
def : Proc<"ultrascale",  [FeatureDSP48E2]>;
def : Proc<"ultrascalep", [FeatureDSP48E2]>;

// We also provide a generic fpga processor model which tries to be good for
// modern chips, and enables features that are generally beneficial.
def : ProcessorModel<"fpga", UltraScaleModel,
                     [FeatureDSP48E2]>;

//===----------------------------------------------------------------------===//
// Register File Description
//===----------------------------------------------------------------------===//

//include "FPGARegisterInfo.td"

//===----------------------------------------------------------------------===//
// Instruction Descriptions
//===----------------------------------------------------------------------===//

//include "FPGAInstrInfo.td"

//def FPGAInstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// Calling Conventions
//===----------------------------------------------------------------------===//

//include "FPGACallingConv.td"

//===----------------------------------------------------------------------===//
// Assembly Printers
//===----------------------------------------------------------------------===//

// The FPGA target supports three different syntaxes for emitting HDL.
// This is controlled by the -fpga-hdl-syntax={verilog|vhdl|systemc}
// FIXME They do nothing yet FIXME
def VerilogWriter : AsmWriter {
  string AsmWriterClassName  = "VerilogInstPrinter";
  int Variant = 0;
}
def VHDLWriter : AsmWriter {
  string AsmWriterClassName  = "VHDLInstPrinter";
  int Variant = 1;
}
def SystemCWriter : AsmWriter {
  string AsmWriterClassName  = "SystemCInstPrinter";
  int Variant = 2;
}

def FPGA : Target {
  // Information about the instructions...
  //let InstructionSet = FPGAInstrInfo;
  let AssemblyWriters = [VerilogWriter, VHDLWriter, SystemCWriter];
  let AssemblyParserVariants = [];
}
