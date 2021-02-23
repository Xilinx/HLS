// (C) Copyright 2016-2020 Xilinx, Inc.
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
// This header file pulls in all the functions of the debug helpers library so
// that tools like llc, opt, and lli can ensure they are linked with all symbols
// from libSupport.a. It should only be used from a tool's main program.
//
// To add a new helper:
//  - Declare it in 'llvm/include/llvm/DebugHelpers.h'
//  - Implement it in 'llvm/lib/DebugHelpers.cpp'
//  - Call it with dummy arguments in 'llvm/include/llvm/LinkAllDebugHelpers.h'
//    (here)
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LINKALLDEBUGHELPERS_H
#define LLVM_LINKALLDEBUGHELPERS_H

#include "llvm/DebugHelpers.h"
#include <cstdlib>

#ifndef NDEBUG

namespace {
  struct ForceDebugHelpersLinking {
    ForceDebugHelpersLinking() {
      // We must reference debug helpers in such a way that compilers will not
      // delete them all as dead code, even with whole program optimization,
      // yet is effectively a NO-OP. As the compiler isn't smart enough
      // to know that getenv() never returns -1, this will do the job.
      if (std::getenv("bar") != (char*) -1)
        return;

      // Those calls are dummy calls so that the compiler think those helpers
      // are necessary, as they are used at least once.
      dumpFuncs(nullptr);
    }
  } ForceDebugHelpersLinking;
}

#endif

#endif
