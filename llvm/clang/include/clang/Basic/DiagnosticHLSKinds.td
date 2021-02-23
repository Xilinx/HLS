//==--- DiagnosticHLSKinds.td -----HLS related diagnostics ----------------===//
//
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
// Xilinx HLS sema and parser related Diagnostic message
//
//===----------------------------------------------------------------------===//

let Component = "HLS" in {
let CategoryName = "HLS Issue" in {


// OpenCL Xilinx extension
def err_xcl_dataflow_on_non_for_loop : Error<
  "Only for-loops and functions support the dataflow">;
def err_xcl_dataflow_on_trivial_loop_body : Error<
  "The for-loop body with pragma/attribute must be enclosed within braces">;
def warn_remove_xcl_dataflow_attr : Warning<
  "Ignore dataflow attribute or pragma">,
  InGroup<IgnoredDataflowAttr>;
def warn_remove_interface_specific_attr : Warning<
  "Ignore interface attribute or pragma which is not used in top function">,
  InGroup<IgnoredInterfaceAttr>;

def warn_invalid_variable_expr: Warning<
  "invalid variable expr ">,
  InGroup<InvalidXlxPragmas>;

def warn_unexpected_pragma_parameter : Warning<
  "unexpected pragma parameter '%0'">,
  InGroup<InvalidXlxPragmas>;
def warn_unexpected_token_in_pragma_argument : Warning<
  "unexpected pragma argument '%0', expects %1">,
  InGroup<InvalidXlxPragmas>;
def warn_repeated_pragma_parameter : Warning<
  "pragma parameter '%0' repeated">,
  InGroup<InvalidXlxPragmas>;
def warn_pragma_named_argument_missing : Warning<
  "missing argument for '%0'">,
  InGroup<InvalidXlxPragmas>;

def warn_extra_tokens_before_variable_expression : Warning<
  "extra token before variable expression is ignored">,
  InGroup<ExtraTokens>;

def warn_extra_tokens_before_pragma_subject : Warning<
  "extra token before pragma subject is ignored">,
  InGroup<ExtraTokens>;
def warn_deprecated_pragma_ignored_by_scout: Warning<
  "'%0' is deprecated, and it will be removed in future release">,
  InGroup<InvalidXlxPragmas>;
def warn_deprecated_pragma_ignored_by_scout_skip_strict_mode: Warning<
  "'%0' is deprecated, and it will be removed in future release. It is suggested to replace it with '%1'.">,
  InGroup<IgnoredXlxPragmas>;
def warn_deprecated_pragma_option_ignored_by_scout: Warning<
  "'%0' in '#pragma HLS %1' is deprecated, and it will be removed in future release. It is suggested to replace it with '%2'.">,
  InGroup<IgnoredXlxPragmas>;
def warn_extra_pragma_hls_token_ignored : Warning<
  "'%0' in '#pragma HLS %1' is ignored">,
  InGroup<ExtraTokens>;
def warn_confilict_pragma_parameter : Warning<
  "pragma parameter '%0' conflicts with '%1'">,
  InGroup<InvalidXlxPragmas>;

def err_resource_pragma_xpm_memory_option_conflict : Error<
  "auto|distribute|block|uram can only be selected as one choice for XPM_MEMORY in Resource pragma.">;

def warn_conflict_pragma_parameter_and_ignored: Warning<
  "pragma option '%0' is conflicting with '%1', and option '%2' is ignored">,
  InGroup<IgnoredXlxPragmas>;

def warn_at_least_one_parameter_required: Warning<
  "at leas one of '%0' and '%1' option is required">,
  InGroup<IgnoredXlxPragmas>;

def error_pipeline_style_unsupport_value: Error<
  "The 'style' option of the 'HLS pipeline' directive or pragma only has 3 values: stp/flp/frp ">;
def error_pipeline_style_conflict: Error<
  "The 'style' option of the 'HLS pipeline' directive or pragma has conflict with %0">;
def warn_pipeline_enable_flush_deprecate: Warning<
  "The 'enable_flush' option of the 'HLS pipeline' directive or pragma is deprecated, and it is suggested to use style=flp as replacement.">;


def error_unknown_interface_mode : Error<
  "The 'mode' option of the 'HLS interface' pragma is either missing or set to incorrect position. Pls guarantee to put it following closely INTERFACE.">;
def warn_invalid_interface_port : Warning<
  "Invalid interface port in '#pragma HLS interface': expect function parameter">,
  InGroup<IgnoredXlxPragmas>;
def warn_invalid_funtion_level_interface_port : Warning<
  "Invalid function level interface port in '#pragma HLS interface': expect 'return'">,
  InGroup<IgnoredXlxPragmas>;
def err_xlx_pragma_invalid_unroll_factor : Error<
  "Invalid pragma unroll factor: '%0'">;
def warn_unsupported_interface_port_data_type : Warning<
  "Unsupported interface port data type in '#pragma HLS interface %0'">,
  InGroup<IgnoredXlxPragmas>;
def warn_unsupported_interface_bram_type : Warning<
  "Array of size 1 cannot be specified as %0, '#pragma HLS interface %0' is ignored">,
  InGroup<IgnoredXlxPragmas>;
def warn_invalid_pragma_variable : Warning<
  "Invalid variable in '#pragma HLS %0': expect variable to be %1">,
  InGroup<InvalidXlxPragmas>;
def warn_implicit_hls_stream : Warning<
  "Invalid variable in '#pragma HLS %0': expect variable to have 'hls::stream' type">,
  InGroup<InvalidXlxPragmas>;
def warn_obsolete_pragma_hls_token_replaced : Warning<
  "'%0' in '#pragma HLS %1' is replaced by '%2'">,
  InGroup<ObsoleteeXlxPragmas>;
def warn_obsolete_pragma_replaced : Warning<
  "'%0' is obsoleted and replaced by '%1'">,
  InGroup<ObsoleteeXlxPragmas>;
def err_xlx_pragma_not_in_function_scope : Error<
  "'#pragma HLS' is only allowed in function scope">;
def warn_xlx_pragma_applied_in_wrong_scope : Warning<
  "'#pragma HLS %0' can only be applied inside %select{function|loop|compound}1 body">, InGroup<InvalidXlxPragmas>;
def err_top_pragma_appiled_in_wrong_scope : Error<
  "'#pragma HLS top' cannot be applied in class method">;

def err_xlx_invalid_resource_option: Error< 
  "Invalid Directive: %0 is invalid combination options">;



//===----------------------------------------------------------------------===//
// Xilinx HLS  Semantic related Diagnostic message
//===----------------------------------------------------------------------===//

// OpenCL Xilinx Extension
def err_opencl_unroll_attr_wrong_target : Error<
  "opencl_unroll_hint attribute is only allowed on loop statements">;
def err_xcl_pipeline_loop_attr_wrong_target : Error<
  "xcl_pipeline_loop attribute is only allowed on loop statements">;
def err_xcl_parallel_loop_attr_wrong_target : Error<
  "xcl_parallel_loop attribute is only allowed on loop statements">;
def err_xcl_array_map_unknown_mode : Error<
  "unknown array mapping mode '%0'">;
def err_xcl_array_unknown_xform_type : Error<
  "unknown array %select{partition|reshape}0 type '%1'">;
def err_xcl_max_wg_size_wrong_number_arguments : Error<
  "'%0' attribute takes exactly 1 or 3 arguments">;
def err_xcl_dataflow_attr_spmd_kernel: Error<
  "The xcl_dataflow attribute requires use of the reqd_work_group_size(1, 1, 1) attribute value "
  "%select{; did you forget to add __attribute__ ((reqd_work_group_size(1, 1, 1)))?|}0">;
def err_xocc_wrong_compiler_version : Error<
  "'%0' is not supported by the current compiler">;
def warn_xocc_wrong_compiler_version : Warning<
  "'%0' is ignored by the current compiler">, InGroup<Deprecated>;

// HLS Xilinx Extension
def err_builtin_expected_type : Error<
  "builtin argument expected to be of type %0, was of type %1">;

def err_attr_in_wrong_scope : Error<
  "%0 attribute is only allowed on function or compound statements">;
def err_sdxkernel_in_wrong_scope : Error<
  "%0 cannot be used as top function since it's class method">;
def err_attribute_unsupported_for_func_param
    : Error<"%0 attribute is not supported for function parameters">;
def warn_attribute_unsupported_for_func_param
    : Warning<"%0 attribute is not supported for function parameters">;
def err_attribute_unexpected_value
    : Error<"xcl_dependence %0 = %1  is unexpected, only support %3">;


def warn_xlx_expr_not_ice : Warning<
  "expression is not an %select{integer|integral}0 constant expression">;
def warn_xlx_loop_tripcount_ignored: Warning<
  "%0, the pragma LOOP_TRIPCOUNT is ignored">;

def warn_allocation_conflict: Warning<
  "ALLOCATION pragma conflicts with %0, and will be ignored">,
  InGroup<IgnoredPragmas>;


def err_latency_value_is_out_of_range
   :Error<"'Latency' value %0 is out of range, valid value is %1">;

def err_xlx_attribute_invalid_option_and_because
    : Error<"the value for option %0 is invalid, %1" >;

def err_xlx_attribute_invalid_option
    : Error<"%0 is not valid value for option %1" >;
def warn_xlx_attribute_ignore_option : Warning<
  "%0 attribute will ignore the option %1 because %2">,
  InGroup<IgnoredPragmas>;
def warn_xlx_attribute_ignore_because_invalid_option: Warning<
  "%0 pragma is ignored, because %1 ">, 
  InGroup<InvalidXlxPragmas>;
def warn_xlx_attribute_argument_outof_range: Warning<
  "%0 requires integer const between %1 and %2 inclusive">, 
  InGroup<InvalidXlxPragmas>;

def warn_xlx_dependence_missing_dependent_option: Warning<
  "Assuming false dependence as a \"true/false\" selection was not specified">,
  InGroup<InvalidXlxPragmas>;


// ------- Scout HLS error for pragma options: error out directly
def err_xlx_pragma_not_supported_by_scout_HLS: Error<
   "the pragma is not supported">;
def err_xlx_pragma_option_not_supported_by_HLS: Error<
   "'%0' pragma '%1' option is not supported, please use '%2' instead.">;
def err_xlx_not_implement_yet :Error<
   "'%0' pragma is not supported">;
// ------- Scout HLS error for pragma options: unsupport but only warning out instead for temp
def err_xlx_pragma_not_supported_by_scout_HLS_WarnOut: Warning<
   "the pragma is not supported and will be ignored">,
  InGroup<InvalidXlxPragmas>;
def err_xlx_pragma_option_not_supported_by_HLS_WarnOut: Warning<
   "the '%1' option to '%0' pragma is not supported and will be ignored">,
  InGroup<InvalidXlxPragmas>;
def err_xlx_pragma_scope_not_supported_by_HLS_WarnOut: Warning<
   "the '%0' pragma is only supported for function and loop and will otherwise be ignored">,
  InGroup<InvalidXlxPragmas>;

def err_xlx_not_supported_by_scout_HLS: Error<
  "%0 is not supported">;

}
}

