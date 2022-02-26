// (c) Copyright 2016-2021 Xilinx, Inc.
//  All Rights Reserved.
// 
//  Licensed to the Apache Software Foundation (ASF) under one
//  or more contributor license agreements.  See the NOTICE file
//  distributed with this work for additional information
//  regarding copyright ownership.  The ASF licenses this file
//  to you under the Apache License, Version 2.0 (the
//  "License"); you may not use this file except in compliance
//  with the License.  You may obtain a copy of the License at
// 
//    http://www.apache.org/licenses/LICENSE-2.0
// 
//  Unless required by applicable law or agreed to in writing,
//  software distributed under the License is distributed on an
//  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied.  See the License for the
//  specific language governing permissions and limitations
//  under the License.

// ===----------------------------------------------------------------------===//
//===- MemoryPartition.cpp - Pass --------------------===//
//
// add memory partition intrinsic call driven by PIPELINE/PARALLEL instrinsic to
// achieve optimal performance
//
//===----------------------------------------------------------------------===//
#include "llvm/ADT/SmallSet.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/XILINXLoopInfoUtils.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/XILINXFPGAIntrinsicInst.h"
#include "llvm/Pass.h"
#include <iostream>
#include <set>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <vector>
#include <unordered_set>
using namespace llvm;
using std::map;
using std::set;
using std::string;
using std::vector;
#define CONST_ITER nullptr
#define DEBUG_TYPE "auto-memory-partition"
#define PRIOR "logic"
#define ENABLE_RESHAPE 0
#undef DEBUG
#define ENABLED_DEBUG 0
#if ENABLED_DEBUG
#define DEBUG(x)                                                               \
  do {                                                                         \
    x;                                                                         \
  } while (false)
#else
#define DEBUG(x)
#endif
//  Vivado_HLS partition factor threshold
#define XilinxThresh 1024
#define HeuristicThresh 64
static cl::opt<bool> ShouldGenerteReport("enable-auto-test", cl::Hidden,
                                         cl::init(false));
struct AutoMemoryPartition : public ModulePass {
  static char ID;
  AutoMemoryPartition() : ModulePass(ID) {}
  bool doInitialization(Module &M) override;
  bool doFinalization(Module &M) override;
  bool runOnModule(Module &M) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.setPreservesAll();
  }

private:
  enum unroll_option {
    DISABLE_UNROLL = 1,
    COMPLETE_UNROLL = 0,
  };
  enum partition_type {
    CYCLIC_PARTITION = 0,
    BLOCK_PARTITION,
    COMPLETE_PARTITION
  };
  enum reshape_type { CYCLIC_RESHAPE = 0, BLOCK_RESHAPE, COMPLETE_RESHAPE };
  struct partition_record {
    partition_type type;
    int dim;
    int factor;
    partition_record(partition_type t, int d, int f)
        : type(t), dim(d), factor(f) {}
    partition_record() {}
    partition_record(const partition_record &one)
        : type(one.type), dim(one.dim), factor(one.factor) {}
  };
  struct reshape_record {
    reshape_type type;
    int dim;
    int factor;
    reshape_record(reshape_type t, int d, int f) : type(t), dim(d), factor(f) {}
    reshape_record() {}
    reshape_record(const reshape_record &one)
        : type(one.type), dim(one.dim), factor(one.factor) {}
  };
  struct field_info {
    SmallVector<int, 4> index_val;
    Type *type;
    std::string name;
    Value *addr;
    field_info() {
      type = nullptr;
      addr = nullptr;
    }
  };

  bool IsRecursiveType(StructType *type) {
    auto visited = llvm::make_unique<std::unordered_set<StructType *>>();
    std::function<bool(StructType*)> IsR = [&](StructType *st) {
      if (visited->count(st) > 0) {
        return st == type;
      } else {
        visited->insert(st);
        for (auto et : st->elements()) {
          while (isa<PointerType>(et) || isa<ArrayType>(et)) {
            if (auto pt = dyn_cast<PointerType>(et))
              et = pt->getElementType();
            else if (auto at = dyn_cast<ArrayType>(et))
              et = at->getElementType();
          }
          if (auto nst = dyn_cast<StructType>(et)) {
            if (IsR(nst))
              return true;
          }
        }
        visited->erase(st);
        return false;
      }
    };
    return IsR(type);
  }

  bool is_candidate_array(Value *array) {
    if (auto AI = dyn_cast<AllocaInst>(array)) {
      auto ty = AI->getAllocatedType();
      if (isa<ArrayType>(ty) || isa<StructType>(ty)) {
        return true;
      }
    }
    if (auto arg = dyn_cast<Argument>(array)) {
      auto ty = arg->getType();
      if (isa<ArrayType>(ty) || isa<PointerType>(ty) || isa<StructType>(ty)) {
        return true;
      }
    }
    if (auto global = dyn_cast<GlobalVariable>(array)) {
      auto ty = global->getValueType();
      if (isa<ArrayType>(ty) || isa<StructType>(ty))
        return true;
    }
    return false;
  }
  void check_hls_partition(Module &M);
  void check_hls_reshape(Module &M);
  void check_hls_stream(Module &M);
  void check_hls_ipcore(Module &M);
  void check_hls_loop_opt(Module &M);
  void apply_auto_pipeline_for_fine_grained_loop(Module &M);
  void apply_complete_unroll_on_sub_loop(Loop *L);
  void get_sub_functions_recursive(Function *func);

  int check_loop_opt_intrinsic(const BasicBlock *L);
  void partition_analysis(Module &M);
  void partition_merge();
  void partition_transform();
  bool partition_analysis_node(
      Value *array, const BasicBlock *top_loop,
      const SmallVector<size_t, 4> &vec_dim_size,
      const DenseMap<Value *, SmallVector<CallInst *, 4>> &addr_with_call_path,
      int dim, int opt, int &factor, DenseMap<int, int> has_partition,
      SmallVector<int, 4> &indices, bool &dual_option, int &repeated);

  bool parse_full_accesses(
      DenseMap<Value *, vector<DenseMap<Value *, int64_t>>> &index_expr_full,
      const DenseMap<Value *, SmallVector<CallInst *, 4>> &addrs_with_call_path,
      int dim, SmallVector<int, 4> &indices);

  DenseMap<Value *, int64_t>
  parse_access_index(Value *index, const SmallVector<CallInst *, 4> &call_path);
  Value *get_actual_argument(Function *callee, int arg_no,
                             const SmallVector<CallInst *, 4> &call_path);
  bool parse_coeff_scev(const SCEV *se_expr, DenseMap<Value *, int64_t> &res,
                        ScalarEvolution &SE);
  void unroll_ref_generate_full(
      Value *array, const BasicBlock *top_loop,
      DenseMap<Value *, vector<DenseMap<Value *, int64_t>>> &index_expr_full,
      vector<DenseMap<Value *, int64_t>> &index_expr_copy, bool &heuristic_on,
      bool &dual_option, int64_t dim_size);

  void unroll_ref_generate(vector<DenseMap<Value *, int64_t>> &index_expr_orig,
                           vector<DenseMap<Value *, int64_t>> &index_expr_copy,
                           const BasicBlock *loop_header, bool &heuristic_on,
                           int64_t dim_size);

  bool get_loop_bound_and_step(const BasicBlock *loop_header,
                               int64_t &trip_count, int64_t &step,
                               int64_t &start);

  void
  remove_repeated_access(const vector<DenseMap<Value *, int64_t>> &index_expr,
                         DenseMap<int, int> &repeat_tag);
  int choose_bank(int &bank_num,
                  const vector<DenseMap<Value *, int64_t>> &index_expr,
                  const DenseMap<int, int> &repeat_tag, int dim_size);
  void bank_renew(int *bank_proc);
  int conflict_detect(int bank_tmp,
                      const vector<DenseMap<Value *, int64_t>> &index_expr,
                      const DenseMap<int, int> &repeat_tag);
  void dual_port_adjust();
  bool reorganize_factor();
  int choose_factor(Value *array, const vector<DenseMap<int, int>> &p_options,
                    DenseMap<int, int> &p_decision);

  void generate_new_partition_intrinsic(Value *val,
                                        partition_record new_partition,
                                        Instruction *insertPos);
  void generate_new_reshape_intrinsic(Value *val, reshape_record new_reshape,
                                      Instruction *insertPos);
  bool update_memory_partition_intrinsic(
      Value *array, const vector<partition_record> &vec_new_partition);
  bool remove_memory_partition_intrinsic(Value *array);
  Value *simplifyGEP(Value *addr);

  DenseMap<Value *, bool> trace_to_bus(Value *array);

  DenseMap<Value *, SmallVector<CallInst *, 4>>
  get_access_address_cross_function(Use *arg, CallInst *call);
  DenseMap<Value *, SmallVector<CallInst *, 4>>
  get_access_address_cross_function(Argument *array);
  void get_array_from_address(Value *addr, Value *array, Value *&top_array,
                              int &dim);
  void delete_later(Instruction *inst);
  void delete_floating_inst();
  void remove_intrinsic_inst();
  void copy_factors(const vector<DenseMap<int, int>> &original,
                    vector<DenseMap<int, int>> &target, int dim);

  bool isLoopInvariant(const Value *val, BasicBlock *loop_header);
  void print_array_location(Value *array);

  bool topological_sort(
      const SmallVector<Function *, 10> &vec_funcs,
      DenseMap<Function *, SmallSet<Function *, 4>> &func_to_sub_funcs,
      DenseMap<Function *, SmallSet<Function *, 4>> &func_to_callers,
      SmallVector<Function *, 10> &res);


  // struct data array member support
  SmallVector<field_info, 4> decompose(Type *ty, SmallVector<int, 4> indices,
                                       StringRef name);
  bool matched_indices(GetElementPtrInst *gep, SmallVector<int, 4> &indices);
  bool matched_indices(ConstantExpr *gep, SmallVector<int, 4> &indices);

  void setName(GetElementPtrInst *gep);

  Value *lookup(const SmallPtrSet<Value *, 4> &val_set, Value *key);
  bool isIdenticalAddr(Value *val, Value *key);
  bool filter(Value *var);
  ///////////////////////////////////////////////////
  // Parse pragma ssdm calls
  // To extract attribute "variable" etc
  int get_pragma_attribute_variable(CallInst *call, Value *val);
  // To extract attribute "factor" "II" "dim" etc
  int get_pragma_attribute_integer(CallInst *call, string attribute, int *val);
  // To extract attribute "complete" "cyclic/block" etc
  int get_pragma_attribute_string(CallInst *call, string attribute,
                                  string *val);

  // math
  int64_t gcd2(int64_t a, int64_t b);
  int64_t ngcd(vector<int64_t> a, int n);
  int intdiv(int64_t a, int64_t b);
  bool isPo2(int bank);

private:
  void print_index(const vector<DenseMap<Value *, int64_t>> &index_expr);
  void print_index(const vector<DenseMap<Value *, int64_t>> &index_expr,
                   const DenseMap<int, int> &repeat_tag);

private:
  DenseMap<const BasicBlock *, size_t> loop_trip_count;
  DenseMap<const BasicBlock *, int64_t> loop_unroll_info;
  DenseMap<const BasicBlock *, int> pipelined_loop;
  SmallPtrSet<const BasicBlock *, 4> coarse_grained_loop;
  SmallPtrSet<const BasicBlock *, 4> dataflowed_loop;
  DenseMap<Value *, SmallVector<partition_record, 4>> user_partition_info;
  DenseMap<Value *, SmallVector<reshape_record, 4>> user_reshape_info;
  DenseMap<Value *, SmallVector<CallInst *, 1>> hls_partition_table;
  DenseMap<Value *, SmallVector<CallInst *, 1>> hls_reshape_table;
  SetVector<Instruction *> to_remove_intrinsic;
  SmallPtrSet<Value *, 4> hls_stream_variables;
  SmallPtrSet<Value *, 4> hls_ipcore_variables;
  SmallSet<Function *, 4> flattened_funcs;
  DenseMap<const BasicBlock *, SmallVector<CallInst *, 1>>
      hls_loop_pipeline_table;
  DenseMap<const BasicBlock *, SmallVector<CallInst *, 1>>
      hls_loop_unroll_table;
  DenseMap<const BasicBlock *, SmallVector<CallInst *, 1>> hls_loop_opt_table;
  // Parition decisions before merge
  DenseMap<Value *, vector<DenseMap<int, int>>> analyzed_partitions;
  // Final decisions
  DenseMap<Value *, DenseMap<int, int>> m_partitions;
  DenseMap<Value *, set<const BasicBlock *>> addr_visited_loops;
  DenseMap<Value *, set<const BasicBlock *>> global_visited_loops;
  DenseMap<Value *, Value *> index_to_addr; // Map index to reference
  bool m_dual_option = true;

  Function *partition_intrisic;
  Function *reshape_intrisic;
  Function *sideeffect_intrinsic;
  Type *int1_ty;
  Type *int32_ty;
  Type *int64_ty;
  SetVector<Instruction *> floating_inst;
  static const StringRef pipeline_metadata_name;
  static const StringRef unroll_metadata_name;
  static const StringRef complete_unroll_metadata_name;
  static const StringRef disable_unroll_metadata_name;
  static const StringRef xilinx_array_partition_bundle_tag;
  static const StringRef xilinx_array_reshape_bundle_tag;
  static const int64_t unrollThreshold = 16;
};
const StringRef AutoMemoryPartition::pipeline_metadata_name =
    "llvm.loop.pipeline.enable";
const StringRef AutoMemoryPartition::unroll_metadata_name =
    "llvm.loop.unroll.count";
const StringRef AutoMemoryPartition::complete_unroll_metadata_name =
    "llvm.loop.unroll.full";
const StringRef AutoMemoryPartition::disable_unroll_metadata_name =
    "llvm.loop.unroll.disable";
const StringRef AutoMemoryPartition::xilinx_array_partition_bundle_tag =
    "xlx_array_partition";
const StringRef AutoMemoryPartition::xilinx_array_reshape_bundle_tag =
    "xlx_array_reshape";
char AutoMemoryPartition::ID = 0;
static RegisterPass<AutoMemoryPartition>
    X(DEBUG_TYPE,
      "Auto automatic memory partition optimal transformation",
      false /* Only looks at CFG */, true /* Transformation Pass */);

static Type *get_array_type(Value *array) {
  if (auto local_array = dyn_cast<AllocaInst>(array))
    return local_array->getAllocatedType();
  if (auto global_array = dyn_cast<GlobalVariable>(array))
    return global_array->getValueType();
  return array->getType();
}

static SmallVector<size_t, 4> getArrayDimensionSize(Type *array_ty) {
  SmallVector<size_t, 4> res;
  Type *curr_type = array_ty;
  while (isa<ArrayType>(curr_type) || isa<PointerType>(curr_type)) {
    auto curr_array_type = dyn_cast<ArrayType>(curr_type);
    auto curr_pointer_type = dyn_cast<PointerType>(curr_type);
    if (curr_array_type) {
      res.push_back(curr_array_type->getNumElements());
      curr_type = curr_array_type->getElementType();
    } else if (curr_pointer_type) {
      res.push_back(0);
      curr_type = curr_pointer_type->getElementType();
    }
  }
  return res;
}

static Type *getBaseType(Type *array_ty) {
  Type *curr_type = array_ty;
  while (isa<ArrayType>(curr_type) || isa<PointerType>(curr_type)) {
    auto curr_array_type = dyn_cast<ArrayType>(curr_type);
    auto curr_pointer_type = dyn_cast<PointerType>(curr_type);
    if (curr_array_type) {
      curr_type = curr_array_type->getElementType();
    } else if (curr_pointer_type) {
      curr_type = curr_pointer_type->getElementType();
    }
  }
  return curr_type;
}

/// FindAllocaDbgDeclare - Finds the llvm.dbg.declare intrinsic describing the
///// alloca 'V', if any.
static DbgDeclareInst *FindAllocaDbgDeclare(Value *V) {
  if (auto *L = LocalAsMetadata::getIfExists(V))
    if (auto *MDV = MetadataAsValue::getIfExists(V->getContext(), L))
      for (User *U : MDV->users())
        if (DbgDeclareInst *DDI = dyn_cast<DbgDeclareInst>(U))
          return DDI;

  return nullptr;
}

static SmallVector<Loop *, 4> getAllSubLoops(Loop *L) {
  SmallVector<Loop *, 4> res;
  for (auto subL : L->getSubLoops()) {
    res.push_back(subL);
    auto sub_res = getAllSubLoops(subL);
    res.insert(res.end(), sub_res.begin(), sub_res.end());
  }
  return res;
}

static void print_loop_location(Loop *loop) {
  auto loc = loop->getStartLoc();
  if (loc)
    loc.print(dbgs());
}

static void print_function_location(Function *func) {
  auto subprogram = func->getSubprogram();
  if (subprogram) {
    dbgs() << "(" << subprogram->getFilename() << ":" << subprogram->getLine()
           << ")";
  }
}

bool AutoMemoryPartition::doInitialization(Module &M) {
  partition_intrisic = nullptr;
  reshape_intrisic = nullptr;
  sideeffect_intrinsic = nullptr;
  int32_ty = Type::getInt32Ty(M.getContext());
  int64_ty = Type::getInt64Ty(M.getContext());
  int1_ty = Type::getInt1Ty(M.getContext());
  return false;
}

bool AutoMemoryPartition::runOnModule(Module &M) {
  // Step 1: get all the HLS pipeline/unroll/array_partition pragmas
  // parsing ssdm
  check_hls_partition(M);
  check_hls_reshape(M);
  check_hls_stream(M);
  check_hls_loop_opt(M);
  check_hls_ipcore(M);
  apply_auto_pipeline_for_fine_grained_loop(M);
  // Step 2: analyze the access pattern under each loop scope
  partition_analysis(M);
  // Step 3: merge the array partition pragmas from each loop scope
  partition_merge();
  // Step 4: generate new pragmas, replace/remove the old pragmas
  // using ssdm
  partition_transform();
  return true;
}

// output: SmallSet<void*> hls_ip_core_variables;
void AutoMemoryPartition::check_hls_ipcore(Module &M) {
  for (auto &F : M) {
    if (F.hasFnAttribute("fpga.blackbox")) {
      for (auto &Arg: F.args()) { 
        if (!is_candidate_array(&Arg)) continue;
        DEBUG(dbgs() << "found ip core variable: " << Arg.getName() << '\n');
        hls_ipcore_variables.insert(&Arg);
        DenseMap<Value *, bool> local_res =
          trace_to_bus(&Arg);
        for (auto &one_res : local_res) {
          auto address = one_res.first;
          Value *top_array = nullptr;
          int dim_offset = -1;
          get_array_from_address(address, &Arg, top_array, dim_offset);
          if(top_array == nullptr) continue;
          hls_ipcore_variables.insert(top_array);
        }
      }
    }
  }
}


// output: DenseMap<void*, vector<void*> > hls_partition_table
// for each variable, collect all its related partition intrinsic function call
void AutoMemoryPartition::check_hls_partition(Module &M) {
  sideeffect_intrinsic = Intrinsic::getDeclaration(&M, Intrinsic::sideeffect);
  if (sideeffect_intrinsic != nullptr) {
    for (auto &one_spec : sideeffect_intrinsic->uses()) {
      CallInst *call = dyn_cast<CallInst>(one_spec.getUser());
      if (!call)
        continue;
      unsigned index = 0;
      while (index < call->getNumOperandBundles()) {
        const auto &bundle_info = call->getOperandBundleAt(index++);
        if (!bundle_info.getTagName().equals(xilinx_array_partition_bundle_tag))
          continue;
        // call void @llvm.sideeffect() [ "xlx_array_partition"(%variable, i32
        // type, i32 factor, i32 dim) ]
        // * type: 0 for cyclic, 1 for block, 2 for complete
        Value *array = simplifyGEP(bundle_info.Inputs[0]);
        auto vec_dims = getArrayDimensionSize(array->getType());
        partition_type par_type = BLOCK_PARTITION;
        if (auto cs_type = dyn_cast<ConstantInt>(bundle_info.Inputs[1])) {
          auto type_int = cs_type->getZExtValue();
          if (0 == type_int)
            par_type = CYCLIC_PARTITION;
          else if (2 == type_int)
            par_type = COMPLETE_PARTITION;
        }
        int factor = 0;
        if (auto cs_factor = dyn_cast<ConstantInt>(bundle_info.Inputs[2])) {
          factor = cs_factor->getZExtValue();
        }
        int dim = 0;
        if (auto cs_dim = dyn_cast<ConstantInt>(bundle_info.Inputs[3])) {
          dim = cs_dim->getZExtValue();
        }
        DEBUG(dbgs() << "found partition for variable: " << array->getName();
            dbgs() << "\ntype = " << par_type << " dim = " << dim
            << " factor = " << factor << '\n');
        DenseMap<Value *, bool> local_res = trace_to_bus(array);
        for (auto &one_res : local_res) {
          auto address = one_res.first;
          Value *top_array = nullptr;
          int dim_offset = -1;
          get_array_from_address(address, array, top_array, dim_offset);
          if (top_array == nullptr) continue;
          if (dim != 0) {
            partition_record par(par_type, dim + dim_offset, factor);
            user_partition_info[top_array].push_back(par);
          } else {
            for (size_t one_dim = 1; one_dim <= vec_dims.size(); ++one_dim) {
              partition_record par(par_type, one_dim + dim_offset, factor);
              user_partition_info[top_array].push_back(par);
            }
          }
          hls_partition_table[top_array].push_back(call);
        }
      }
    }
  }
}

// output: DenseMap<void*, vector<void*> > hls_reshape_table
// for each variable, collect all its related reshape intrinsic function call
void AutoMemoryPartition::check_hls_reshape(Module &M) {
  sideeffect_intrinsic = Intrinsic::getDeclaration(&M, Intrinsic::sideeffect);
  if (sideeffect_intrinsic != nullptr) {
    for (auto &one_spec : sideeffect_intrinsic->uses()) {
      CallInst *call = dyn_cast<CallInst>(one_spec.getUser());
      if (!call)
        continue;
      unsigned index = 0;
      while (index < call->getNumOperandBundles()) {
        const auto &bundle_info = call->getOperandBundleAt(index++);
        if (!bundle_info.getTagName().equals(xilinx_array_reshape_bundle_tag))
          continue;
        // call void @llvm.sideeffect() [ "xlx_array_reshape"(%variable, i32
        // type, i32 factor, i32 dim) ]
        // * type: 0 for cyclic, 1 for block, 2 for complete
        Value *array = simplifyGEP(bundle_info.Inputs[0]);
        auto vec_dims = getArrayDimensionSize(array->getType());
        reshape_type par_type = BLOCK_RESHAPE;
        if (auto cs_type = dyn_cast<ConstantInt>(bundle_info.Inputs[1])) {
          auto type_int = cs_type->getZExtValue();
          if (0 == type_int)
            par_type = CYCLIC_RESHAPE;
          else if (2 == type_int)
            par_type = COMPLETE_RESHAPE;
        }
        int factor = 0;
        if (auto cs_factor = dyn_cast<ConstantInt>(bundle_info.Inputs[2])) {
          factor = cs_factor->getZExtValue();
        }
        int dim = 0;
        if (auto cs_dim = dyn_cast<ConstantInt>(bundle_info.Inputs[3])) {
          dim = cs_dim->getZExtValue();
        }
        DEBUG(dbgs() << "found reshape for variable: " << array->getName();
            dbgs() << "\ntype = " << par_type << " dim = " << dim
            << " factor = " << factor << '\n');
        DenseMap<Value *, bool> local_res = trace_to_bus(array);
        for (auto &one_res : local_res) {
          auto address = one_res.first;
          Value *top_array = nullptr;
          int dim_offset = -1;
          get_array_from_address(address, array, top_array, dim_offset);
          if(top_array == nullptr) continue;
          if (dim != 0) {
            reshape_record par(par_type, dim + dim_offset, factor);
            user_reshape_info[top_array].push_back(par);
          } else {
            for (size_t one_dim = 1; one_dim <= vec_dims.size(); ++one_dim) {
              reshape_record par(par_type, one_dim + dim_offset, factor);
              user_reshape_info[top_array].push_back(par);
            }
          }
          hls_reshape_table[top_array].push_back(call);
        }
      }
    }
  }
}

// output: SmallSet<void*> hls_stream_variables;
void AutoMemoryPartition::check_hls_stream(Module &M) {
  sideeffect_intrinsic = Intrinsic::getDeclaration(&M, Intrinsic::sideeffect);
  if (sideeffect_intrinsic != nullptr) {
    for (auto &one_spec : sideeffect_intrinsic->uses()) {
      CallInst *call = dyn_cast<CallInst>(one_spec.getUser());
      if (!call || !isa<StreamPragmaInst>(call))
        continue;
      auto stream_pragma = cast<StreamPragmaInst>(call);
      Value *array = simplifyGEP(stream_pragma->getVariable());
      DEBUG(dbgs() << "found stream variable: " << array->getName() << '\n');
      hls_stream_variables.insert(array);
      DenseMap<Value *, bool> local_res = trace_to_bus(array);
      for (auto &one_res : local_res) {
        auto address = one_res.first;
        Value *top_array = nullptr;
        int dim_offset = -1;
        get_array_from_address(address, array, top_array, dim_offset);
        if(top_array == nullptr) continue;
        hls_stream_variables.insert(top_array);
      }
    }
  }
}

// output: hls_loop_pipeline_table, hls_loop_unroll_table
// for each loop, collect all its related pipeline/unroll intrinsic function
// call
void AutoMemoryPartition::check_hls_loop_opt(Module &M) {
  for (auto &F : M) {
    if (F.empty())
      continue;
    if (F.hasFnAttribute("fpga.blackbox"))
      continue;
    LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();
    ScalarEvolution &SE = getAnalysis<ScalarEvolutionWrapperPass>(F).getSE();
    for (auto loop : LI.getLoopsInPreorder()) {
      auto trip_count = SE.getSmallConstantTripCount(loop);
      if (trip_count > 0 && loop->getLoopLatch() != loop->getExitingBlock()) {
        // minus 1 for non do-while loop
        trip_count--;
      }
      DEBUG(dbgs() << "===> parsing loop: " << loop->getName() << " ";
            print_loop_location(loop);
            dbgs() << " trip count = " << trip_count << '\n';
            dbgs() << "SE trip count = ";
            SE.getBackedgeTakenCount(loop)->dump(); dbgs() << '\n');
      auto header = loop->getHeader();
      loop_trip_count[header] = trip_count;
      if (isDataFlow(loop)) {
        dataflowed_loop.insert(header);
        continue;
      }
      auto loop_id = loop->getLoopID();
      if (loop_id != nullptr) {
        // get loop pipeline/unroll info
        // First operand should refer to the loop id itself.
        assert(loop_id->getNumOperands() > 0 &&
               "requires at least one operand");
        assert(loop_id->getOperand(0) == loop_id && "invalid loop id");

        for (unsigned i = 1, e = loop_id->getNumOperands(); i < e; ++i) {
          MDNode *MD = dyn_cast<MDNode>(loop_id->getOperand(i));
          if (!MD)
            continue;

          MDString *S = dyn_cast<MDString>(MD->getOperand(0));
          if (!S)
            continue;
          DEBUG(dbgs() << S->getString() << '\n');
          if (pipeline_metadata_name.equals(S->getString())) {
            auto II = dyn_cast<ConstantAsMetadata>(MD->getOperand(1));
            auto II_int = II ? dyn_cast<ConstantInt>(II->getValue()) : nullptr;
            pipelined_loop[header] = 1;
            if (II_int)
              pipelined_loop[header] = II_int->getSExtValue();
            apply_complete_unroll_on_sub_loop(loop);
          }
          if (unroll_metadata_name.equals(S->getString())) {
            auto factor = dyn_cast<ConstantAsMetadata>(MD->getOperand(1));
            auto factor_int =
                factor ? dyn_cast<ConstantInt>(factor->getValue()) : nullptr;
            if (factor_int) {
              loop_unroll_info[header] = factor_int->getZExtValue();
            }
          }
          if (complete_unroll_metadata_name.equals(S->getString())) {
            loop_unroll_info[header] = COMPLETE_UNROLL;
          }
          if (disable_unroll_metadata_name.equals(S->getString())) {
            loop_unroll_info[header] = DISABLE_UNROLL;
          }
        }
      }
    }
  }
  /*
  Function *pipeline_intrisic = M.getFunction(pipeline_intrisic_name);
  if (pipeline_intrisic != nullptr) {
    for (auto &one_spec : pipeline_intrisic->uses()) {
      CallInst *call = dyn_cast<CallInst>(one_spec.getUser());
      if (!call)
        continue;
      LoopInfo &LI =
          getAnalysis<LoopInfoWrapperPass>(*call->getFunction()).getLoopInfo();
      auto loop = LI.getLoopFor(call->getParent());
      if (loop) {
        hls_loop_opt_table[loop->getHeader()].push_back(call);
        hls_loop_pipeline_table[loop->getHeader()].push_back(call);
        CallSite cs(call);
        if (cs.arg_size() > 1) {
          if (auto cs_ii = dyn_cast<ConstantInt>(cs.getArgument(1)))
            pipelined_loop[loop->getHeader()->getHeader()] =
  cs_ii->getZExtValue();
        }
        apply_complete_unroll_on_sub_loop(loop);
      }
    }
  }
  Function *unroll_intrisic = M.getFunction(unroll_intrisic_name);
  if (unroll_intrisic != nullptr) {
    for (auto &one_spec : unroll_intrisic->uses()) {
      CallInst *call = dyn_cast<CallInst>(one_spec.getUser());
      if (!call)
        continue;
      LoopInfo &LI =
          getAnalysis<LoopInfoWrapperPass>(*call->getFunction()).getLoopInfo();
      // ouput type of LI.getLoopFor(call->getParent()) is Loop*
      auto loop = LI.getLoopFor(call->getParent());
      if (loop) {
        hls_loop_opt_table[loop->getHeader()].push_back(call);
        hls_loop_unroll_table[loop->getHeader()].push_back(call);
        CallSite cs(call);
        if (cs.arg_size() > 2) {
          if (auto cs_factor = dyn_cast<ConstantInt>(cs.getArgument(2)))
            loop_unroll_info[loop->getHeader()] = cs_factor->getSExtValue();
        }
      }
    }
  }
  */
  for (auto func : flattened_funcs) {
    if (func->empty())
      continue;
    if (func->hasFnAttribute("fpga.blackbox"))
      continue;
    DEBUG(dbgs() << "flatten function '" << func->getName() << "' ";
          print_function_location(func); dbgs() << '\n');
    LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>(*func).getLoopInfo();
    for (auto loop : LI.getLoopsInPreorder()) {
      loop_unroll_info[loop->getHeader()] = COMPLETE_UNROLL;
    }
  }
  // TODO check unroll region spec
}

bool AutoMemoryPartition::filter(Value *var) {
  if (lookup(hls_stream_variables, var) != nullptr)
    return true;
  if (lookup(hls_ipcore_variables, var) != nullptr)
    return true;
  return false;
}


void AutoMemoryPartition::partition_analysis(Module &M) {
  DEBUG(dbgs() << "\n\n ###### Partition analysis...\n");
  //  for (each function scope) {
  //    get_local_arrays();
  ////      local array 'a_buf',  factor=4
  ////        sub_f(a_buf);
  ////      local array 'b_buf', sub_f(b_buf); factor=8
  ////        sub_f(c);  'c' is external memory
  ////        void suf_f(int *a) {
  ////  #pragma HLS ARRAY_PARTITITON variable=a
  ////        }
  ////  need INFO: array index (loop scope), load/store info
  //    get_use_locations();
  //    group_the_references_within_loop_scope();
  //    for (each local arrays) {
  //      for (each dimension) {
  //        for (each loop scope) {
  //          bool ret = partition_analysis_loop();
  //        }
  //      }
  //    }
  //  }

  for (auto &F : M) { // Function
    if (F.empty())
      continue;
    if (F.hasFnAttribute("fpga.blackbox")) 
      continue;
    DEBUG(dbgs() << F.getName() << '\n');
    SmallVector<Value *, 10> candidate_arrays;
    for (auto it = M.global_begin(); it != M.global_end(); ++it) {
      if (filter(&*it)) continue;
        continue;
      candidate_arrays.push_back(&*it);
    }
    DenseMap<Value *, DenseMap<const BasicBlock *, SmallVector<Use *, 10>>>
        array_to_loop_uses;
    LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();
    for (auto &B : F) {   // BasicBlock
      for (auto &I : B) { // Instruction
        auto AI = dyn_cast<AllocaInst>(&I);
        if (AI == nullptr)
          continue;
        if (!is_candidate_array(AI))
          continue;
        if (filter(AI))
          continue;
        candidate_arrays.push_back(AI);
      }
    }
    if (!F.isVarArg()) {
      for (auto &arg : F.args()) {
        if (filter(&arg))
          continue;
        if (is_candidate_array(&arg)) {
          candidate_arrays.push_back(&arg);
        }
      }
    }
    for (auto local_array : candidate_arrays) {
      for (auto &one_use : local_array->uses()) {
        auto one_user = one_use.getUser();
        auto gep =
            dyn_cast<GetElementPtrInst>(one_user); // generate array address
        auto ce = dyn_cast<ConstantExpr>(one_user);
        if ((ce != nullptr && ce->getOpcode() == Instruction::GetElementPtr) ||
            gep != nullptr) {
          Value *addr = ce;
          if (gep != nullptr) {
            if (gep->getParent() == nullptr || gep->getFunction() != &F)
              continue;
            addr = gep;
          }
          for (auto &one_gep_use : addr->uses()) {
            if (auto *inst = dyn_cast<Instruction>(
                    one_gep_use.getUser())) { // read or write or call
              // TODO handle cross function references (partial access)
              if (inst->getFunction() != &F)
                continue;
              auto *curr_loop = LI.getLoopFor(inst->getParent());
              while (curr_loop) {
                array_to_loop_uses[local_array][curr_loop->getHeader()]
                    .push_back(&one_gep_use);
                curr_loop = curr_loop->getParentLoop();
              }
            }
          }
        } else if (auto *inst = dyn_cast<Instruction>(one_user)) {
          if (inst->getFunction() != &F)
            continue;
          // TODO handle cross function references (full access)
          auto *curr_loop = LI.getLoopFor(inst->getParent());
          while (curr_loop) {
            array_to_loop_uses[local_array][curr_loop->getHeader()].push_back(
                &one_use);
            curr_loop = curr_loop->getParentLoop();
          }
        }
      }
    }
    SmallVector<const BasicBlock *, 4> vec_loops;
    for (auto loop : LI.getLoopsInPreorder()) {
      vec_loops.push_back(loop->getHeader());
    }
    // generate address for each array fields
    DenseMap<Value *, SmallVector<field_info, 4>> decomposed_res;
    for (auto array : candidate_arrays) {
      SmallVector<int, 4> indices;
      auto ty = array->getType();
      if (auto alloca = dyn_cast<AllocaInst>(array)) {
        indices.push_back(-1);
        ty = alloca->getAllocatedType();
      } else if (auto global = dyn_cast<GlobalVariable>(array)) {
        indices.push_back(-1);
        ty = global->getValueType();
      }
      decomposed_res[array] = decompose(ty, indices, array->getName());
      field_info fi;
      fi.type = ty;
      fi.index_val = indices;
      fi.name = array->getName();
      fi.addr = array;
      decomposed_res[array].push_back(fi);
    }
    DEBUG(dbgs() << "\n\nStart partition factor analysis ...\n");
    for (auto array : candidate_arrays) {
      if (array_to_loop_uses.count(array) <= 0)
        continue;
      DEBUG(dbgs() << "analyze variable " << array->getName() << '\n');
      DenseMap<Value *, vector<DenseMap<int, int>>> local_factor;
      Instruction *insertPos = nullptr;
      if (auto alloca = dyn_cast<AllocaInst>(array)) {
        insertPos = alloca->getParent()->getTerminator();
      } else if (auto arg = dyn_cast<Argument>(array)) {
        insertPos = arg->getParent()->getEntryBlock().getTerminator();
      } else if (auto global = dyn_cast<GlobalVariable>(array)) {
        insertPos = F.getEntryBlock().getTerminator();
      }
      DenseMap<int, int> has_partition;
      for (auto loop : vec_loops) {
        if (array_to_loop_uses[array].count(loop) <= 0)
          continue;
        // Step 1:  check loop optimization
        // #pragma HLS pipeline
        // #pragma HLS unroll factor=x
        // #pragma HLS unroll
        int opt = check_loop_opt_intrinsic(loop);
        if (opt == 1)
          continue;
        DEBUG(dbgs() << "loop opt: " << opt << '\n');
        if (global_visited_loops[array].count(loop) > 0 && opt == 0) {
          DEBUG(dbgs() << "SKIP variable " << array->getName() << " in loop "
                       << loop->getName() << '\n');
          continue;
        }
        DenseMap<Value *, SmallVector<CallInst *, 4>> addrs_with_call_path;
        for (auto use : array_to_loop_uses[array][loop]) {
          auto user = use->getUser();
          if (isa<BitCastInst>(user) && user->hasOneUse()) {
            user = user->user_back();
          }
          auto read = dyn_cast<LoadInst>(user);
          auto write = dyn_cast<StoreInst>(user);
          auto call = dyn_cast<CallInst>(user);
          Value *addr = nullptr;
          if (read != nullptr) {
            addr = read->getPointerOperand();
            addrs_with_call_path[addr] = SmallVector<CallInst *, 4>{};
          } else if (write != nullptr) {
            addr = write->getPointerOperand();
            addrs_with_call_path[addr] = SmallVector<CallInst *, 4>{};
          } else if (call != nullptr) {
            auto cross_addrs_with_call_path =
                get_access_address_cross_function(use, call);
            addrs_with_call_path.insert(cross_addrs_with_call_path.begin(),
                                        cross_addrs_with_call_path.end());
          } else {
            DEBUG(dbgs() << "found unsupported accesss : ";
                  Value *user = use->getUser(); user->print(dbgs());
                  if (auto inst = dyn_cast<Instruction>(user)) {
                    DebugLoc loc = inst->getDebugLoc();
                    if (loc) {
                      dbgs() << " ";
                      loc.print(dbgs());
                      dbgs() << "\n";
                    }
                  });
          }
        }
        for (auto &one_info : decomposed_res[array]) {
          auto array_ty = one_info.type;
          auto indices = one_info.index_val;
          auto name = one_info.name;
          auto addr = one_info.addr;
          auto vec_dims = getArrayDimensionSize(array_ty);
          DenseMap<int, int> local_factor_loop;
          bool local_dual_option = m_dual_option;
          for (int dim = vec_dims.size() - 1; dim >= 0; dim--) {
            int factor = 1;
            int repeated = 0;
            int ret = partition_analysis_node(
                array, loop, vec_dims, addrs_with_call_path, dim, opt, factor,
                has_partition, indices, local_dual_option, repeated);
            if (ret) {
              if (factor == 0) {
                if (vec_dims[dim] <= XilinxThresh) {
                  has_partition[dim] = factor;
                  local_factor_loop[dim] = factor;
                  local_dual_option = false;
                }
              } else if (factor > 1) {
                if (factor <= XilinxThresh) {
                  has_partition[dim] = factor;
                  local_factor_loop[dim] = factor;
                  local_dual_option = false;
                }
              }  
              if (factor == 0) {
                DEBUG(dbgs() << "         ==> dimension " << dim + 1;
                    dbgs() << " ==> complete partition \n");
              } else if (factor > 1) {
                DEBUG(dbgs() << "         ==> dimension " << dim + 1;
                    dbgs() << "==> partition factor: " << factor << '\n');
              } else {
                DEBUG(dbgs() << "         ==> dimension " << dim + 1;
                    dbgs()
                    << "==> factor partition factor: " << factor << '\n');
              }
            } else {
              // succeed = false;
              DEBUG(dbgs()
                  << "[warning] No conflict-free partitioning solution\n");
              local_dual_option = false;
            }
            if (opt == -1 && has_partition.count(dim) > 0 && (repeated == 0)) {
              DEBUG(dbgs()
                  << "Hooray! Indices are different!! \n");
              break;
            }
          }
          if (!local_factor_loop.empty()) {
            if (addr == nullptr) {
              SmallVector<Value *, 4> gep_indices;
              for (auto index : indices) {
                if (index == -1) {
                  gep_indices.push_back(ConstantInt::get(int64_ty, 0));
                } else {
                  gep_indices.push_back(ConstantInt::get(int32_ty, index));
                }
              }
              // get the address of the first element
              gep_indices.push_back(ConstantInt::get(int64_ty, 0));
              auto struct_field = GetElementPtrInst::CreateInBounds(
                  array, gep_indices, name, insertPos);
              delete_later(struct_field);
              addr = struct_field;
              one_info.addr = struct_field;
            }
            local_factor[addr].push_back(local_factor_loop);
          }
        }
      }
      for (auto one_factor_res : local_factor) {
        auto one_local_factor = one_factor_res.second;
        auto addr = one_factor_res.first;
        analyzed_partitions[addr] = one_local_factor;
      }
    }
  }
}

// Analyze pipeline/unroll ssdm calls
// input: DenseMap<void *, SmallVector<void *, 1>> hls_loop_opt;
// return value: '1' - no optimization; '-1'- pipeline;
//               '0'- complete unroll; 'x'- x>1, unroll factor
int AutoMemoryPartition::check_loop_opt_intrinsic(const BasicBlock *loop) {
  int opt = 1;
  if (coarse_grained_loop.count(loop) > 0) {
    DEBUG(dbgs() << "\n\n disable memory partition for coarse grained loop\n");
    return opt;
  }
  if (dataflowed_loop.count(loop) > 0) {
    DEBUG(dbgs() << "\n\n disable memory partition for dataflow loop\n");
    return opt;
  }
  auto header = loop;
  if (pipelined_loop.count(header) > 0 && 
      (loop_unroll_info.count(header) > 0 && loop_unroll_info[header] == COMPLETE_UNROLL)) {
    DEBUG(dbgs() << "\n\n multiple opt directives are confusing\n");
    return opt;
  }

  if (pipelined_loop.count(header) > 0) {
    auto II = pipelined_loop[header];
    if (II == 1 || II == -1) {
      return -1;
    } else if (II == 0) {
      DEBUG(dbgs() << "disable pipeline\n");
      return 1;
    } else {
      DEBUG(dbgs() << "cannot support II greater than 1\n");
      return -(II);
    }
  }

  if (loop_unroll_info.count(header) > 0 && loop_unroll_info[header] != DISABLE_UNROLL) {
    auto factor = loop_unroll_info[header];
    if (factor == COMPLETE_UNROLL) {
      return 0;
    }
    DEBUG(dbgs() << "Get unroll factor " << factor << "\n");
    return factor;
  }
  return opt;
}

// requirement: get type of variables, get array dimension info
void AutoMemoryPartition::partition_merge() {
  DEBUG(dbgs() << "\n\n ######Partition merge...\n");

  //  1. Reorganize the partitioning factors
  int ret = static_cast<int>(reorganize_factor());
  if (ret == 0) {
    return;
  }

  //  2. Merge the factors
  for (auto info : analyzed_partitions) {
    auto array = info.first;
    auto p_options = info.second;
    DEBUG(dbgs() << "==> Array merge: " << array->getName() << '\n');

    if (p_options.size() == 1) {
      m_partitions[array] = p_options[0];
      continue;
    }

    DenseMap<int, int> p_decision;
    choose_factor(array, p_options, p_decision);
    m_partitions[array] = p_decision;
  }

  // Apply dual port partition for the right most dimension by default
  if (m_dual_option) {
    dual_port_adjust();
  }
}

static bool simple_compare(const std::pair<int, Value *> &one,
                           const std::pair<int, Value *> &other) {
  return std::abs(one.first) < std::abs(other.first);
}

void AutoMemoryPartition::partition_transform() {
  DEBUG(dbgs() << "\n\n ######Partition transform...\n");
  string partition_list_file = "__merlin_partition_list";
  FILE *fp = nullptr;
  if (ShouldGenerteReport)
    fp = fopen(partition_list_file.c_str(), "w");
  vector<std::pair<int, Value *>> output_sorting;
  for (auto partition : m_partitions) {
    auto array = partition.first;
    unsigned int line_number;
    if (auto inst = dyn_cast<Instruction>(array)) {
      if (auto dbg = FindAllocaDbgDeclare(array)) {
        inst = dbg;
      }
      DebugLoc loc = inst->getDebugLoc();
      if (loc) {
        line_number = loc->getLine();
      } else {
        line_number = 0;
      }
      output_sorting.emplace_back(line_number, array);
    }
  }
  std::sort(output_sorting.begin(), output_sorting.end(), simple_compare);
  int partition_cnt = 0;
#if ENABLE_RESHAPE
  int reshape_cnt = 0;
#endif
  for (auto pair : output_sorting) {
    auto line_number = pair.first;
    auto array = pair.second;
    auto strategy = m_partitions[array];
    auto local_array = array;
    auto addr = dyn_cast<GetElementPtrInst>(array);
    if (addr)
      local_array = addr->getPointerOperand();
    if (!isa<AllocaInst>(local_array) && !isa<GlobalVariable>(local_array))
      continue;
    BasicBlock *bb = nullptr;
    if (auto alloca = dyn_cast<AllocaInst>(local_array)) {
      bb = alloca->getParent();
    } else {
      for (auto user : local_array->users()) {
        if (auto inst = dyn_cast<Instruction>(user)) {
          if (inst->getParent()) {
            bb = &inst->getFunction()->getEntryBlock();
            break;
          }
        }
      }
    }
    if (!bb)
      continue;
    auto last_inst = bb->getTerminator();
    if (addr) {
      if (addr->getParent() == nullptr) {
        addr->insertBefore(last_inst);
      } else {
        bb = addr->getParent();
        last_inst = bb->getTerminator();
      }
    }
    remove_memory_partition_intrinsic(array);
    for (auto st : strategy) {
      int dim = st.first + 1;
      int factor = st.second;
      if (factor == 1)
        continue;
      std::stringstream ss;
      std::string varname = array->getName();
      if (factor == 0) {
        partition_record new_pragma(COMPLETE_PARTITION, dim, factor);
        generate_new_partition_intrinsic(array, new_pragma, last_inst);
        partition_cnt++;
        ss << "line " << line_number << ": partitioning array '";
      } else {
#if ENABLE_RESHAPE
        reshape_record new_pragma(CYCLIC_RESHAPE, dim, factor);
        generate_new_reshape_intrinsic(array, new_pragma, last_inst);
        reshape_cnt++;
        ss << "line " << line_number << ": reshaping array '";
#else
        partition_record new_pragma(CYCLIC_PARTITION, dim, factor);
        generate_new_partition_intrinsic(array, new_pragma, last_inst);
        partition_cnt++;
        ss << "line " << line_number << ": partitioning array '";
#endif
      }
      ss << varname << "\'  on dimension " << dim;
      if (factor != 0) {
        ss << " with factor " << factor << '\n';
      } else {
        ss << " completely\n";
      }
      string add_string = ss.str();
      if (fp)
        fprintf(fp, "%s", add_string.c_str());

      if (ShouldGenerteReport) {
        if (factor != 0) {
#if ENABLE_RESHAPE
          dbgs() << "INFO: [AUTO 102] Reshaping array '";
#else
          dbgs() << "INFO: [AUTO 101] Partitioning array '";
#endif
          dbgs() << array->getName() << "\' ";
          print_array_location(local_array);
          dbgs() << " on dimension " << dim;
          dbgs() << " with factor " << factor << '\n';
        } else {
          dbgs() << "INFO: [AUTO 101] Partitioning array '";
          dbgs() << array->getName() << "\' ";
          print_array_location(local_array);
          dbgs() << " on dimension " << dim;
          dbgs() << " completely\n";
        };
      }
    }
  }
  dbgs() << "INFO: [AUTO 101] Generating " << partition_cnt
         << " array partition pragma(s) automatically\n";
#if ENABLE_RESHAPE
  dbgs() << "INFO: [AUTO 102] Generating " << reshape_cnt
         << " array reshape pragma(s) automatically\n";
#endif
  delete_floating_inst();
  remove_intrinsic_inst();
  if (fp)
    fclose(fp);
}

bool AutoMemoryPartition::reorganize_factor() {
  SmallPtrSet<Value *, 4> manual_partition_array;
  //  1. Add the exsiting pragma factors
  //  User insert array_partition pragma on a sub function argument
  //  Need a cross function analysis
  SmallPtrSet<Value*, 4> analyzed_arrays;
  for (auto one_array_info : analyzed_partitions) {
    analyzed_arrays.insert(one_array_info.first);
  }
  for (auto it : user_partition_info) {
    auto array = it.first;
    // User input array_partition is highest priority
    if (lookup(analyzed_arrays, array) != nullptr) {
      DEBUG(dbgs() << "Drop automatic partition for array " << array->getName()
                   << " because of user partition\n");
      analyzed_partitions.erase(array);
    }
    for (auto record : it.second) {
      DEBUG(dbgs() << "Get partition table for array " << array->getName()
          << '\n');
      DenseMap<int, int> hls_factors;
      hls_factors[record.dim - 1] = record.factor;
      DEBUG(dbgs() << "[INFO] dim-" << record.dim << ", factor-" << record.factor
          << '\n');
      analyzed_partitions[array].push_back(hls_factors);
    }
    manual_partition_array.insert(array);

#if 0
    // Currently we dont suppor block partitioning
    if (record.type == BLOCK_PARTITION) {
      if (analyzed_partitions.count(array) > 0) {
        analyzed_partitions.erase(array);
        manual_partition_arrray.insert(array);
      }
    } else {
      if (analyzed_partitions.count(array) > 0) {
        analyzed_partitions[array].push_back(hls_factors);
      } else {
        vector<DenseMap<int, int>> new_vec;
        new_vec.push_back(hls_factors);
        analyzed_partitions[array] = new_vec;
      }
    }
#endif
  }
  for (auto it : user_reshape_info) {
    auto array = it.first;
    DEBUG(for (auto record : it.second) {
          dbgs() << "Get reshape table for array " << array->getName() << '\n';
          dbgs() << "[INFO] dim-" << record.dim << ", factor-" << record.factor
                 << '\n';});
    // User input array_reshape is highest priority
    if (lookup(analyzed_arrays, array) != nullptr) {
      DEBUG(dbgs() << "Drop automatic partition for array " << array->getName()
                   << " because of user reshape\n");
      analyzed_partitions.erase(array);
    }
    manual_partition_array.insert(array);
  }
  // cross function
  // No need to cross function from down to up now
  // 3. Pass the vectors between arguments
  DenseMap<Value *, DenseMap<Value *, bool>> res;
  for (auto &info : analyzed_partitions) {
    auto array = info.first;
    DEBUG(dbgs() << "TRACE: " << array->getName() << '\n');

    DenseMap<Value *, bool> local_res = trace_to_bus(array);
    if (!local_res.empty())
      res[array] = local_res;
  }
  auto tmp_partitions = analyzed_partitions;
  analyzed_partitions.clear();
  SmallPtrSet<Value *, 4> automatic_partition_array;
  for (auto R : res) {
    auto array = R.first;
    for (auto r : R.second) {
      Value *address = r.first;
      bool is_bus = r.second;
      if (is_bus) {
        DEBUG(dbgs() << "Drop automatic partition for array ";
              address->print(dbgs());
              dbgs() << " because of interface partition\n");
        continue;
      }
      Value *top_array = nullptr;
      int dim = -1;
      get_array_from_address(address, array, top_array, dim);
      if (top_array == nullptr)
        continue;
      if (lookup(manual_partition_array, top_array) != nullptr) {
        DEBUG(dbgs() << "Drop automatic partition for array ";
              top_array->print(dbgs());
              dbgs() << " because of user partition/reshape\n");
        continue;
      }
      
      if (filter(top_array)) {
        DEBUG(dbgs() << "Drop automatic partition for array ";
              top_array->print(dbgs());
              dbgs() << " because of array streaming pragma, ipcore and etc\n");
        continue;
      }

      // TODO: partial access?
      auto banking_options = tmp_partitions[array];

      if (auto key = lookup(automatic_partition_array, top_array)) {
        copy_factors(banking_options, analyzed_partitions[key], dim);
      } else {
        vector<DenseMap<int, int>> arr_factors;
        copy_factors(banking_options, arr_factors, dim);
        analyzed_partitions[top_array] = arr_factors;
        automatic_partition_array.insert(top_array);
      }
      DEBUG(dbgs() << "Pass factors from "; array->print(dbgs());
            dbgs() << " to "; top_array->print(dbgs()); dbgs() << '\n');
    }
  }
  return true;
}

// In dual-port memory partitioning, the partitioning factor of
// the rightmost dimension is half as single port partitioning
void AutoMemoryPartition::dual_port_adjust() {
  for (auto &it : m_partitions) {
    Value *array = it.first;
    auto strategy = it.second;
    auto array_ty = get_array_type(array);
    auto arr_size = getArrayDimensionSize(array_ty);

    // For multidimensional arrays, when multiple dimensions are
    // partitioned dual port partition should not be applied
    int multi_dim_par = 0;
    if (arr_size.size() > 1) {
      for (int i = 0; i < arr_size.size() - 1; i++) {
        if (strategy.find(i) != strategy.end()) {
          if (strategy[i] != 1) {
            multi_dim_par = 1;
            break;
          }
        }
      }
    }
    if (multi_dim_par != 0) {
      continue;
    }
    for (auto &itt : strategy) {
      int factor = itt.second;
      int dim = itt.first;
      // Only apply to the rightmost dimension
      if (dim < arr_size.size() - 1) {
        continue;
      }

      if (factor >= 2) {
        int local_factor;
        if ((factor % 2) != 0) {
          local_factor = factor / 2 + 1;
        } else {
          local_factor = factor / 2;
        }
        // Dual port aligned to power of two
        int factor_po2 = local_factor;
        while (!isPo2(factor_po2)) {
          factor_po2++;
        }
        if (factor_po2 < factor) {
          local_factor = factor_po2;
        }
        DEBUG(dbgs() << "Apply dual port: " << dim << ", " << local_factor
                     << '\n');
        m_partitions[array][dim] = local_factor;
      }
    }
  }
}

void AutoMemoryPartition::print_array_location(Value *array) {
  if (auto inst = dyn_cast<Instruction>(array)) {
    if (auto dbg = FindAllocaDbgDeclare(array)) {
      inst = dbg;
    }
    DebugLoc loc = inst->getDebugLoc();
    if (loc) {
      dbgs() << "(";
      loc.print(dbgs());
      dbgs() << ")";
    }
  }
}

//  factor: '0'- complete partition;
//          '1'- no partition ; "x"- x>1, partition factor
bool AutoMemoryPartition::partition_analysis_node(
    Value *array, const BasicBlock *top_loop,
    const SmallVector<size_t, 4> &vec_dim_size,
    const DenseMap<Value *, SmallVector<CallInst *, 4>> &addrs_with_call_path,
    int dim, int opt, int &factor, DenseMap<int ,int> has_partition,
    SmallVector<int, 4> &indices, bool &dual_option, int &repeated) {

  DEBUG(dbgs() << "\n ==> parse access in dimension " << dim
               << " for variable: ";
        array->print(dbgs()); dbgs() << " "; print_array_location(array);
        dbgs() << '\n');

  //  Step 2: Get DenseMap<Value *, vector<DenseMap<Value *, int64_t>>>
  //  e.g. index: i*1+j*2+i*j*3+4 => <i,1> <j,2> <i*j,3> <const, 4> maybe
  //  we can use LLVM ScalarEvolution
  DenseMap<Value *, vector<DenseMap<Value *, int64_t>>> index_expr_full;
  vector<DenseMap<Value *, int64_t>> index_expr_copy;
  parse_full_accesses(index_expr_full, addrs_with_call_path, dim, indices);
  if (index_expr_full.size() == 0) {
    factor = 1;
    return true;
  }

  //  Step 2: Unroll the accesses, and remove the repeat accesses
  //  Get loop iterator variable range
  //  PROBLEM: reference under condition
  bool heuristic_on = false;
  int64_t dim_size = vec_dim_size[dim];
  // Get the actual dimension size of the pointer
  // Use the biggest size if the pointer points to different arrays
  if (dim_size == 0) {
    int max_size = dim_size;
    DenseMap<Value *, bool> res = trace_to_bus(array);
    for (auto r : res) {
      Value *address = r.first;
      bool is_bus = r.second;
      if (is_bus)
        continue;
      Value *top_array = nullptr;
      int dim = -1;
      get_array_from_address(address, array, top_array, dim);
      if (top_array == nullptr)
        continue;
      DEBUG(dbgs() << "\n TRACE: "; top_array->print(dbgs()); dbgs() << " ";
            print_array_location(top_array); dbgs() << '\n');

      auto array_ty = get_array_type(top_array);
      auto arr_size = getArrayDimensionSize(array_ty);
      if (vec_dim_size.size() == arr_size.size()) {
        int size = arr_size[dim];
        if (size > 0 && size > max_size) {
          max_size = size;
        }
      }
    }
    dim_size = max_size;
  }
  DEBUG(dbgs() << "dimension " << dim << " size: " << dim_size << '\n');

  unroll_ref_generate_full(array, top_loop, index_expr_full, index_expr_copy,
                           heuristic_on, dual_option, dim_size);
  //  Heuristic: if the dimension size is same as a related complete parallel
  //  loops's tripcount, complete partition this dimension
  //  For example:
  //  # int array[VAL][OTHER];
  //  # for(i=0; i<VAL; i++)
  //  #  #pragma HLS unroll
  //  #   array[any_ref(i)][xx] =
  //
  if (heuristic_on) {
    DEBUG(dbgs() << "Heuristically complete partition the dimension\n");
    factor = 0;
    return true;
  }

  // Step 3:
  // After unrolling still need remove repeated ref
  DenseMap<int, int> repeat_tag;
  remove_repeated_access(index_expr_copy, repeat_tag);

  // int64_t original_ref = index_expr_full.size();
  // int64_t total_ref = index_expr_copy.size();
  int64_t conflict_ref = index_expr_copy.size() - repeat_tag.size();
  repeated = repeat_tag.size();
  if (dim_size > 1 && conflict_ref >= dim_size / 2) {
    factor = 0;
    if (dim_size > XilinxThresh)
      factor = 1024;
    return true;
  }
  if ((opt >= 0 && conflict_ref <= 1) ||
      (opt < 0 && ((conflict_ref - opt - 1) / (-opt)) <= 1)) {
    factor = 1;
    return true;
  }
  if (dual_option) {
    if ((opt >= 0 && conflict_ref <= 2) ||
        (opt < 0 && ((conflict_ref - opt - 1) / (-opt) <= 2))) {
      factor = 1;
      dual_option = false;
      return true;
    }
  }

  // Step 4:
  // should be unrelated to llvm
  int bank_num = conflict_ref;
  if (opt < 0)
    factor = (bank_num - opt - 1) / (-opt);

  bank_renew(&bank_num);
  int ret = choose_bank(bank_num, index_expr_copy, repeat_tag, dim_size);
  if (ret) {
    factor = bank_num;
    return true;
  }

  // Step 5: possible heuristic
  //  Yuxin: 07/19/2019
  //  Add strong heuristic: when analysis failed, complete partition the
  //  dimension if there are more than 2 accesses Set threshold as 256
  //
  //  Yuxin: Nov/06/2019
  //  If cannot find the factor and decide to heuristically partition it,
  //  we completely partition it.  (according to xfopencv/hog case)
  if (conflict_ref >= 2 && has_partition.size() == 0 && dim_size > 0 &&
      dim_size <= HeuristicThresh) {
    DEBUG(dbgs() << "Heuristically complete partition the dimension\n");
    factor = 0;
    return true;
  }

  DEBUG(dbgs() << "Fail to find a partition factor\n" << '\n');
  return false;
}

// Input:
//    DenseMap<Value *, vector<DenseMap<Value *, int64_t>>> *index_expr_full
//    related unrolled loops
// Output:
//    unrolled index DenseMap
//    DenseMap<Value *, vector<DenseMap<Value *, int64_t>>> *index_expr_copy
void AutoMemoryPartition::unroll_ref_generate_full(
    Value *array, const BasicBlock *top_loop,
    DenseMap<Value *, vector<DenseMap<Value *, int64_t>>> &index_expr_full,
    vector<DenseMap<Value *, int64_t>> &index_expr_copy, bool &heuristic_on,
    bool &dual_option, int64_t dim_size) {

  DEBUG(dbgs() << "[print ==>] Unroll the references "
               << "\n original refs: " << index_expr_full.size() << '\n');
  for (auto index_info : index_expr_full) {
    auto &info = index_info.second;
    auto index = index_info.first;
    auto addr = index_to_addr[index];
    auto &visited_loops = addr_visited_loops[addr];
    SmallVector<const BasicBlock *, 4> vec_loop;
    for (auto index_atom : info[0]) {
      if (index_atom.first == CONST_ITER)
        continue;
      if (auto loop_header = dyn_cast<BasicBlock>(index_atom.first)) {
        vec_loop.push_back(loop_header);
      } else {
        DEBUG(dbgs() << "[print ==>] Fail to find the related loop: "
                     << index_atom.first->getName() << "\n");
        auto loop_iter = const_cast<BasicBlock *>(top_loop);
        if (loop_unroll_info.count(loop_iter) > 0 && 
            loop_unroll_info[loop_iter] != DISABLE_UNROLL) {
          //         auto factor = loop_unroll_info[loop_iter];
          //         if (factor == COMPLETE_UNROLL) {
          if (!isLoopInvariant(index_atom.first, loop_iter)) {
            DEBUG(dbgs() << "Loop invariant, Heuristic analysis " << '\n');
            if (dim_size <= HeuristicThresh) {
              heuristic_on = true;
            }
            //         }
            dual_option = false;
        }
        }
      }
    }
    auto index_expr_local = index_expr_full[index];
    for (auto loop : vec_loop) {
      if (visited_loops.count(loop) > 0) {
        DEBUG(dbgs() << "[print ==>] SKIP replicated unrolled reference\n");
        continue;
      }
      visited_loops.insert(loop);
      global_visited_loops[array].insert(loop);
      vector<DenseMap<Value *, int64_t>> index_expr_copy_tmp;
      unroll_ref_generate(index_expr_local, index_expr_copy_tmp, loop,
                          heuristic_on, dim_size);
      if (!index_expr_copy_tmp.empty())
        index_expr_local = index_expr_copy_tmp;
    }
    index_expr_full[index] = index_expr_local;
  }
  for (auto index_info : index_expr_full) {
    auto &info = index_info.second;
    for (int i = 0; i < info.size(); ++i) {
      //      DenseMap<Value *, int64_t> tmp_info;
      //      for (auto index_atom : info[i]) {
      //        tmp_info[index_atom.first] = index_atom.second;
      //      }
      index_expr_copy.push_back(info[i]);
    }
  }
  DEBUG(print_index(index_expr_copy);
        dbgs() << "[print ==>] Unroll the references, copied refs: "
               << index_expr_copy.size() << '\n');
}

bool AutoMemoryPartition::get_loop_bound_and_step(const BasicBlock *loop,
                                                    int64_t &trip_count,
                                                    int64_t &step,
                                                    int64_t &start) {
  start = 0;
  step = 1;
  trip_count = loop_trip_count[loop];
  DEBUG(dbgs() << "start = " << start << " step = " << step
               << " trip_count = " << trip_count << '\n');
  return trip_count != 0;
}

// Input:
//    target unroll loop
//    original index
// Output:
//    unrolled index
//    vector<DenseMap<Value *, int64_t>> *index_expr_copy,
void AutoMemoryPartition::unroll_ref_generate(
    vector<DenseMap<Value *, int64_t>> &index_expr_orig,
    vector<DenseMap<Value *, int64_t>> &index_expr_copy,
    const BasicBlock *loop_header, bool &heuristic_on, int64_t dim_size) {

  int local_opt = 1;
  if (loop_unroll_info.count(loop_header) > 0 && 
      loop_unroll_info[loop_header] != DISABLE_UNROLL) {
    local_opt = loop_unroll_info[loop_header];
  } else if (pipelined_loop.count(loop_header) > 0) {
    local_opt = -1;
  }
  int64_t trip_count = 0;
  int64_t step = 1;
  int64_t start = 0;
  int ret = get_loop_bound_and_step(loop_header, trip_count, step, start);
  if (!ret) {
    DEBUG(dbgs() << "cannot get constant bound " << '\n');
    return;
  }

  int64_t upper_bound = 1;
  int64_t lower_bound = 0;
  if (local_opt == 0) {
    //  complete unroll
    upper_bound = start + trip_count * step;
    lower_bound = start;
  } else if (local_opt > 0) {
    //  partial unroll: use unroll factor as upperbound
    upper_bound = local_opt;
    lower_bound = 0;
  } else {
    //  pipeline: No unroll required by default
    // index_expr_copy.push_back(idx);
    //    return;
  }

  auto loop_iter = const_cast<BasicBlock *>(loop_header);
  DEBUG(dbgs() << "For loop: "; dbgs() << loop_iter->getName();
        dbgs() << "\n unroll_bound: " << lower_bound << " to " << upper_bound
               << '\n');

  //  A[i+1];
  //  linear related to loop iterator i
  for (auto idx : index_expr_orig) {
    if (idx.find(loop_iter) != idx.end()) {
      for (int64_t j = lower_bound; j < upper_bound; j += step) {
        DenseMap<Value *, int64_t> idx_new;
        int64_t const_value = 0;
        if (idx.find(CONST_ITER) != idx.end()) {
          const_value = idx[CONST_ITER];
        }
        for (auto pair_coeff : idx) {
          auto pair_variable = pair_coeff.first;
          auto pair_const = pair_coeff.second;
          if (pair_variable == loop_iter) {
            const_value += idx[loop_iter] * j;
          }
          //  2*i: 2*(i+1) = 2*i+2
          //  2*i+2*j: 2*(i+1)+2*j = 2*i+2*j+2
          // TODO(youxiang): 2*i*j unroll
          // If variable is not current loop iterator, still push the original
          // pair
          if (local_opt != 0 ||
              (local_opt == 0 && pair_variable != loop_iter)) {
            idx_new[pair_variable] = pair_const;
          }
        }
        idx_new[CONST_ITER] = const_value;
        index_expr_copy.push_back(idx_new);
      }
    } else {
      //  A[idx[i]+2];
      //  non-linear related to loop iterator i
      index_expr_copy.push_back(idx);
      DEBUG(dbgs() << "Heuristic analysis " << '\n');
      for (auto pair_coeff : idx) {
        auto pair_expr = pair_coeff.first;
        if (pair_expr == nullptr) {
          continue;
        }
        if (loop_unroll_info.count(loop_iter) > 0) {
          auto factor = loop_unroll_info[loop_iter];
          if (factor == COMPLETE_UNROLL) {
            if (!isLoopInvariant(pair_expr, loop_iter)) {
              if (dim_size > 0 && dim_size == trip_count &&
                  dim_size <= HeuristicThresh) {
                DEBUG(dbgs() << "Loop invariant, Heuristic analysis " << '\n');
                heuristic_on = true;
                break;
              }
            }
          }
        }
      }
    }
  }
}

void AutoMemoryPartition::print_index(
    const vector<DenseMap<Value *, int64_t>> &index_expr) {
  for (auto expr : index_expr) {
    int i = 0;
    for (auto iter : expr) {
      if (iter.first == CONST_ITER) {
        dbgs() << iter.second;
      } else {
        dbgs() << iter.first->getName();
        dbgs() << " * " << iter.second;
      }
      if (i + 1 < expr.size())
        dbgs() << " + ";
      i++;
    }
    dbgs() << "\n";
  }
}

void AutoMemoryPartition::print_index(
    const vector<DenseMap<Value *, int64_t>> &index_expr,
    const DenseMap<int, int> &repeat_tag) {
  for (size_t i = 0; i < index_expr.size(); i++) {
    if (repeat_tag.find(i) == repeat_tag.end()) {
      int j = 0;
      for (auto iter : index_expr[i]) {
        if (iter.first == CONST_ITER) {
          dbgs() << iter.second;
        } else {
          dbgs() << iter.first->getName() << " * " << iter.second;
        }
        if (j + 1 < index_expr[i].size())
          dbgs() << " + ";
        j++;
      }
      dbgs() << "\n";
    }
  }
}

/*Remove the identical references from a CMirNode for array 'arr_name'*/
void AutoMemoryPartition::remove_repeated_access(
    const vector<DenseMap<Value *, int64_t>> &index_expr,
    DenseMap<int, int> &repeat_tag) {

  for (size_t i = 0; i < index_expr.size(); i++) {
    for (size_t j = i + 1; j < index_expr.size(); j++) {
      bool diff = false;
      auto index_i = index_expr[i];
      auto index_j = index_expr[j];
      if (repeat_tag.find(j) == repeat_tag.end()) {
        if (index_i.size() != index_j.size()) {
          continue;
        }
        for (auto iter : index_i) {
          auto value = iter.first;
          auto coeff = iter.second;
          if (index_j.find(value) != index_j.end()) {
            if (coeff != index_j[value]) {
              diff = true;
              break;
            }
          } else {
            diff = true;
            break;
          }
        }
        if (!diff) {
          repeat_tag[j] = 1;
        }
      }
    }
  }

  DEBUG(dbgs() << "[print ==>] Unroll the references, repeated refs: "
               << repeat_tag.size() << '\n');
}

int AutoMemoryPartition::choose_bank(
    int &bank_num, const vector<DenseMap<Value *, int64_t>> &index_expr,
    const DenseMap<int, int> &repeat_tag, int dim_size) {

  //  Set a limit for the partitioning factor
  while (bank_num <= XilinxThresh) {

    int ret = conflict_detect(bank_num, index_expr, repeat_tag);
    if (ret) {
      DEBUG(dbgs() << "[PARTITION-ANALYSIS] Bank = " << bank_num
                   << " success for non-conflict partitioning\n");
      return true;
    }
    bank_num++;
    bank_renew(&bank_num);

    if (dim_size > 1 && bank_num > dim_size / 2) {
      break;
    }
  }
  return false;
}

// Conflict access detection, check wether if the bank number can guarantee
// conflict-free access
int AutoMemoryPartition::conflict_detect(
    int bank_tmp, const vector<DenseMap<Value *, int64_t>> &index_expr,
    const DenseMap<int, int> &repeat_tag) {

  for (size_t i = 0; i < index_expr.size(); i++) {
    for (size_t j = i + 1; j < index_expr.size(); j++) {
      auto index_j = index_expr[j];
      DenseMap<Value *, int64_t> coeffs_tmp;
      DenseMap<Value *, int64_t> coeffs_tmp1;
      vector<int64_t> coeffs;
      int64_t cn = 1;
      if (repeat_tag.find(j) == repeat_tag.end()) {
        for (auto &iter_a : index_expr[i]) {
          auto value = iter_a.first;
          auto coeff = iter_a.second;
          if (value != nullptr) {
            if (index_j.find(value) == index_j.end()) {
              coeffs_tmp[value] = coeff;
            } else {
              coeffs_tmp[value] = (coeff - index_j[value]);
            }
          } else {
            cn = coeff - index_j[value];
          }
        }
        for (auto &iter_b : index_expr[j]) {
          if (iter_b.first != nullptr) {
            if (index_expr[i].find(iter_b.first) == index_expr[i].end()) {
              coeffs_tmp[iter_b.first] = -iter_b.second;
            }
          }
        }
        //    set<Value *> coeffs_find;
        //    for (auto &iter1 : coeffs_tmp) {
        //      if (coeffs_find.count(iter1.first) > 0) {
        //        continue;
        //      }
        //      for (auto &iter2 : coeffs_tmp) {
        //        if (iter1 == iter2) {
        //          coeffs_tmp1[iter1.first] = iter1.second;
        //          continue;
        //        }
        //      }
        //    }
        for (auto iter1 : coeffs_tmp) {
          coeffs.push_back(iter1.second);
        }
        if (coeffs.empty()) {
          coeffs.push_back(0);
        }

        if (intdiv(cn, gcd2(ngcd(coeffs, coeffs.size()), bank_tmp)) == 0) {
          DEBUG(dbgs() << "Check fail: gcd ( "; for (auto cf
                                                     : coeffs) dbgs()
                                                << cf << ",";
                dbgs() << bank_tmp << ")|" << cn << '\n');
          return false;
        }
      }
    }
  }
  return true;
}

// Input:  an array's access/value under a loop scope
//         dimension
// Output:  DenseMap<Value *, vector<DenseMap<Value *, int>>> index_expr_full
//   DenseMap index_expr_full: <index_pointer, index_vector
//   <iterator/variable/constant, coefficient>>
bool AutoMemoryPartition::parse_full_accesses(
    DenseMap<Value *, vector<DenseMap<Value *, int64_t>>> &index_expr_full,
    const DenseMap<Value *, SmallVector<CallInst *, 4>> &addrs_with_call_path,
    int dim, SmallVector<int, 4> &indices) {

  // for (each index) {
  //   // Step 1: parse the access, see if it is:
  //   // partial access, cross function access
  //   // &/* op
  //   analyze_array_reference();

  //   // Step 2: TODO
  //   // cross_function_analysis();

  //   // Step 3: index analysis
  //   // Similar to CMarsExpression
  //   // DenseMap <iterator/variable/constant, coefficient>
  //   parse_access_index();
  // }
  // return true;
  for (auto one_addr_with_call_path : addrs_with_call_path) {
    auto addr = one_addr_with_call_path.first;
    if (auto bitcast = dyn_cast<BitCastInst>(addr)) {
      addr = bitcast->getOperand(0);
    }
    Value *index = nullptr;
    if (auto gep = dyn_cast<GetElementPtrInst>(addr)) {
      if (matched_indices(gep, indices)) {
        if (gep->getNumIndices() > dim + indices.size()) {
          index = gep->getOperand(dim + 1 + indices.size());
        }
      }
    } else if (auto ce = dyn_cast<ConstantExpr>(addr)) {
      if (ce->getOpcode() == Instruction::GetElementPtr) {
        if (matched_indices(ce, indices)) {
          if (ce->getNumOperands() > dim + indices.size()) {
            index = ce->getOperand(dim + 1 + indices.size());
          }
        }
      }
    }

    // Step 1: parse the access, see if it is:
    // partial access, cross function access
    // &/* op
    // analyze_array_reference();

    // Step 2: TODO
    // cross_function_analysis();

    if (index == nullptr)
      continue;
    // Step 3: index analysis
    // Similar to CMarsExpression
    // DenseMap <iterator/variable/constant, coefficient>
    auto res = parse_access_index(index, one_addr_with_call_path.second);
    index_expr_full[index].push_back(res);
    index_to_addr[index] = addr;
  }
  DEBUG(dbgs() << "[PRINT] print all the parsed accesses: \n";
        for (auto index_info
             : index_expr_full) {
          index_info.first->print(dbgs());
          dbgs() << ": \n";
          print_index(index_info.second);
        });

  return true;
}

// input : 'se_expr'
// output : 'res' -> key: unknown value| loop induction variable |CONST_ITER
bool AutoMemoryPartition::parse_coeff_scev(const SCEV *se_expr,
                                             DenseMap<Value *, int64_t> &res,
                                             ScalarEvolution &SE) {
  if (isa<SCEVCouldNotCompute>(se_expr)) {
    res.clear();
    return false;
  }
  if (auto *sc_sext = dyn_cast<SCEVSignExtendExpr>(se_expr)) {
    return parse_coeff_scev(sc_sext->getOperand(), res, SE);
  }
  if (auto *sc_zext = dyn_cast<SCEVZeroExtendExpr>(se_expr)) {
    return parse_coeff_scev(sc_zext->getOperand(), res, SE);
  }
  if (auto *sc_unknown = dyn_cast<SCEVUnknown>(se_expr)) {
    res[sc_unknown->getValue()] += 1;
    return true;
  }
  if (auto cs_se_expr = dyn_cast<SCEVConstant>(se_expr)) {
    res[CONST_ITER] += cs_se_expr->getValue()->getSExtValue();
    return true;
  }
  if (auto cs_add_rec = dyn_cast<SCEVAddRecExpr>(se_expr)) {
    if (cs_add_rec->isAffine()) {
      auto loop = cs_add_rec->getLoop();
      Value *loop_iter = nullptr;
      // we may not get canonical loop so we use loop header as
      // virtual loop iterator
      if (loop)
        loop_iter = loop->getHeader();
      auto start = cs_add_rec->getStart();
      auto cs_step = dyn_cast<SCEVConstant>(cs_add_rec->getStepRecurrence(SE));
      if (loop_iter != nullptr && start != nullptr && cs_step != nullptr) {
        if (parse_coeff_scev(start, res, SE)) {
          res[loop_iter] += cs_step->getValue()->getSExtValue();
          return true;
        }
      }
    }
    res.clear();
    return false;
  }
  if (auto cs_add = dyn_cast<SCEVAddExpr>(se_expr)) {
    for (auto op : cs_add->operands()) {
      if (!parse_coeff_scev(op, res, SE)) {
        res.clear();
        return false;
      }
    }
    return true;
  }
  if (auto cs_mul = dyn_cast<SCEVMulExpr>(se_expr)) {
    if (cs_mul->getNumOperands() == 2) {
      DenseMap<Value *, int64_t> sub_res;
      auto op0 = cs_mul->getOperand(0);
      auto op1 = cs_mul->getOperand(1);
      if (dyn_cast<SCEVConstant>(op0)) {
        std::swap(op0, op1);
      }
      if (auto cs_op1 = dyn_cast<SCEVConstant>(op1)) {
        int64_t coeff = cs_op1->getValue()->getSExtValue();
        if (parse_coeff_scev(op0, sub_res, SE)) {
          for (auto ele : sub_res) {
            ele.second *= coeff;
            res[ele.first] += ele.second;
          }
        } else {
          res.clear();
          return false;
        }
        return true;
      }
    }
    res.clear();
    return false;
  }
  res.clear();
  return false;
}

DenseMap<Value *, int64_t> AutoMemoryPartition::parse_access_index(
    Value *index, const SmallVector<CallInst *, 4> &call_path) {
  DenseMap<Value *, int64_t> res;
  if (auto add = dyn_cast<BinaryOperator>(index)) {
    if (add->getParent() == nullptr) {
      for (auto &op : add->operands()) {
        auto sub_res = parse_access_index(op.get(), call_path);
        for (auto atom : sub_res) {
          res[atom.first] += atom.second;
        }
      }
      return res;
    }
  }
  if (auto cs = dyn_cast<ConstantInt>(index)) {
    res[CONST_ITER] = cs->getSExtValue();
    return res;
  }
  auto inst = dyn_cast<Instruction>(index);
  if (!inst || inst->getParent() == nullptr) {
    DEBUG(dbgs() << "found invalid array index: "; index->print(dbgs());
          dbgs() << '\n');
    res[index] = 1;
    return res;
  }
  ScalarEvolution &SE =
      getAnalysis<ScalarEvolutionWrapperPass>(*inst->getFunction()).getSE();
  auto se_expr = SE.getSCEV(index);
  parse_coeff_scev(se_expr, res, SE);
  if (res.empty()) {
    res[index] = 1;
  }
  // propagate the scalar argument across function
  SmallVector<Argument *, 4> all_args;
  for (auto atom : res) {
    auto val = atom.first;
    if (val == CONST_ITER)
      continue;
    auto arg = dyn_cast<Argument>(val);
    if (!arg)
      continue;
    all_args.push_back(arg);
  }
  for (auto arg : all_args) {
    auto callee = arg->getParent();
    auto arg_no = arg->getArgNo();
    auto actual_val = get_actual_argument(callee, arg_no, call_path);
    if (actual_val) {
      auto sub_res = parse_access_index(actual_val, call_path);
      auto orig_coeff = res[arg];
      for (auto sub_atom : sub_res) {
        res[sub_atom.first] += sub_atom.second * orig_coeff;
      }
      res.erase(arg);
    }
  }
  return res;
}

void AutoMemoryPartition::generate_new_partition_intrinsic(
    Value *array, partition_record new_partition, Instruction *insertPos) {
  if (sideeffect_intrinsic != nullptr) {
    SmallVector<Value *, 1> args;
    SmallVector<Value *, 4> Inputs;
    auto array_arg = array;
    if (isa<AllocaInst>(array) || isa<GlobalVariable>(array)) {
      SmallVector<Value *, 2> idx_list;
      idx_list.push_back(ConstantInt::get(int64_ty, 0));
      idx_list.push_back(ConstantInt::get(int64_ty, 0));
      array_arg =
          GetElementPtrInst::CreateInBounds(array, idx_list, "", insertPos);
    }
    Inputs.push_back(array_arg);
    Inputs.push_back(ConstantInt::get(int32_ty, new_partition.type));
    Inputs.push_back(ConstantInt::get(int32_ty, new_partition.factor));
    Inputs.push_back(ConstantInt::get(int32_ty, new_partition.dim));
    Inputs.push_back(ConstantInt::get(int1_ty, 0));
    OperandBundleDef partition_bundle(xilinx_array_partition_bundle_tag,
                                      Inputs);
    auto call = CallInst::Create(sideeffect_intrinsic, args,
                                 ArrayRef<OperandBundleDef>{partition_bundle},
                                 "", insertPos);
    call->setOnlyAccessesInaccessibleMemory();
    call->setDoesNotThrow();
  }
}

void AutoMemoryPartition::generate_new_reshape_intrinsic(
    Value *array, reshape_record new_reshape, Instruction *insertPos) {

  if (sideeffect_intrinsic != nullptr) {
    SmallVector<Value *, 1> args;
    SmallVector<Value *, 4> Inputs;
    auto array_arg = array;
    if (isa<AllocaInst>(array) || isa<GlobalVariable>(array)) {
      SmallVector<Value *, 2> idx_list;
      idx_list.push_back(ConstantInt::get(int64_ty, 0));
      idx_list.push_back(ConstantInt::get(int64_ty, 0));
      array_arg =
          GetElementPtrInst::CreateInBounds(array, idx_list, "", insertPos);
    }
    Inputs.push_back(array_arg);
    Inputs.push_back(ConstantInt::get(int32_ty, new_reshape.type));
    Inputs.push_back(ConstantInt::get(int32_ty, new_reshape.factor));
    Inputs.push_back(ConstantInt::get(int32_ty, new_reshape.dim));
    OperandBundleDef reshape_bundle(xilinx_array_reshape_bundle_tag, Inputs);
    auto call = CallInst::Create(sideeffect_intrinsic, args,
                                 ArrayRef<OperandBundleDef>{reshape_bundle}, "",
                                 insertPos);
    call->setOnlyAccessesInaccessibleMemory();
    call->setDoesNotThrow();
  }
}

Value *AutoMemoryPartition::simplifyGEP(Value *addr) {
  if (auto gep = dyn_cast<GetElementPtrInst>(addr)) {
    bool access_data_member = false;
    gep_type_iterator GTI = gep_type_begin(*gep);
    for (User::op_iterator I = gep->op_begin() + 1, E = gep->op_end(); I != E;
         ++I, ++GTI) {
      // Skip indices into struct types.
      if (isa<StructType>(GTI.getIndexedType())) {
        access_data_member = I + 1 != E;
        break;
      }
    }
    // convert (gep array, 0, 0) => array
    if (gep->getNumOperands() <= 3 && gep->hasAllZeroIndices() &&
        !access_data_member)
      return gep->getOperand(0);
  }
  if (auto ce = dyn_cast<ConstantExpr>(addr)) {
    if (ce->getOpcode() == Instruction::GetElementPtr) {
      bool access_data_member = false;
      gep_type_iterator GTI = gep_type_begin(*ce);
      bool all_zero_indices = true;
      for (User::op_iterator I = ce->op_begin() + 1, E = ce->op_end(); I != E;
           ++I, ++GTI) {
        // Skip indices into struct types.
        if (isa<StructType>(GTI.getIndexedType())) {
          access_data_member = I + 1 != E;
          break;
        }
        if (cast<ConstantInt>(*I)->isZero())
          continue;
        all_zero_indices = false;
      }
      // convert (gep array, 0, 0) => array
      if (ce->getNumOperands() <= 3 && all_zero_indices &&
          !access_data_member)
        return ce->getOperand(0);
    }
  }
  return addr;
}

int64_t AutoMemoryPartition::gcd2(int64_t a, int64_t b) {
  int64_t c;
  if (a < 0) {
    a = -a;
  }
  //  b is always positive here
  //  if (b < 0)
  //    b = -b;
  //
  // swap a,b
  if (a < b) {
    c = a;
    a = b;
    b = c;
  }

  if (b == 0) {
    return a;
    //  a will never be 0 here
    //  else if (a == 0)
    //    return b;
  }
  { return gcd2(b, a % b); }
}

int AutoMemoryPartition::intdiv(int64_t a, int64_t b) {
  if ((a % b) != 0) {
    return 1; //  Cannot be divided
  }
  {
    return 0; //  Can be divided
  }
}

int64_t AutoMemoryPartition::ngcd(vector<int64_t> a, int n) {
  if (n == 1) {
    return a[0];
  }
  { return gcd2(a[n - 1], ngcd(a, n - 1)); }
}

bool AutoMemoryPartition::isPo2(int bank) {
  if (bank == 2) {
    return true;
  }
  if ((bank % 2) != 0) {
    return false;
  }
  return isPo2(bank / 2);
}

int AutoMemoryPartition::choose_factor(
    Value *array, const vector<DenseMap<int, int>> &p_options,
    DenseMap<int, int> &p_decision) {
  DEBUG(dbgs() << "Choose factor:\n");
  auto array_ty = get_array_type(array);
  auto arr_size = getArrayDimensionSize(array_ty);
  int opt = 1;
  // Yuxin: 07/15/2019
  // Deal with dim=0 array_partition pragma
  for (auto candidate : p_options) {
    if (candidate.count(-1) > 0) {
      DEBUG(dbgs() << "Fully partition the array into registers\n";
            dbgs() << "dim-" << -1 << ", factor-" << candidate[-1] << "\n");
      p_decision[-1] = 0;
      return opt;
    }
  }
  for (int i = 0; i < arr_size.size(); i++) {
    vector<int> factors;
    for (auto candidate : p_options) {
      if (candidate.count(i) <= 0) {
        continue;
      }
      DEBUG(dbgs() << "dim-" << i << ", factor-" << candidate[i] << "\n");
      factors.push_back(candidate[i]);
    }
    if (factors.empty()) {
      continue;
    }
    // Yuxin: Jun.13.2019
    // Merge the partition factors, choose the biggest one
    // For Intel flow, we will report warning if the chosen factor cannot be
    // divded by any other factors
    int a = factors[0];
    if (a > 0) {
      for (size_t ii = 1; ii < factors.size(); ii++) {
        if (factors[ii] == 0) {
          a = 0;
          break;
        }
        if (factors[ii] > a) {
          if (factors[ii] % a != 0) {
            opt = 0;
          }
          a = factors[ii];
        } else if (factors[ii] < a) {
          if (a % factors[ii] != 0) {
            opt = 0;
          }
        }
      }
    }

    if (arr_size[i] > 0 && arr_size[i] < XilinxThresh && a > arr_size[i] / 2) {
      a = 0;
    }

    if (a == 0 && arr_size[i] > XilinxThresh) {
      a = XilinxThresh;
    }
    //  FIXME: set the max factor as the partition factor (or
    //  complete unroll)
    p_decision[i] = a;
    DEBUG(dbgs() << "dim-" << i << ", size-" << arr_size[i] << ", choose-" << a
                 << '\n');
  }
  return opt;
}

bool AutoMemoryPartition::update_memory_partition_intrinsic(
    Value *array, const vector<partition_record> &vec_new_partition) {
  bool changed = false;
  bool first = true;
  for (auto call : hls_partition_table[array]) {
    vector<OperandBundleDef> remain_bundles;
    unsigned index = 0;
    while (index < call->getNumOperandBundles()) {
      const auto &bundle_info = call->getOperandBundleAt(index++);
      if (!bundle_info.getTagName().equals(xilinx_array_partition_bundle_tag))
        continue;
      // call void @llvm.sideeffect() [ "xlx_array_partition"(%variable, i32
      // type, i32 factor, i32 dim) ]
      // * type: 0 for cyclic, 1 for block, 2 for complete
      Value *target_array = simplifyGEP(bundle_info.Inputs[0]);
      if (target_array == array)
        continue;
      remain_bundles.push_back(OperandBundleDef(bundle_info));
    }
    if (first) {
      for (auto new_partition : vec_new_partition) {
        SmallVector<Value *, 4> Inputs;
        Inputs.push_back(array);
        Inputs.push_back(ConstantInt::get(int32_ty, new_partition.type));
        if (new_partition.type != COMPLETE_PARTITION)
          Inputs.push_back(ConstantInt::get(int32_ty, new_partition.factor));
        Inputs.push_back(ConstantInt::get(int32_ty, new_partition.dim));
        OperandBundleDef partition_bundle(xilinx_array_partition_bundle_tag,
                                          Inputs);
        remain_bundles.push_back(partition_bundle);
      }
      first = false;
    }
    if (!remain_bundles.empty()) {
      SmallVector<Value *, 1> args;
      auto new_call =
          CallInst::Create(sideeffect_intrinsic, args, remain_bundles);
      new_call->insertBefore(call);
    }
    call->eraseFromParent();
    changed = true;
  }
  return changed;
}

bool AutoMemoryPartition::remove_memory_partition_intrinsic(Value *array) {
  bool changed = false;
  for (auto call : hls_partition_table[array]) {
    to_remove_intrinsic.insert(call); 
    changed = true;
  }
  return changed;
}

DenseMap<Value *, bool> AutoMemoryPartition::trace_to_bus(Value *array) {
  DenseMap<Value *, bool> res;
  if (isa<AllocaInst>(array) || isa<GlobalVariable>(array)) {
    res[array] = false;
    return res;
  }
  if (auto arg = dyn_cast<Argument>(array)) {
    auto func = arg->getParent();
    if (func->use_empty()) {
      // top kernel function, the array is bus port
      res[array] = true;
      return res;
    }
    auto arg_no = arg->getArgNo();
    for (auto &use : func->uses()) {
      auto call = dyn_cast<CallInst>(use.getUser());
      if (!call)
        continue;
      CallSite cs(call);
      if (cs.arg_size() <= arg_no)
        continue;
      auto actual_arg = cs.getArgument(arg_no);
      if (auto bitcast = dyn_cast<BitCastInst>(actual_arg)) {
        actual_arg = bitcast->getOperand(0);
      }
      auto sub_res = trace_to_bus(actual_arg);
      res.insert(sub_res.begin(), sub_res.end());
    }
  }
  if (auto gep = dyn_cast<GetElementPtrInst>(array)) {
    auto ptr = gep->getPointerOperand();
    if (isa<AllocaInst>(ptr) || isa<GlobalVariable>(ptr)) {
      res[array] = false;
      return res;
    }
    auto sub_res = trace_to_bus(ptr);
    SmallVector<Value *, 4> indices;
    gep_type_iterator GTI = gep_type_begin(*gep);
    bool indice_struct = false;
    for (User::op_iterator I = gep->op_begin() + 1, E = gep->op_end(); I != E;
         ++I, ++GTI) {
      if (indice_struct) {
        indices.push_back(*I);
      } else {
        indices.push_back(ConstantInt::get(int64_ty, 0));
      }
      indice_struct = false;
      // keep indices into struct types.
      if (isa<StructType>(GTI.getIndexedType())) {
        indice_struct = true;
      }
    }
    for (auto one : sub_res) {
      auto pre_array = one.first;
      auto is_bus = one.second;
      if (isa<Argument>(pre_array)) {
        auto addr = GetElementPtrInst::CreateInBounds(pre_array, indices);
        setName(addr);
        delete_later(addr);
        res[addr] = is_bus;
        //     res[pre_array] = is_bus;
      } else if (isa<AllocaInst>(pre_array) || isa<GlobalVariable>(pre_array)) {
        SmallVector<Value *, 4> pre_indices;
        pre_indices.insert(pre_indices.end(), indices.begin(), indices.end());
        auto addr = GetElementPtrInst::CreateInBounds(pre_array, pre_indices);
        setName(addr);
        delete_later(addr);
        res[addr] = is_bus;
        //    res[pre_array] = is_bus;
      } else if (auto pre_gep = dyn_cast<GetElementPtrInst>(pre_array)) {
        SmallVector<Value *, 4> pre_indices;
        for (auto &op : pre_gep->indices()) {
          pre_indices.push_back(op.get());
        }
        auto pre_pointer_operand = pre_gep->getPointerOperand();
        for (int i = 1; i < indices.size(); ++i) {
          pre_indices.push_back(indices[i]);
        }

        auto addr =
            GetElementPtrInst::CreateInBounds(pre_pointer_operand, pre_indices);
        setName(addr);
        delete_later(addr);
        res[addr] = is_bus;
        //      res[pre_pointer_operand] = is_bus;
      }
    }
  }
  return res;
}

DenseMap<Value *, SmallVector<CallInst *, 4>>
AutoMemoryPartition::get_access_address_cross_function(Use *arg,
                                                         CallInst *call) {
  DenseMap<Value *, SmallVector<CallInst *, 4>> res;
  int arg_no = -1;
  for (auto I = call->arg_begin(); I != call->arg_end(); ++I) {
    if (I == arg) {
      arg_no = I - call->arg_begin();
      break;
    }
  }
  if (arg_no == -1) {
    DEBUG(dbgs() << "invalid function call argument \n");
    return res;
  }
  Function *callee = call->getCalledFunction();
  if (!callee || callee->empty()) {
    DEBUG(dbgs() << "cannot find sub function or found an empty function \n");
    return res;
  }
  auto address_sub_function =
      get_access_address_cross_function(callee->arg_begin() + arg_no);
  Value *arg_val = arg->get();
  if (isa<Argument>(arg_val)) {
    for (auto one : address_sub_function) {
      one.second.push_back(call);
      res[one.first] = one.second;
    }
    return res;
  }
  auto is_alloca = isa<AllocaInst>(arg_val);
  auto is_global = isa<GlobalVariable>(arg_val);
  auto gep = dyn_cast<GetElementPtrInst>(arg_val);
  if (!is_alloca && !gep && !is_global) {
    DEBUG(dbgs() << "found an unsupported partial address: ";
          arg_val->print(dbgs()); dbgs() << '\n');
    return res;
  }
  auto ptr = arg_val;
  SmallVector<Value *, 4> indices;
  bool is_trail_zero = false;
  if (is_alloca || is_global) {
  } else {
    for (auto &index : gep->indices()) {
      indices.push_back(index.get());
    }
    ptr = gep->getPointerOperand();
    if (auto cs = dyn_cast<Constant>(indices.back())) {
      if (cs->isZeroValue()) {
        is_trail_zero = true;
      }
    }
  }

  for (auto sub_addr : address_sub_function) {
    auto sub_gep = dyn_cast<GetElementPtrInst>(sub_addr.first);
    if (sub_gep == nullptr && !isa<Argument>(sub_addr.first)) {
      DEBUG(dbgs() << "found unsupported address:  ";
            sub_addr.first->print(dbgs()); dbgs() << "\n");
      continue;
    }
    SmallVector<Value *, 4> new_indices = indices;
    if (sub_gep != nullptr) {
      int i = 0;
      for (auto &index : sub_gep->indices()) {
        if (i == 0 && new_indices.size() > 0) {
          auto second_idx = index.get();
          if (auto cs = dyn_cast<ConstantInt>(second_idx)) {
            if (cs->isZeroValue()) {
              i++;
              continue;
            }
          }
          if (is_trail_zero) {
            new_indices.back() = index.get();
          } else {
            Value *org_val = new_indices.back();
            Value *offset_val = index.get();
            auto org_type = dyn_cast<IntegerType>(org_val->getType());
            auto offset_type = dyn_cast<IntegerType>(offset_val->getType());
            if (org_type && offset_type) {
              if (org_type->getBitWidth() > offset_type->getBitWidth()) {
                offset_val = CastInst::Create(Instruction::SExt, offset_val, org_type);
                delete_later(cast<Instruction>(offset_val));
              } else if (org_type->getBitWidth() < offset_type->getBitWidth()) {
                org_val  = CastInst::Create(Instruction::SExt, org_val, offset_type);
                delete_later(cast<Instruction>(org_val));
              }
            }
            auto add_index = BinaryOperator::Create(
                Instruction::Add, org_val, offset_val);

            delete_later(add_index);
            // plus the trail index and second index
            new_indices.back() = add_index;
          }
        } else {
          new_indices.push_back(index.get());
        }
        ++i;
      }
    }
    auto addr = GetElementPtrInst::CreateInBounds(ptr, new_indices);
    setName(addr);
    delete_later(addr);
    sub_addr.second.push_back(call);
    res[addr] = sub_addr.second;
  }
  return res;
}

DenseMap<Value *, SmallVector<CallInst *, 4>>
AutoMemoryPartition::get_access_address_cross_function(Argument *array) {
  DenseMap<Value *, SmallVector<CallInst *, 4>> res;
  for (auto &use : array->uses()) {
    auto user = use.getUser();
    if (isa<BitCastInst>(user) && user->hasOneUse()) {
      user = user->user_back();
    }
    auto gep = dyn_cast<GetElementPtrInst>(user);
    auto call = dyn_cast<CallInst>(user);
    if (call != nullptr) {
      auto sub_res = get_access_address_cross_function(&use, call);
      res.insert(sub_res.begin(), sub_res.end());
      continue;
    }
    if (gep != nullptr) {
      bool contain_direct_access = false;
      for (auto &gep_use : gep->uses()) {
        auto user = gep_use.getUser();
        if (isa<BitCastInst>(user) && user->hasOneUse()) {
          user = user->user_back();
        }
        if (isa<LoadInst>(user) || isa<StoreInst>(user)) {
          contain_direct_access = true;
          continue;
        }
        if (auto call_gep = dyn_cast<CallInst>(user)) {
          auto sub_res = get_access_address_cross_function(&gep_use, call_gep);
          res.insert(sub_res.begin(), sub_res.end());
          continue;
        }
        DEBUG(dbgs() << "found unsupported access: ";
              use.getUser()->print(dbgs()); dbgs() << '\n');
      }
      if (contain_direct_access) {
        res[gep] = SmallVector<CallInst *, 4>{};
      }
      continue;
    }
    if (isa<LoadInst>(user) || isa<StoreInst>(user)) {
      res[array] = SmallVector<CallInst *, 4>{};
      continue;
    }
    DEBUG(dbgs() << "found unsupported access: "; use.getUser()->print(dbgs());
          dbgs() << '\n');
  }
  return res;
}

void AutoMemoryPartition::delete_later(Instruction *val) {
  floating_inst.insert(val);
}

void AutoMemoryPartition::delete_floating_inst() {
  for (auto i = floating_inst.size(); i > 0; --i) {
    auto inst = floating_inst[i - 1];
    if (inst->getParent() != nullptr)
      continue;
    inst->dropAllReferences();
    inst->deleteValue();
  }
  return;
}

void AutoMemoryPartition::remove_intrinsic_inst() {
  for (auto i = to_remove_intrinsic.size(); i > 0; --i) {
    auto inst = to_remove_intrinsic[i - 1];
    if (inst->getParent() != nullptr)
      continue;
    inst->dropAllReferences();
    inst->deleteValue();
  }
  return;
}

bool AutoMemoryPartition::doFinalization(Module &M) { return false; }

Value *AutoMemoryPartition::get_actual_argument(
    Function *callee, int arg_no,
    const SmallVector<CallInst *, 4> &call_paths) {
  for (auto call : call_paths) {
    auto curr_callee = call->getCalledFunction();
    if (curr_callee == callee) {
      CallSite cs(call);
      return cs.getArgument(arg_no);
    }
  }
  return nullptr;
}

void AutoMemoryPartition::copy_factors(
    const vector<DenseMap<int, int>> &original,
    vector<DenseMap<int, int>> &target, int dim_offset) {
  for (auto it : original) {
    DenseMap<int, int> new_temp;
    for (auto itt : it) {
      int factor = itt.second;
      int dim = itt.first + dim_offset;
      new_temp[dim] = factor;
    }
    target.push_back(new_temp);
  }
}

// get the target array (AllocaInst, Argument, et) and its offset dimension from
// address
// e.g.
// int a[1][2][3];
// sub(&a); // 'a', -1
// sub(a);// 'a', 0
// sub(a[0]); // 'a', 1
// sub(a[0][0]); // 'a', 2
// sub(&a[0][0][0]) // 'a', 2
//
void AutoMemoryPartition::get_array_from_address(Value *addr, Value *array,
                                                   Value *&top_array,
                                                   int &dim) {
  if (isa<AllocaInst>(addr) || isa<GlobalVariable>(addr)) {
    top_array = addr;
    if (isa<Argument>(array))
      dim = -1;
    else
      dim = 0;
    return;
  }
  if (isa<Argument>(addr)) {
    top_array = addr;
    dim = 0;
    return;
  }
  auto gep = dyn_cast<GetElementPtrInst>(addr);
  if (gep != nullptr) {
    gep_type_iterator GTI = gep_type_begin(*gep);
    SmallVector<Value *, 4> indices;
    bool indice_struct = false;
    bool contains_struct = false;
    size_t last_struct_index = 0;
    std::string suffix;
    for (User::op_iterator I = gep->op_begin() + 1, E = gep->op_end(); I != E;
         ++I, ++GTI) {
      if (indice_struct) {
        indices.push_back(*I);
        last_struct_index = indices.size();
        suffix += "." + std::to_string(
                            cast<ConstantInt>(*I)->getValue().getSExtValue());
      } else {
        indices.push_back(ConstantInt::get(int64_ty, 0));
      }
      indice_struct = false;
      // keep indices into struct types.
      if (isa<StructType>(GTI.getIndexedType())) {
        indice_struct = true;
        contains_struct = true;
      }
    }
    if (!contains_struct || last_struct_index == 0) {
      top_array = gep->getPointerOperand();
      bool is_local_array = isa<AllocaInst>(top_array);
      bool is_global_array = isa<GlobalVariable>(top_array);
      dim = gep->getNumIndices() - 1 - is_local_array - is_global_array;
      return;
    } else {
      if (last_struct_index == indices.size() ||
          last_struct_index + 1 == indices.size()) {
        auto gep_type = gep->getType();
        if (auto ptr_type = dyn_cast<PointerType>(gep_type)) {
          if (isa<ArrayType>(ptr_type->getElementType())) {
            if (last_struct_index == indices.size()) {
              // for a.b with array type, replace it with its first element address
              // for comparsion later
              indices.push_back(ConstantInt::get(int64_ty, 0));
              auto new_array = GetElementPtrInst::CreateInBounds(
                  gep->getPointerOperand(), indices);
              setName(new_array);
              delete_later(new_array);
              top_array = new_array;
              dim = 0;
              return;
            }
          }
        }
        top_array = gep;
        dim = 0;
        return;
      } else {
        auto new_array = GetElementPtrInst::CreateInBounds(
            gep->getPointerOperand(),
            ArrayRef<Value *>(indices.begin(),
                              indices.begin() + last_struct_index + 1));
        setName(new_array);
        delete_later(new_array);
        top_array = new_array;
        dim = indices.size() - last_struct_index - 1;
        return;
      }
    }
  } 

  if (auto ce = dyn_cast<ConstantExpr>(addr)) {
    if (ce->getOpcode() == Instruction::GetElementPtr) {
      GlobalVariable *Ptr = cast<GlobalVariable>(ce->getOperand(0));
      gep_type_iterator GTI = gep_type_begin(*ce);
      SmallVector<Constant *, 4> indices;
      bool indice_struct = false;
      bool contains_struct = false;
      size_t last_struct_index = 0;
      std::string suffix;
      for (User::op_iterator I = ce->op_begin() + 1, E = ce->op_end(); I != E;
          ++I, ++GTI) {
        if (indice_struct) {
          indices.push_back(cast<Constant>(*I));
          last_struct_index = indices.size();
          suffix += "." + std::to_string(
              cast<ConstantInt>(*I)->getValue().getSExtValue());
        } else {
          indices.push_back(ConstantInt::get(int64_ty, 0));
        }
        indice_struct = false;
        // keep indices into struct types.
        if (isa<StructType>(GTI.getIndexedType())) {
          indice_struct = true;
          contains_struct = true;
        }
      }
      if (!contains_struct || last_struct_index == 0) {
        top_array = ce->getOperand(0);
        bool is_global_array = isa<GlobalVariable>(top_array);
        dim = ce->getIndices().size() - 1 - is_global_array;
        return;
      } else {
        if (last_struct_index == indices.size() ||
            last_struct_index + 1 == indices.size()) {
          auto gep_type = ce->getType();
          if (auto ptr_type = dyn_cast<PointerType>(gep_type)) {
            if (isa<ArrayType>(ptr_type->getElementType())) {
              if (last_struct_index == indices.size()) {
                // for a.b with array type, replace it with its first element address
                // for comparsion later
                indices.push_back(ConstantInt::get(int64_ty, 0));
                auto new_array = ConstantExpr::getGetElementPtr(Ptr->getValueType(), Ptr, indices);
                top_array = new_array;
                dim = 0;
                return;
              }
            }
          }

          top_array = ce;
          dim = 0;
          return;
        } else {
          auto new_array = ConstantExpr::getGetElementPtr(Ptr->getValueType(), Ptr, ArrayRef<Constant*>(indices.begin(),
                indices.begin() + last_struct_index + 1));
          top_array = new_array;
          dim = indices.size() - last_struct_index - 1;
          return;
        }
      }
    }
  }
  assert(0 && "should not happen");
  top_array = addr;
  dim = 0;
  return;
}

void AutoMemoryPartition::apply_complete_unroll_on_sub_loop(Loop *L) {
  DEBUG(dbgs() << "flatten loop \'" << L->getName() << "\' ";
        print_loop_location(L); dbgs() << "\n");
  for (auto subL : getAllSubLoops(L)) {
    loop_unroll_info[subL->getHeader()] = COMPLETE_UNROLL;
  }
  for (auto blk : L->blocks()) {
    for (auto &inst : *blk) {
      auto call = dyn_cast<CallInst>(&inst);
      if (!call)
        continue;
      auto callee = call->getCalledFunction();
      if (!callee || callee->empty())
        continue;
      get_sub_functions_recursive(callee);
    }
  }
  return;
}

void AutoMemoryPartition::get_sub_functions_recursive(Function *func) {
  if (flattened_funcs.count(func) > 0) {
    return;
  }
  flattened_funcs.insert(func);
  for (auto &blk : *func) {
    for (auto &inst : blk) {
      auto call = dyn_cast<CallInst>(&inst);
      if (!call)
        continue;
      auto callee = call->getCalledFunction();
      if (!callee || callee->empty())
        continue;
      get_sub_functions_recursive(callee);
    }
  }
  return;
}

void AutoMemoryPartition::apply_auto_pipeline_for_fine_grained_loop(
    Module &M) {
  SmallVector<Function *, 10> vec_funcs;
  DenseMap<Function *, SmallSet<Function *, 4>> func_to_sub_funcs;
  DenseMap<Function *, SmallSet<Function *, 4>> func_to_callers;
  for (auto &F : M) {
    if (F.empty())
      continue;
    vec_funcs.push_back(&F);
    for (auto &u : F.uses()) {
      auto call = dyn_cast<CallInst>(u.getUser());
      if (!call)
        continue;
      auto caller = call->getFunction();
      if (!caller)
        continue;
      func_to_sub_funcs[caller].insert(&F);
      func_to_callers[&F].insert(caller);
    }
  }
  SmallVector<Function *, 10> new_vec_funcs;
  bool success = topological_sort(vec_funcs, func_to_sub_funcs, func_to_callers,
                                  new_vec_funcs);
  if (!success) {
    DEBUG(dbgs() << "found recursive function \n");
    return;
  }
  for (auto i = new_vec_funcs.size(); i > 0; --i) {
    auto F = new_vec_funcs[i - 1];
    if (F->empty()) continue;
    if (F->hasFnAttribute("fpga.blackbox")) 
      continue;
    if (flattened_funcs.count(F) > 0)
      continue;
    LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>(*F).getLoopInfo();
    bool function_contains_loop = false;
    for (auto L : LI.getLoopsInPreorder()) {
      auto header = L->getHeader();
      if (pipelined_loop.count(header) > 0 ||
          dataflowed_loop.count(header) > 0 ) {
        function_contains_loop = true;
        continue;
      }
      bool contains_no_complete_unrolled_loop = false;
      for (auto subL : L->getSubLoops()) {
        auto sub_header = subL->getHeader();
        if (pipelined_loop.count(sub_header) > 0 || (loop_unroll_info.count(sub_header) <= 0 || loop_unroll_info[sub_header] != COMPLETE_UNROLL) ||
            coarse_grained_loop.count(sub_header) > 0) {
          contains_no_complete_unrolled_loop = true;
          break;
        }
      }
      if (contains_no_complete_unrolled_loop) {
        function_contains_loop = true;
        coarse_grained_loop.insert(header);
        continue;
      }
      bool contains_unflattened_call = false;
      for (auto B : L->blocks()) {
        if (LI.getLoopFor(B) != L)
          continue;
        for (auto &I : *B) {
          auto call = dyn_cast<CallInst>(&I);
          if (!call)
            continue;
          auto callee = call->getCalledFunction();
          if (callee && callee->empty() && callee->doesNotAccessMemory())
            continue;
          if (!callee || flattened_funcs.count(callee) > 0)
            continue;
          contains_unflattened_call = true;
          break;
        }
        if (contains_unflattened_call)
          break;
      }
      if (contains_unflattened_call) {
        function_contains_loop = true;
        coarse_grained_loop.insert(header);
        continue;
      }
      if (loop_unroll_info.count(header) > 0 && loop_unroll_info[header] != DISABLE_UNROLL) {
        if (loop_unroll_info[header] != COMPLETE_UNROLL) {
          function_contains_loop = true;
        }
        continue;
      }
      pipelined_loop[header] = -1;
    }
    if (!function_contains_loop)
      flattened_funcs.insert(F);
  }
}

bool AutoMemoryPartition::topological_sort(
    const SmallVector<Function *, 10> &vec_funcs,
    DenseMap<Function *, SmallSet<Function *, 4>> &func_to_sub_funcs,
    DenseMap<Function *, SmallSet<Function *, 4>> &func_to_callers,
    SmallVector<Function *, 10> &res) {
  SmallVector<Function *, 10> remain_funcs(vec_funcs.begin(), vec_funcs.end());
  do {
    SmallVector<Function *, 10> tmp;
    for (auto func : remain_funcs) {
      if (func_to_callers[func].empty()) {
        res.push_back(func);
        for (auto sub_func : func_to_sub_funcs[func]) {
          func_to_callers[sub_func].erase(func);
        }
      } else {
        tmp.push_back(func);
      }
    }
    if (tmp.size() == remain_funcs.size())
      return false;
    remain_funcs.swap(tmp);
  } while (!remain_funcs.empty());
  return true;
}

SmallVector<AutoMemoryPartition::field_info, 4>
AutoMemoryPartition::decompose(Type *ty, SmallVector<int, 4> indices,
                                 StringRef name) {
  SmallVector<field_info, 4> res;
  auto vec_dim = getArrayDimensionSize(ty);
  int index_offset = vec_dim.size();
  while (index_offset-- > 0)
    indices.push_back(-1);
  auto base_type = getBaseType(ty);
  auto st_ty = dyn_cast<StructType>(base_type);
  if (!st_ty || IsRecursiveType(st_ty))
    return res;
  int index_val = 0;
  for (auto e : st_ty->elements()) {
    SmallVector<int, 4> new_indices = indices;
    std::string new_name = name;
    new_name += "." + std::to_string(index_val);
    new_indices.push_back(index_val);
    if (isa<ArrayType>(e)) {
      field_info fi;
      fi.type = e;
      fi.index_val = new_indices;
      fi.name = new_name;
      res.push_back(fi);
    } 
    index_val++;
    auto sub_res = decompose(e, new_indices, new_name);
    res.insert(res.end(), sub_res.begin(), sub_res.end());
  }
  return res;
}

bool AutoMemoryPartition::matched_indices(GetElementPtrInst *gep,
                                            SmallVector<int, 4> &indices) {
  for (int i = 0; i < indices.size(); ++i) {
    if (gep->getNumIndices() <= i)
      return false;
    if (indices[i] == -1)
      continue; // array script
    auto index = gep->getOperand(1 + i);
    auto c_index = dyn_cast<ConstantInt>(index);
    if (!c_index || c_index->getValue().getSExtValue() != indices[i])
      return false;
  }
  return true;
}

bool AutoMemoryPartition::matched_indices(ConstantExpr *gep,
                                            SmallVector<int, 4> &indices) {
  for (int i = 0; i < indices.size(); ++i) {
    if (gep->getNumOperands() <= i + 1)
      return false;
    if (indices[i] == -1)
      continue; // array script
    auto index = gep->getOperand(1 + i);
    auto c_index = dyn_cast<ConstantInt>(index);
    if (!c_index || c_index->getValue().getSExtValue() != indices[i])
      return false;
  }
  return true;
}

void AutoMemoryPartition::setName(GetElementPtrInst *gep) {
  if (gep->hasName())
    return;
  auto ptr = gep->getPointerOperand();
  if (!ptr->hasName())
    return;
  std::string name = ptr->getName();
  bool indice_struct = false;
  gep_type_iterator GTI = gep_type_begin(*gep);
  for (User::op_iterator I = gep->op_begin() + 1, E = gep->op_end(); I != E;
       ++I, ++GTI) {
    if (indice_struct) {
      name += "." +
              std::to_string(cast<ConstantInt>(*I)->getValue().getSExtValue());
    }
    indice_struct = false;
    // keep indices into struct types.
    if (isa<StructType>(GTI.getIndexedType())) {
      indice_struct = true;
    }
  }
  if (name == ptr->getName())
    gep->setName(name + ".addr");
  else
    gep->setName(name);
}

Value *AutoMemoryPartition::lookup(const SmallPtrSet<Value *, 4> &val_set,
                                     Value *key) {
  if (val_set.count(key) > 0)
    return key;
  for (auto v : val_set) {
    if (isIdenticalAddr(v, key)) {
      return v;
    }
  }
  return nullptr;
}

bool AutoMemoryPartition::isIdenticalAddr(Value *v, Value *key) {
  v = simplifyGEP(v);
  key = simplifyGEP(key);
  if (v == key)
    return true;
  auto v_gep = dyn_cast<GetElementPtrInst>(v);
  auto key_gep = dyn_cast<GetElementPtrInst>(key);
  if (!v_gep || !key_gep)
    return false;
  if (v_gep->getNumOperands() != key_gep->getNumOperands())
    return false;
  for (auto i = 0; i < v_gep->getNumOperands(); ++i) {
    if (v_gep->getOperand(i) == key_gep->getOperand(i)) continue;
    auto c_v_index = dyn_cast<ConstantInt>(v_gep->getOperand(i));
    auto c_key_index = dyn_cast<ConstantInt>(key_gep->getOperand(i));
    if (c_v_index != nullptr && c_key_index != nullptr &&
        c_v_index->getValue().getSExtValue() == c_key_index->getValue().getSExtValue())
      continue;
    return false;
  }
  return true;
}

void AutoMemoryPartition::bank_renew(int *bank_proc) {
  if (*bank_proc == 1) {
    return;
  }
  //  Partition factor can only be power-of-two
  if (PRIOR == "logic") {
    while (!isPo2(*bank_proc)) {
      (*bank_proc)++;
    }
  }
}

bool AutoMemoryPartition::isLoopInvariant(const Value *var,
                                            BasicBlock *loop_header) {
  if (!isa<Instruction>(var))
    return true;
  static DenseMap<BasicBlock *, DenseMap<const Value *, bool>> cached;
  if (cached[loop_header].count(var) <= 0) {
    LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>(*loop_header->getParent())
                       .getLoopInfo();
    auto loop = LI.getLoopFor(loop_header);
    if (loop && loop->isLoopInvariant(var)) {
      cached[loop_header][var] = true;
    } else {
      cached[loop_header][var] = false;
    }
  }
  return cached[loop_header][var];
}
