set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_tool(xilinx-legacy-rewriter
  XilinxLegacyRewriter.cpp
  )

target_link_libraries(xilinx-legacy-rewriter
  PRIVATE
  clangAST
  clangBasic
  clangFrontend
  clangTooling
  clangToolingRefactor
  clangRewriteFrontend
  )
