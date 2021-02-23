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

#include "../ClangTidy.h"
#include "../ClangTidyModule.h"
#include "../ClangTidyModuleRegistry.h"
#include "../readability/BracesAroundStatementsCheck.h"
#include "../readability/FunctionSizeCheck.h"
#include "../readability/NamespaceCommentCheck.h"
#include "../readability/RedundantSmartptrGetCheck.h"
#include "AggregateOnApintCheck.h"
#include "ApintLiteralConvertationCheck.h"
#include "ArrayStreamCheckCheck.h"
#include "ConstantarrayParamCheck.h"
#include "Directive2pragmaCheck.h"
#include "DumpOpenclkernelCheck.h"
#include "LabelAllLoopsCheck.h"
#include "LoopBraceBracketCheck.h"
#include "RemoveAssertCheck.h"
#include "SsdmIntrinsicsArgumentsCheck.h"
#include "SsdmIntrinsicsScopeCheck.h"
#include "StreamInStructCheck.h"
#include "SystemcDetectorCheck.h"
#include "Tb31ProcessCheck.h"
#include "TbProcessCheck.h"
#include "TbRemoveTopCheck.h"
#include "TbXfmatCheck.h"
#include "TopParametersCheck.h"
#include "XfmatArrayGeometryCheck.h"
#include "WarnMayneedNoCtorAttributeCheck.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {

class XilinxModule : public ClangTidyModule {
public:
  void addCheckFactories(ClangTidyCheckFactories &CheckFactories) override {
    CheckFactories.registerCheck<AggregateOnApintCheck>(
        "xilinx-aggregate-on-apint");
    CheckFactories.registerCheck<ApintLiteralConvertationCheck>(
        "xilinx-apint-literal-convertation");
    CheckFactories.registerCheck<ArrayStreamCheckCheck>(
        "xilinx-array-stream-check");
    CheckFactories.registerCheck<ConstantarrayParamCheck>(
        "xilinx-constantarray-param");
    CheckFactories.registerCheck<Directive2pragmaCheck>(
        "xilinx-directive2pragma");
    CheckFactories.registerCheck<DumpOpenclkernelCheck>(
        "xilinx-dump-openclkernel");
    CheckFactories.registerCheck<LabelAllLoopsCheck>(
        "xilinx-label-all-loops");
    CheckFactories.registerCheck<LoopBraceBracketCheck>(
        "xilinx-loop-brace-bracket");
    CheckFactories.registerCheck<RemoveAssertCheck>("xilinx-remove-assert");
    CheckFactories.registerCheck<SsdmIntrinsicsArgumentsCheck>(
        "xilinx-ssdm-intrinsics-arguments");
    CheckFactories.registerCheck<SsdmIntrinsicsScopeCheck>(
        "xilinx-ssdm-intrinsics-scope");
    CheckFactories.registerCheck<StreamInStructCheck>(
        "xilinx-stream-in-struct");
    CheckFactories.registerCheck<SystemcDetectorCheck>(
        "xilinx-systemc-detector");
    CheckFactories.registerCheck<Tb31ProcessCheck>("xilinx-tb31-process");
    CheckFactories.registerCheck<TbProcessCheck>("xilinx-tb-process");
    CheckFactories.registerCheck<TbRemoveTopCheck>("xilinx-remove-top");
    CheckFactories.registerCheck<TbXfmatCheck>("xilinx-tb-xfmat");
    CheckFactories.registerCheck<TopParametersCheck>("xilinx-top-parameters");
    CheckFactories.registerCheck<XfmatArrayGeometryCheck>(
        "xilinx-xfmat-array-geometry");
    CheckFactories.registerCheck<WarnMayneedNoCtorAttributeCheck>(
        "xilinx-warn-mayneed-no-ctor-attribute");
  }

  ClangTidyOptions getModuleOptions() override {
    ClangTidyOptions Options;
    auto &Opts = Options.CheckOptions;
    return Options;
  }
};

// Register the XilinxTidyModule using this statically initialized variable.
static ClangTidyModuleRegistry::Add<XilinxModule> X("xilinx-module",
                                                    "Adds Xilinx lint checks.");

} // namespace xilinx

// This anchor is used to force the linker to link in the generated object file
// and thus register the XilinxModule.
volatile int XilinxModuleAnchorSource = 0;

} // namespace tidy
} // namespace clang
