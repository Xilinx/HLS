set(LLVM_LINK_COMPONENTS
  Analysis
  AsmParser
  Core
  Support
  TransformUtils
  )

set(XILINXHLSIRSources
  TestUtils.cpp
  XILINXBRAMIntrinsicsTest.cpp
  XILINXFunctionUtils.cpp
  XILINXLoopUtils.cpp
  XILINXPragmasTest.cpp
  )

add_llvm_unittest(XILINXHLSIRTests
  ${XILINXHLSIRSources}
  )
