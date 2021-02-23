set(LLVM_TARGET_DEFINITIONS FPGA.td)

tablegen(LLVM FPGAGenSubtargetInfo.inc -gen-subtarget)
add_public_tablegen_target(FPGACommonTableGen)

add_llvm_target(FPGACodeGen
  FPGASubtarget.cpp
  FPGATargetMachine.cpp
  FPGATargetTransformInfo.cpp)

add_subdirectory(TargetInfo)
add_subdirectory(MCTargetDesc)
