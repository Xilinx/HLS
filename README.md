# Xilinx Vitis HLS LLVM 2021.1

The directory contains Xilinx HLS LLVM source code and examples for use with Xilinx Vitis HLS 2020.2 release.

Directory            | Description      
---------------------|------------------
hls-llvm-project | llvm-project submodule, only the `clang`, `clang-tools-extra`, and `llvm` sub-directories are used by Vitis HLS
[ext](ext) | External libraries required by hls-llvm-project
[plugins](plugins) | Custom llvm passes that can be built and used in Vitis HLS
[vitis_hls_examples](vitis_hls_examples) | Examples of using Vitis HLS with local hls-llvm-project or plugin binaries

## How to build hls-llvm-project
- Use a Xilinx compatible linux [Build Machine OS](https://www.xilinx.com/html_docs/xilinx2021_1/vitis_doc/acceleration_installation.html)
  - This requirement is due to [ext](ext) library usage
- Clone the HLS repo sources (including hls-llvm-project submodule)
- Install [CMake 3.4.3 or higher](https://cmake.org/download/)
- Install [ninja](https://ninja-build.org/) [optional for faster builds]
- run [build-hls-llvm-project.sh](build-hls-llvm-project.sh) in the cloned directory:
  > ./build-hls-llvm-project.sh

Copyright 2016-2021 Xilinx, Inc.
SPDX-License-Identifier: Apache-2.0
