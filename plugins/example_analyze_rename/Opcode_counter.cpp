// (C) Copyright 2016-2021 Xilinx, Inc.
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
//

//===----------------------------------------------------------------------===//
//
// Analyze, opcode stats
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

struct CountOp: public FunctionPass {
  static char ID;
  std::map<std::string, int> opcode_counter;
  CountOp() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override;
};

char CountOp::ID = 0;
static RegisterPass<CountOp> X("opcode_count", "coutning number of opcodes of each type",
                                false /* Only looks at CFG */,
                                false /* Transformation Pass */);

bool CountOp::runOnFunction(Function& function) {
    if (skipFunction(function))
        return false;

    for (auto& basic_block : function)
        for (auto& instruction : basic_block) {
            if (opcode_counter.find(instruction.getOpcodeName()) == opcode_counter.end()) {
                opcode_counter[instruction.getOpcodeName()] = 1;
            } else {
                opcode_counter[instruction.getOpcodeName()]++;
            }
        }
    dbgs() << "\n.:: Opcode stats ::.\n";
    for (auto k_v : opcode_counter) {
        dbgs() << std::string(5, ' ') << k_v.first << ":" << k_v.second << "\n";
    }
    opcode_counter.clear();


    // We didn't modify the IR (analysis only)
    return false;
}
