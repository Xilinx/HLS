# MemoryPartition Pass
Apply array partition pragma automatically according to access pattern analysis
to achieve target II for pipelined loop or achieve fully parallelism
for unrolled loop

This is used by the [plugin_auto_array_partition_flow_demo](../../vitis_hls_examples/plugin_auto_array_partition_flow_demo) example Vitis HLS project.

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
opt -always-inline -mem2reg -gvn -reflow-simplify-type -reflow-combine -mem2reg -gvn -indvars -load ./LLVMMemoryPartition.so -auto-memory-partition *.bc
```
