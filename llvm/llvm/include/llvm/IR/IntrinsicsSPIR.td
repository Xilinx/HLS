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
// This file defines all of the SPIR-specific intrinsics for use with SPIR.
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "spir" in {
//
// Async Copy and Prefetch Functions
//

//
// Atomic Functions
//

//
// Common Built-In Functions
//

//
// Event Built-In Functions
//

//
// Geometric Built-In Functions
//

//
// Image Read and Write Functions
//

//
// Integer Built-In Functions
//

//
// Math Built-In Functions
//

//
// Miscellaneous Vector Functions
//

//
// Relational Built-In Functions
//

//
// Synchronization and Memory Fence Functions
//

def int_spir_work_group_barrier : 
    Intrinsic<[], [llvm_i32_ty, llvm_i32_ty], [IntrConvergent]>,
    GCCBuiltin<"__spir_work_group_barrier">;

//
// Vector Data Load and Store Functions
//

//
// Work-Item Built-In Functions
//
let IntrProperties = [IntrNoMem] in {
def int_spir_get_work_dim : 
    Intrinsic<[llvm_i32_ty], []>,
    GCCBuiltin<"__spir_get_work_dim">;

def int_spir_get_global_size :
    Intrinsic<[llvm_anyint_ty], [llvm_i32_ty]>,
    GCCBuiltin<"__spir_get_global_size">;

def int_spir_get_global_id :
    Intrinsic<[llvm_anyint_ty], [llvm_i32_ty]>,
    GCCBuiltin<"__spir_get_global_id">;

def int_spir_get_local_size :
    Intrinsic<[llvm_anyint_ty], [llvm_i32_ty]>,
    GCCBuiltin<"__spir_get_local_size">;

def int_spir_get_enqueued_local_size :
    Intrinsic<[llvm_anyint_ty], [llvm_i32_ty]>,
    GCCBuiltin<"__spir_get_enqueued_local_size">;

def int_spir_get_local_id :
    Intrinsic<[llvm_anyint_ty], [llvm_i32_ty]>,
    GCCBuiltin<"__spir_get_local_id">;

def int_spir_get_num_groups :
    Intrinsic<[llvm_anyint_ty], [llvm_i32_ty]>,
    GCCBuiltin<"__spir_get_num_groups">;

def int_spir_get_group_id :
    Intrinsic<[llvm_anyint_ty], [llvm_i32_ty]>,
    GCCBuiltin<"__spir_get_group_id">;

// get_local_size(dim) * get_group_id(dim) + get_global_offset(dim)
def int_spir_get_global_id_base :
    Intrinsic<[llvm_anyint_ty], [llvm_i32_ty]>;

def int_spir_get_global_offset :
    Intrinsic<[llvm_anyint_ty], [llvm_i32_ty]>,
    GCCBuiltin<"__spir_get_global_offset">;

def int_spir_get_global_linear_id :
    Intrinsic<[llvm_anyint_ty], []>,
    GCCBuiltin<"__spir_get_global_linear_id">;

def int_spir_get_local_linear_id :
    Intrinsic<[llvm_anyint_ty], []>,
    GCCBuiltin<"__spir_get_local_linear_id">;
}

//
// Pipe Functions
//
def int_spir_read_pipe_2 :
    Intrinsic<[llvm_i32_ty],
	          [LLVMAnyPointerType<llvm_any_ty>, LLVMAnyPointerType<llvm_any_ty>],
	          [IntrArgMemOnly, NoCapture<0>, NoCapture<1>]>,
    GCCBuiltin<"__spir_read_pipe_2">;
def int_spir_write_pipe_2 :
    Intrinsic<[llvm_i32_ty],
	          [LLVMAnyPointerType<llvm_any_ty>, LLVMAnyPointerType<llvm_any_ty>],
	          [IntrArgMemOnly, NoCapture<0>, NoCapture<1>]>,
    GCCBuiltin<"__spir_write_pipe_2">;

def int_spir_read_pipe_block_2 :
    Intrinsic<[llvm_any_ty], [LLVMAnyPointerType<LLVMMatchType<0>>],
	          [IntrArgMemOnly, NoCapture<0>]>,
    GCCBuiltin<"__spir_read_pipe_block_2">;
def int_spir_write_pipe_block_2 :
    Intrinsic<[], [llvm_any_ty, LLVMAnyPointerType<LLVMMatchType<0>>],
	          [IntrArgMemOnly, NoCapture<1>]>,
    GCCBuiltin<"__spir_write_pipe_block_2">;
}
