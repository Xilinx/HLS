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
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/AsmParser/Parser.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace {

TEST(CloneModuleTest, Bug) {

  StringRef Mod = R"llvm(
target datalayout = "e-m:e-i64:64-i128:128-i256:256-i512:512-i1024:1024-i2048:2048-i4096:4096-n8:16:32:64-S128-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024"
target triple = "x86_64-unknown-linux-gnu"

%struct.s_cint32 = type { i64, i64 }

define void @_Z20top_target_generatorht(i8 zeroext %adc_in, i16 zeroext %delay_index) local_unnamed_addr !dbg !49 {
entry:
  %agg.tmp33 = alloca %struct.s_cint32, align 8
  call void @llvm.dbg.declare(metadata %struct.s_cint32* %agg.tmp33, metadata !50, metadata !DIExpression()), !dbg !51
  ret void
}

; Function Attrs: nounwind readnone speculatable
declare void @llvm.dbg.declare(metadata, metadata, metadata) #0

attributes #0 = { nounwind readnone speculatable }

!llvm.dbg.cu = !{!0, !7, !13, !26, !41}
!llvm.module.flags = !{!47, !48}

!0 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus, file: !1, producer: "clang version 7.0.0 ", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !2, globals: !3)
!1 = !DIFile(filename: "test1.cpp", directory: ".")
!2 = !{}
!3 = !{!4}
!4 = !DIGlobalVariableExpression(var: !5, expr: !DIExpression())
!5 = distinct !DIGlobalVariable(name: "target_simulator_delay", scope: !0, file: !1, line: 11, type: !6, isLocal: false, isDefinition: true)
!6 = !DIBasicType(name: "unsigned short", size: 16, encoding: DW_ATE_unsigned)
!7 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus, file: !1, producer: "clang version 7.0.0 ", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !2, retainedTypes: !2, globals: !8)
!8 = !{!9, !11}
!9 = !DIGlobalVariableExpression(var: !10, expr: !DIExpression())
!10 = distinct !DIGlobalVariable(name: "equalizer_scal", scope: !7, file: !1, line: 9, type: !6, isLocal: false, isDefinition: true)
!11 = !DIGlobalVariableExpression(var: !12, expr: !DIExpression())
!12 = distinct !DIGlobalVariable(name: "history", linkageName: "_ZL7history", scope: !7, file: !1, line: 5, type: !6, isLocal: true, isDefinition: true)
!13 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus, file: !1, producer: "clang version 7.0.0 ", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !2, globals: !14)
!14 = !{!15, !17, !22, !24}
!15 = !DIGlobalVariableExpression(var: !16, expr: !DIExpression())
!16 = distinct !DIGlobalVariable(name: "equalizer_scal", scope: !13, file: !1, line: 9, type: !6, isLocal: false, isDefinition: true)
!17 = !DIGlobalVariableExpression(var: !18, expr: !DIExpression())
!18 = distinct !DIGlobalVariable(name: "tmp_re", scope: !19, file: !1, line: 20, type: !6, isLocal: true, isDefinition: true)
!19 = distinct !DISubprogram(name: "equalizer", linkageName: "_Z9equalizer8s_cint32", scope: !1, file: !1, line: 13, type: !20, isLocal: false, isDefinition: true, scopeLine: 14, flags: DIFlagPrototyped, isOptimized: false, unit: !13, variables: !2)
!20 = !DISubroutineType(types: !21)
!21 = !{null}
!22 = !DIGlobalVariableExpression(var: !23, expr: !DIExpression())
!23 = distinct !DIGlobalVariable(name: "tmp_im", scope: !19, file: !1, line: 20, type: !6, isLocal: true, isDefinition: true)
!24 = !DIGlobalVariableExpression(var: !25, expr: !DIExpression())
!25 = distinct !DIGlobalVariable(name: "reset_counter", scope: !19, file: !1, line: 21, type: !6, isLocal: true, isDefinition: true)
!26 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus, file: !1, producer: "clang version 7.0.0 ", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !2, retainedTypes: !2, globals: !27)
!27 = !{!28, !32, !35, !37, !39}
!28 = !DIGlobalVariableExpression(var: !29, expr: !DIExpression())
!29 = distinct !DIGlobalVariable(name: "b", scope: !30, file: !1, line: 18, type: !6, isLocal: true, isDefinition: true)
!30 = distinct !DISubprogram(name: "compute_omega_x_dt", linkageName: "_Z18compute_omega_x_dt8s_cint32", scope: !1, file: !1, line: 14, type: !31, isLocal: false, isDefinition: true, scopeLine: 15, flags: DIFlagPrototyped, isOptimized: false, unit: !26, variables: !2)
!31 = !DISubroutineType(types: !2)
!32 = !DIGlobalVariableExpression(var: !33, expr: !DIExpression())
!33 = distinct !DIGlobalVariable(name: "acc", scope: !34, file: !1, line: 41, type: !6, isLocal: true, isDefinition: true)
!34 = distinct !DISubprogram(name: "apply_doppler_shift", linkageName: "_Z19apply_doppler_shift8s_cint32", scope: !1, file: !1, line: 39, type: !31, isLocal: false, isDefinition: true, scopeLine: 40, flags: DIFlagPrototyped, isOptimized: false, unit: !26, variables: !2)
!35 = !DIGlobalVariableExpression(var: !36, expr: !DIExpression())
!36 = distinct !DIGlobalVariable(name: "increment", linkageName: "_ZL9increment", scope: !26, file: !1, line: 30, type: !6, isLocal: true, isDefinition: true)
!37 = !DIGlobalVariableExpression(var: !38, expr: !DIExpression())
!38 = distinct !DIGlobalVariable(name: "time_delay_phase", linkageName: "_ZL16time_delay_phase", scope: !26, file: !1, line: 31, type: !6, isLocal: true, isDefinition: true)
!39 = !DIGlobalVariableExpression(var: !40, expr: !DIExpression())
!40 = distinct !DIGlobalVariable(name: "sincos_table", linkageName: "_ZL12sincos_table", scope: !26, file: !1, line: 1, type: !6, isLocal: true, isDefinition: true)
!41 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus, file: !1, producer: "clang version 7.0.0 ", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !2, globals: !42)
!42 = !{!43, !45}
!43 = !DIGlobalVariableExpression(var: !44, expr: !DIExpression())
!44 = distinct !DIGlobalVariable(name: "delay_line_history", linkageName: "_ZL18delay_line_history", scope: !41, file: !1, line: 6, type: !6, isLocal: true, isDefinition: true)
!45 = !DIGlobalVariableExpression(var: !46, expr: !DIExpression())
!46 = distinct !DIGlobalVariable(name: "delay_line_index", linkageName: "_ZL16delay_line_index", scope: !41, file: !1, line: 7, type: !6, isLocal: true, isDefinition: true)
!47 = !{i32 2, !"Dwarf Version", i32 4}
!48 = !{i32 2, !"Debug Info Version", i32 3}
!49 = distinct !DISubprogram(name: "top_target_generator", linkageName: "_Z20top_target_generatorht", scope: !1, file: !1, line: 60, type: !31, isLocal: false, isDefinition: true, scopeLine: 61, flags: DIFlagPrototyped, isOptimized: false, unit: !0, variables: !2)
!50 = !DILocalVariable(name: "in", arg: 1, scope: !34, file: !1, line: 39, type: !6)
!51 = !DILocation(line: 39, column: 35, scope: !34, inlinedAt: !52)
!52 = distinct !DILocation(line: 85, column: 23, scope: !49)
  )llvm";

  SMDiagnostic Err;
  LLVMContext Context;
  auto M = parseAssemblyString(Mod, Err, Context);
  assert(M && "Could not parse module?");
  assert(!verifyModule(*M) && "Module is not well formed!");

  auto NewM = llvm::CloneModule(M.get());
  EXPECT_FALSE(verifyModule(*NewM)); // failed here

  DebugInfoFinder Finder;
  Finder.processModule(*NewM);
  // expect failure here, but passes, so I think the Finder count the unit from
  //   '!llvm.dbg.cu = !{!2, !45, !10, !59, !33}'
  EXPECT_EQ(5U, Finder.compile_unit_count());
}


}
