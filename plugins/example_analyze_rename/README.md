# Renamer Pass

Example of a custom LLVM pass that renames the arguments of all functions.  
This is used by the [plugin_analyze_rename_flow_demo](../../vitis_hls_examples/plugin_analyze_rename_flow_demo) example Vitis HLS project.

## Setup to compile
You may build the custome pass using either a Vitis HLS installation or a local HLS LLVM build.

### Option 1. Compile using Vitis HLS
```
build.sh
```

### Option 2. Compile using local HLS LLVM build
First set your environment to point your local clang llvm build via PATH, LD_LIBRARY_PATH, etc. then run:
```
make all
```

## Run the pass
You can manually test your pass with a LLVM bitcode file (not available in this example)
```
opt -load ./LLVMRenamer.so -renamer *.bc
```

Copyright 2016-2021 Xilinx, Inc.
SPDX-License-Identifier: Apache-2.0
