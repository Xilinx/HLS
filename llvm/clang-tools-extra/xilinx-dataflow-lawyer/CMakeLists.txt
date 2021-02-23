set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_tool(xilinx-dataflow-lawyer
  XilinxDataflowLawyer.cpp
  )

target_link_libraries(xilinx-dataflow-lawyer
  PRIVATE
  clangAST
  clangBasic
  clangFrontend
  clangTooling
  clangToolingRefactor
  clangRewriteFrontend
  )
