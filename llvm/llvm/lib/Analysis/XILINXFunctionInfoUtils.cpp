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

#include "llvm/Analysis/XILINXFunctionInfoUtils.h"
#include <cassert>

using namespace llvm;

bool llvm::isDataFlow(const Function *F) {
  return F->hasFnAttribute("fpga.dataflow.func");
}

/// \brief Captures function pipeline information.
class PipelineInfo {
  long long II;        // target II
  PipelineStyle Style; // pipeline style

public:
  PipelineInfo(long long II, PipelineStyle Style) : II(II), Style(Style) {}

  long long getII() const { return II; }
  PipelineStyle getStyle() const { return Style; }
};

/// Get function \p F pipeline inforamtion: II, Style.
static Optional<PipelineInfo> GetPipelineInfo(const Function *F) {
  if (!F->hasFnAttribute("fpga.static.pipeline"))
    return None;

  auto P = F->getFnAttribute("fpga.static.pipeline");
  std::pair<StringRef, StringRef> PipeLineInfoStr =
      P.getValueAsString().split(".");
  long long II;
  // https://reviews.llvm.org/D24778 indicates "getAsSignedInteger" returns true
  // for failure, false for success because of convention.
  if (getAsSignedInteger(PipeLineInfoStr.first, 10, II))
    return None;

  long long StyleCode;
  if (getAsSignedInteger(PipeLineInfoStr.second, 10, StyleCode))
    return None;

  assert((StyleCode >= -1) && (StyleCode <= 2) && "unexpected pipeline style!");
  PipelineStyle Style = static_cast<PipelineStyle>(StyleCode);
  return PipelineInfo(II, Style);
}

bool llvm::isPipeline(const Function *F) {
  Optional<PipelineInfo> PInfo = GetPipelineInfo(F);
  if (!PInfo.hasValue())
    return false;

  return PInfo.getValue().getII();
}

Optional<PipelineStyle> llvm::getPipelineStyle(const Function *F) {
  Optional<PipelineInfo> PInfo = GetPipelineInfo(F);
  if (!PInfo.hasValue())
    return None;

  return PInfo.getValue().getStyle();
}

bool llvm::isPipelineOff(const Function *F) {
  Optional<PipelineInfo> PInfo = GetPipelineInfo(F);
  if (!PInfo.hasValue())
    return false;

  return (PInfo.getValue().getII() == 0);
}

Optional<long long> llvm::getPipelineII(const Function *F) {
  Optional<PipelineInfo> PInfo = GetPipelineInfo(F);
  if (!PInfo.hasValue())
    return None;

  auto II = PInfo.getValue().getII();
  if (II == 0)
    return None;

  return II;
}

/// NOTE: #pragma HLS inline marks function with Attribute::AlwaysInline
bool llvm::isAlwaysInline(const Function *F) {
  return F->hasFnAttribute(Attribute::AlwaysInline);
}

/// NOTE: #pragma HLS inline marks function with Attribute::AlwaysInline
bool llvm::isAlwaysInline(const CallSite CS) {
  return CS.hasFnAttr(Attribute::AlwaysInline);
}

/// NOTE: #pragma HLS inline off marks function with Attribute::NoInline
bool llvm::isNoInline(const Function *F) {
  return F->hasFnAttribute(Attribute::NoInline);
}

/// NOTE: #pragma HLS inline off marks function with Attribute::NoInline
bool llvm::isNoInline(const CallSite CS) {
  return CS.hasFnAttr(Attribute::NoInline);
}

bool llvm::isTop(const Function *F) {
  return F->hasFnAttribute("fpga.top.func");
}

Optional<const std::string> llvm::getTopFunctionName(const Function *F) {
  if (!isTop(F))
    return None;

  auto TFName = F->getFnAttribute("fpga.top.func").getValueAsString();
  if (!TFName.empty())
    return TFName.str();

  return None;
}
