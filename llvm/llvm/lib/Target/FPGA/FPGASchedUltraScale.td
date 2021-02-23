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
// This file defines the machine model for UltraScale to support instruction
// scheduling and other instruction cost heuristics.
//
//===----------------------------------------------------------------------===//

def UltraScaleModel : SchedMachineModel {
  let CompleteModel = 0;
}

let SchedModel = UltraScaleModel in {

// Ports 0, 1, handle all loads and stores.
//def USPort0 : ProcResource<1>;
//def USPort1 : ProcResource<1>;

//def USPort01  : ProcResGroup<[USPort0, USPort1]>;

//def : WriteRes<WriteStore, [USPort01]>;
} // SchedModel
