set(LLVM_LINK_COMPONENTS support)

add_clang_library(clangTidyXilinxModule
  AggregateOnApintCheck.cpp
  ApintLiteralConvertationCheck.cpp
  ArrayStreamCheckCheck.cpp
  ConstantarrayParamCheck.cpp
  Directive2pragmaCheck.cpp
  DumpOpenclkernelCheck.cpp
  LabelAllLoopsCheck.cpp
  LoopBraceBracketCheck.cpp
  RemoveAssertCheck.cpp
  SsdmIntrinsicsArgumentsCheck.cpp
  SsdmIntrinsicsScopeCheck.cpp
  StreamInStructCheck.cpp
  SystemcDetectorCheck.cpp
  Tb31ProcessCheck.cpp
  TbProcessCheck.cpp
  TbRemoveTopCheck.cpp
  TbXfmatCheck.cpp
  TopParametersCheck.cpp
  WarnMayneedNoCtorAttributeCheck.cpp
  XfmatArrayGeometryCheck.cpp
  XilinxTidyModule.cpp
  
  LINK_LIBS
  clangAST
  clangASTMatchers
  clangBasic
  clangLex
  clangTidy
  clangTidyReadabilityModule
  clangTidyUtils
  )
