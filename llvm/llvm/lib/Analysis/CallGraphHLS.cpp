// (c) Copyright 2016-2020 Xilinx, Inc.
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
// This file defines '-dot-callgraph-hls', which emit a callgraphDemangle.dot
// containing the call graph with demangled function name of a module.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/CallPrinter.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/DOTGraphTraitsPass.h"
#include "llvm/Analysis/CallGraphHLS.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Support/GraphWriterHLS.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/ADT/SetVector.h"

using namespace llvm;

extern cl::opt<std::string> CFGHierUserfilelist;
static cl::opt<std::string> TopFunctionName("csim-top-function-name", cl::Hidden,
                                            cl::desc("The name of top function"));
namespace llvm {
  void initializeUserfileList();
  bool isFunctionDefinedByUser(Function *F);

  static std::string DemangleFunctionName(StringRef NameRef) {
    if (!NameRef.startswith("_Z"))
      return NameRef;

    std::string Name = NameRef;

    int Status = 0;
    if (char *Demangled =
            itaniumDemangle(Name.c_str(), nullptr, nullptr, &Status)) {
      if (Status == 0)
        Name = Demangled;
      std::free(Demangled);
      return Name;
    }

    return Name;
  }

  // c++ demangling API
  static std::string getSubStrDemangleName(std::string input, bool no_param) {
    // for GCC only: first must be _ and second must by A-Z
    if (input.size() <= 2) return input;
    if (input[0] != '_') return input;
    if ((input[1] > 'Z' || input[1] < 'A') && input[1] != '_') return input;

    static llvm::ItaniumPartialDemangler Context;

    // not a mangling name.
    if (Context.partialDemangle(input.c_str()))
        return input;

    if (!Context.isFunction() || !no_param) {
        std::unique_ptr<char, void(*)(void*)> res {
            Context.finishDemangle(nullptr, nullptr), std::free };
        return res.get();
    }

    // return function name without param
    std::unique_ptr<char, void(*)(void*)> res {
        Context.getFunctionName(nullptr, nullptr), std::free };
    return res.get();
  }

  /// Make adaptive to old libiberty cplus_demangle API:
  ///   in 3.9 flow, following string will appear, just catch the first string before
  ///   the first '.', compatable with old demangle API.
  ///   "_Z19find_Btranspose_newILi3EEvPA2048_7ap_uintILi128EEPS1_ib.auFgemmTop.1"
  static std::string getDemangleName(std::string input, bool no_param) {
    SmallVector<StringRef, 5> StrVec;
    StringRef(input).split(StrVec, ".");
    if (StrVec.empty())
        return input;

    // Assume that the padding string always added at the end of the mangled name.
    std::string DeStr = getSubStrDemangleName(StrVec[0].str(), no_param);
    if (DeStr != StrVec[0].str())
        return DeStr;
    // And if the first section is failed to demangle, then pass the total string.
    // llvm.dbg.value
    return getSubStrDemangleName(input, no_param);
  }

  bool isCtorOrDtor(std::string input) {
    // for GCC only: first must be _ and second must by A-Z
    if (input.size() <= 2) return false;
    if (input[0] != '_') return false;
    if ((input[1] > 'Z' || input[1] < 'A') && input[1] != '_') return false;

    static llvm::ItaniumPartialDemangler Context;

    // not a mangling name.
    if (Context.partialDemangle(input.c_str()))
        return false;

    if (!Context.isFunction())
        return false;

    // return isCtorOrDtor
    return Context.isCtorOrDtor();
  }

  static Function *findTopFunction(Module &M, const std::string TopName) {
    for (auto &F : M)
      if (getDemangleName(F.getName(), true) == TopName)
        return &F;

    return nullptr;
  }

  static bool isSSDMFunction(Function* F) {
    assert(F != 0);
    std::string name = getDemangleName(F->getName(), true);
    if (name.find("_ssdm") == 0)
        return true;

    return false;
  }

  static bool shouldIgnoreShow(Function* F) {
    if (F == 0)
      return true;

    if (F->isIntrinsic())
      return true;
    
    if (isSSDMFunction(F))
      return true;

    if(isCtorOrDtor(F->getName()))
      return true;

    if (!llvm::isFunctionDefinedByUser(F))
      return true;
    return false;
  }

  static std::string XilinxFID = "xilinx.fid";

  void setFunctionID(Function *Callee, unsigned Num) {
    Type *Int32 = IntegerType::get(Callee->getContext(), 32);
    auto *FID = ConstantAsMetadata::get(ConstantInt::get(Int32, Num));
    MDString *Name = MDString::get(Callee->getContext(), "fid");
    Metadata *Ops[] = {Name, FID};
    MDNode *MD = MDNode::get(Callee->getContext(), Ops);
    Callee->setMetadata(XilinxFID , MD);
  }

  int getFunctionID(Function *Callee) {
    if(MDNode *MD = Callee->getMetadata(XilinxFID)) {
      MDString *Name = dyn_cast<MDString>(MD->getOperand(0));
      if(Name && Name->getString() == "fid") {
        if(ConstantAsMetadata *MDNum = dyn_cast<ConstantAsMetadata>(MD->getOperand(1))) {
          int FID = cast<ConstantInt>(MDNum->getValue())->getSExtValue();
          errs() << "Function:" << Callee->getName() << " ---> FID:" << FID << "\n";
          return FID;
        }
      }
    }
    return -1;
  }

}

namespace llvm {

class CallGraphHLS: public CallGraph {
public:
  CallGraphHLS(Module &M): CallGraph(M) {}
  std::vector<CallGraphNode *> ShowCallGraphNode;
};

template <>
struct GraphTraits<CallGraphHLS *> : public GraphTraits<CallGraphNode *> {
  using PairTy =
      std::pair<const Function *const, std::unique_ptr<CallGraphNode>>;

  static NodeRef getEntryNode(CallGraphHLS *CGN) {
    return CGN->getExternalCallingNode(); // Start at the external node!
  }

  static CallGraphNode *CGGetValuePtr(const PairTy &P) {
    return P.second.get();
  }

typedef std::vector<CallGraphNode *>::iterator nodes_iterator;
  static nodes_iterator nodes_begin(CallGraphHLS *CG) { return CG->ShowCallGraphNode.begin(); }
  static nodes_iterator nodes_end  (CallGraphHLS *CG) { return CG->ShowCallGraphNode.end(); }
  static unsigned       size       (CallGraphHLS *CG) { return CG->ShowCallGraphNode.size(); }
};

template <> struct DOTGraphTraits<CallGraphHLS *> : public DefaultDOTGraphTraits {
  DOTGraphTraits(bool isSimple = false) : DefaultDOTGraphTraits(isSimple) {}

  static std::string getGraphName(CallGraphHLS *Graph) { return "Call graph demangled"; }

  std::string getNodeLabel(CallGraphNode *Node, CallGraphHLS *Graph) {
    if (Function *Func = Node->getFunction())
      return DemangleFunctionName(Func->getName());

    return "external node";
  }

  std::string getNodeDemangleName(CallGraphNode *Node, CallGraphHLS *Graph) {
    if (Function *Func = Node->getFunction())
      return getDemangleName(Func->getName(), true);

    return "external node";
  }

  std::string getNodeMangleName(CallGraphNode *Node, CallGraphHLS *Graph) {
    if (Function *Func = Node->getFunction())
      return Func->getName();

    return "external node";
  }

  int getLineNumber(Function* F) {
    int line = -1;
    if (DISubprogram *SP = F->getSubprogram()) {
      if(SP->getLine() > 0)
        line = SP->getLine();
    }
    return line;
  }

  std::string getFileName(Function* F) {
    std::string FileName = "";
    if (DISubprogram *SP = F->getSubprogram()) {
      if (DIFile * SourceFile = SP->getFile()) {
        FileName = SourceFile->getFilename();
      }
    }
    return FileName;
  }

  int getNodeLineNumber(CallGraphNode *Node, CallGraphHLS *Graph) {
    if (Function *Func = Node->getFunction())
      return getLineNumber(Func);
    return -1;
  }

  std::string getNodeFileName(CallGraphNode *Node, CallGraphHLS *Graph) {
    if (Function *Func = Node->getFunction())
      return getFileName(Func);
    return "";
  }

  int getNodeFunctionID(CallGraphNode *Node, CallGraphHLS *Graph) {
    if (Function *Func = Node->getFunction())
      return getFunctionID(Func);
    return -1;
  }

};

struct AnalysisCallGraphWrapperPassTraits {
  static CallGraphHLS *getGraph(CallGraphWrapperPass *P) {
    return static_cast<CallGraphHLS *>(&P->getCallGraph());
  }
};

} // end llvm namespace

namespace {
struct CallGraphHLSDOTPrinter : public DOTGraphTraitsModulePrinter<
                              CallGraphWrapperPass, true, CallGraphHLS *,
                              AnalysisCallGraphWrapperPassTraits> {
  static char ID;
  CallGraphHLSDOTPrinter()
      : DOTGraphTraitsModulePrinter<CallGraphWrapperPass, true, CallGraphHLS *,
                                    AnalysisCallGraphWrapperPassTraits>(
            "callgraphDemangle", ID) {
    initializeCallGraphHLSDOTPrinterPass(*PassRegistry::getPassRegistry());
  }

  void initShowCallGraphNode(CallGraphNode &CGN, 
                             std::vector<CallGraphNode *> &ShowCallGraphNode, 
                             SetVector<CallGraphNode *> &Visited) {
    if(Visited.count(&CGN))
      return ;
    Visited.insert(&CGN);
    Function *Callee = CGN.getFunction();
    if(shouldIgnoreShow(Callee))
      return ;

    ShowCallGraphNode.push_back(&CGN);

    for(unsigned i = 0; i < CGN.size(); i++) {
      CallGraphNode *CalleeNode = CGN[i];
      if (!CalleeNode)
        continue;
      initShowCallGraphNode(*CalleeNode, ShowCallGraphNode, Visited);
    }
    return ;
  }

  void initShowCallGraphNode(Module &M, CallGraphHLS *Graph) {
    Function *TopFunc = llvm::findTopFunction(M, TopFunctionName);
    CallGraphNode *StartNode = Graph->getExternalCallingNode();
    if(TopFunc) {
      CallGraphNode *TopNode = (*Graph)[TopFunc];
      if(TopNode)
        StartNode = TopNode;
    }

    SetVector<CallGraphNode *> Visited;
    initShowCallGraphNode(*StartNode, Graph->ShowCallGraphNode, Visited);
  }

  unsigned numberFunction(std::vector<CallGraphNode *> &ShowCallGraphNode) {
    unsigned FunctionNumber = 0;
    for(CallGraphNode *CGN:ShowCallGraphNode) {
      if(!CGN)
        continue;
      Function *Callee = CGN->getFunction();
      if(!Callee)
        continue;
      FunctionNumber++;
      setFunctionID(Callee, FunctionNumber);
      getFunctionID(Callee);
    }
    return FunctionNumber;
  }

  bool runOnModule(Module &M) override {
    CallGraphHLS * Graph = new CallGraphHLS(M);
    llvm::initializeUserfileList();
    initShowCallGraphNode(M, Graph);
    numberFunction(Graph->ShowCallGraphNode);
    std::string Filename = "callgraphDemangle.dot";
    errs() << "CFGHierUserfilelist:[" << CFGHierUserfilelist << "]\n";
    if(!TopFunctionName.empty()) {
      errs() << "TopFunctionName:" << TopFunctionName << "\n";
    }
    /*
     * revert to simple filename 
    std::string SourceFileName = M.getSourceFileName();
    if(!SourceFileName.empty()) {
      std::string BaseFileName = removeExtension(getBaseFileName(SourceFileName));
      if(!BaseFileName.empty()) 
        Filename = BaseFileName + "_" + Filename;
    }
    */
    std::error_code EC;

    errs() << "Writing '" << Filename << "'...";
    raw_fd_ostream File(Filename, EC, sys::fs::F_Text);
    std::string Title = DOTGraphTraits<CallGraphHLS *>::getGraphName(Graph);

    if (!EC)
      llvm::WriteGraphHLS(File, Graph, true, Title);
    else
      errs() << "  error opening file for writing!";
    errs() << "\n";
    free(Graph);

    return false;
  }
};

}

char CallGraphHLSDOTPrinter::ID = 0;
INITIALIZE_PASS(CallGraphHLSDOTPrinter, "dot-callgraph-hls",
                "Print call graph with demangled function name to 'dot' file", false, false)

ModulePass *llvm::createCallGraphHLSDOTPrinterPass() {
  return new CallGraphHLSDOTPrinter();
}
