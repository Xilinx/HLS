# Xilinx HLS LLVM source code

Directory            | Description      
---------------------|------------------
[clang](clang) | sources
[clang-tools-extra](clang-tools-extra) | sources
[llvm](llvm) | sources


## Setup to compile Vitis HLS LLVM
- Clone the sources
- Install [CMake 3.4.3 or higher](https://cmake.org/download/)
- Install [ninja](https://ninja-build.org/) [optional for faster builds]
- Install [Xilinx Vitis HLS](https://www.xilinx.com/support/download/index.html/content/xilinx/en/downloadNav/vitis/2020-2.html)
- Set XILINX_HLS environment variable to Vitis_HLS installation
    - This can be done by sourcing the Vitis_HLS settings64.sh file

## Compile Vitis HLS LLVM
run `build.sh`
