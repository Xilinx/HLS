// (C) Copyright 2016-2020 Xilinx, Inc.
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
// This file defines external functions that can be called to explicitly
// instantiate the CFG printer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CFGHIERPRINTER_H
#define LLVM_ANALYSIS_CFGHIERPRINTER_H

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/PassManager.h"
//#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/GraphHierWriter.h"
#include "llvm/IR/DebugInfoMetadata.h"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/CallSite.h"
#include <set>
#include <map>
#include <vector>
#include <sstream>

extern llvm::cl::opt<bool> DumpAllBB;

namespace llvm {

struct HierGraphFunction {
  Function *F;
  LoopInfo *LI;
  // BBs not contained in loops, to partition with loop (subgraph)
  std::vector<BasicBlock *> TopBBs;
  // Hidden BB list
  std::set<BasicBlock *> HiddenBBs;
  // Execusion times for each BB
  std::map<BasicBlock *, uint64_t> BBExeNum;
};


//===--------------------------------------------------------------------===//
// GraphTraits specializations 
//===--------------------------------------------------------------------===//

template <> struct GraphTraits<HierGraphFunction*> : public GraphTraits<BasicBlock*> {
  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  typedef std::vector<BasicBlock *>::iterator nodes_iterator;
  static nodes_iterator nodes_begin(HierGraphFunction *HG) { return HG->TopBBs.begin(); }
  static nodes_iterator nodes_end  (HierGraphFunction *HG) { return HG->TopBBs.end(); }
  static unsigned       size       (HierGraphFunction *HG) { return HG->TopBBs.size(); }
};
//template <> struct GraphTraits<const HierGraphFunction*> : public GraphTraits<const BasicBlock*> {
//  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
//  typedef std::vector<BasicBlock *>::const_iterator nodes_iterator;
//  static nodes_iterator nodes_begin(const HierGraphFunction *HG) { return HG->TopBBs.begin(); }
//  static nodes_iterator nodes_end  (const HierGraphFunction *HG) { return HG->TopBBs.end(); }
//  static unsigned       size       (const HierGraphFunction *HG) { return HG->TopBBs.size(); }
//};

bool isFunctionDefinedByUser(Function *F);
bool isCtorOrDtor(std::string input);
void initializeUserfileList();
int getFunctionID(Function *F);
void setFunctionID(Function *F, unsigned Num);

//Remove function name parameters
static std::string moveFuncNamePara(std::string FuncName) {
    std::size_t pos = FuncName.find("(");
    if(pos == std::string::npos)
        return FuncName;
    if(pos == 0)
        return FuncName;
    return FuncName.substr(0, pos);
}

//get function demangling name
static std::string getDemangleName(const Function *Fn) {
    auto NameRef = Fn->getName();
    if (!NameRef.startswith("_Z"))
      return NameRef;

    if (Fn->hasFnAttribute("fpga.demangled.name"))
      return Fn->getFnAttribute("fpga.demangled.name").getValueAsString();

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

static Value *getArrayBasePointer(Value *Ptr) {
  if (!Ptr)
      return 0;

  if (isa<AllocaInst>(Ptr) || isa<GlobalVariable>(Ptr))
      return Ptr;
  if (isa<Argument>(Ptr))
      return Ptr;

  if (CastInst *Cast = dyn_cast<CastInst>(Ptr))
      return getArrayBasePointer(Cast->getOperand(0));

  if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Ptr))
      return getArrayBasePointer(GEP->getOperand(0));

  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(Ptr)) {
      if (CE->getOpcode() == Instruction::GetElementPtr)
          return getArrayBasePointer(CE->getOperand(0));
      if (CE->isCast())
          return getArrayBasePointer(CE->getOperand(0));
  }

  return 0;
}

/// Determine whether this alloca is either a VLA or an array.
static bool isArray(AllocaInst *AI) {
  return AI->isArrayAllocation() ||
    AI->getType()->getElementType()->isArrayTy();
}

template<>
struct DOTGraphTraits<HierGraphFunction*> : public DefaultDOTGraphTraits {

  DOTGraphTraits (bool isSimple=false) : DefaultDOTGraphTraits(isSimple) {}

  static std::string getGraphName(HierGraphFunction *HG) {
    //return "CFG for '" + HG->F->getName().str() + "' function";
    return "CFG for '" + moveFuncNamePara(getDemangleName(HG->F)) + "' function";
  }

  static std::string getSimpleNodeLabel(BasicBlock *Node,
                                        HierGraphFunction *) {
    if (!Node->getName().empty())
      return Node->getName().str();

    std::string Str;
    raw_string_ostream OS(Str);

    Node->printAsOperand(OS, false);
    return OS.str();
  }

  static std::string getCompleteNodeLabel(BasicBlock *Node, 
                                          HierGraphFunction *) {
    std::string Str;
    raw_string_ostream OS(Str);

    if (Node->getName().empty()) {
      Node->printAsOperand(OS, false);
      OS << ":";
    }

    OS << *Node;
    std::string OutStr = OS.str();
    if (OutStr[0] == '\n') OutStr.erase(OutStr.begin());

    // Process string output to make it nicer...
    for (unsigned i = 0; i != OutStr.length(); ++i)
      if (OutStr[i] == '\n') {                            // Left justify
        OutStr[i] = '\\';
        OutStr.insert(OutStr.begin()+i+1, 'l');
      } else if (OutStr[i] == ';') {                      // Delete comments!
        unsigned Idx = OutStr.find('\n', i+1);            // Find end of line
        OutStr.erase(OutStr.begin()+i, OutStr.begin()+Idx);
        --i;
      }

    return OutStr;
  }

  std::string getNodeLabel(BasicBlock *Node,
                           HierGraphFunction *Graph) {
    if (isSimple())
      return getSimpleNodeLabel(Node, Graph);
    else
      return getCompleteNodeLabel(Node, Graph);
  }

  static int getLineNumber(Instruction *I) {
    if (I == NULL) return -1;
    int line = -1;
    if (I->getDebugLoc()) {
      line = I->getDebugLoc().getLine();
    }
    return line;
  }

  static std::string getFileName(BasicBlock* B) {
    std::string filename = "";
    Instruction* I = B->getFirstNonPHIOrDbg();
    while (I && !I->getDebugLoc()) {
      I = I->getNextNode();
    }
    if (I && I->getDebugLoc()) {
      DIScope *scope = dyn_cast<DIScope>(I->getDebugLoc().getScope());
      if (scope) {
        auto *DFile = scope->getFile();
        filename = DFile->getFilename();
      }
    }
    return filename;
  }

  std::string getNodeAttributes(BasicBlock* Node,
                                HierGraphFunction* Graph) {
    
    std::string result = "filename=\"";
    result += getFileName(Node);
    result += "\",";

    // collect line number
    std::set<unsigned> lineNumbers;
    for (auto &I : *Node) {
      int number = getLineNumber(&I);
      if (number >= 0) lineNumbers.insert(number);
    }

    //print line number
    result += "linenumber=\"";
    for (auto num : lineNumbers) {
      result += std::to_string(num);
      result += ",";
    }
    // remove the last comma
    if (result[result.size()-1] == ',')
      result[result.size()-1] = '"';
    else
      result += "\"";

    if (DumpAllBB && !isNodeHidden(Node, Graph)) {
      SetVector<CallInst *> CIs;
      getCIs(Node, CIs);
      result = result + " callList=\"" + getCallList(CIs) + "\"";
      std::vector<std::pair<Instruction *, std::string>> MemReads;
      std::vector<std::pair<Instruction *, std::string>> MemWrites;
      getMemOPs(Node, MemReads, MemWrites);
      result = result + " memoryops=\"" + getMemOpList(MemReads, false) 
                      + " " + getMemOpList(MemWrites, true) + "\"";
      uint64_t exeNum = getNodeExeNum(Node, Graph);
      result = result + " execusionnum=\"" + std::to_string(exeNum) + "\""; 
    }
    return result;
  }


  uint64_t getNodeExeNum(BasicBlock* BB,
                                HierGraphFunction* Graph) {
    if (!BB) return 0;
    if (Graph->BBExeNum.count(BB) == 0)
      return 0;
    return Graph->BBExeNum[BB];
  }
 
  static std::string getEdgeSourceLabel(BasicBlock *Node,
                                        succ_iterator I) {
     // Label source of conditional branches with "T" or "F"
    if (const BranchInst *BI = dyn_cast<BranchInst>(Node->getTerminator()))
      if (BI->isConditional())
        return (I == succ_begin(Node)) ? "T" : "F";

    // Label source of switch edges with the associated value.
    if (const SwitchInst *SI = dyn_cast<SwitchInst>(Node->getTerminator())) {
      unsigned SuccNo = I.getSuccessorIndex();

      if (SuccNo == 0) return "def";

      std::string Str;
      raw_string_ostream OS(Str);
      auto Case = *SwitchInst::ConstCaseIt::fromSuccessorIndex(SI, SuccNo);
      OS << Case.getCaseValue()->getValue();
      return OS.str();
    }
    return "";
  }

  bool isDgbCall(Function* F) {
    assert(F != 0);
    static const std::string LLVMDBG_PREFIX = "llvm.dbg";
    std::string name = getDemangleName(F);
    if (name.find(LLVMDBG_PREFIX) == 0)
      return true;
    return false;
  }

  bool getCIs(BasicBlock *BB, SetVector<CallInst *> &CIs) {
    bool Changed = false;
    for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I) {
      CallInst *CI = dyn_cast<CallInst>(I);
      if (!CI)
        continue;
      Function *Callee = CI->getCalledFunction();
      if (!Callee) 
        continue;
      if (Callee->isDeclaration() || isDgbCall(Callee)) 
        continue;
      if(llvm::isCtorOrDtor(Callee->getName()))
        continue;
      if (!llvm::isFunctionDefinedByUser(Callee)) 
        continue;
      if (CIs.count(CI)) 
        continue;
   
      CIs.insert(CI);
      Changed = true;
    }
    return Changed;
  }

  std::string getCallName(CallInst *CI) {
    std::string CallName = "";
    if (Function *Callee = CI->getCalledFunction()) {
      CallName = Callee->getName();
      int FID = getFunctionID(Callee);
      if(FID > 0)
        CallName = "" + std::to_string(FID);
      int LineNumber = getLineNumber(CI);
      if (LineNumber != -1) {
        CallName = CallName + ":" + std::to_string(LineNumber);
      }
    };
    return CallName;
  }

  std::string getCallList(SetVector<CallInst *> &CIs) {
    if(CIs.size() <= 0)
      return "";
    std::string CallList = getCallName(CIs[0]);
    for (unsigned i = 1, e = CIs.size(); i != e; ++i)
      CallList = CallList + "; " + getCallName(CIs[i]);
    return CallList;
  }

  bool shouldShowMemory(Value *Base) {
    if(AllocaInst *AI = dyn_cast<AllocaInst>(Base)) {
      return isArray(AI);
    }
    if (GlobalValue *GV = dyn_cast<GlobalValue>(Base))
      return GV->getValueType()->isArrayTy();

    if (isa<ArrayType>(Base->getType()))
      return true;

    if (auto *PTy = dyn_cast<PointerType>(Base->getType()))
      return isa<ArrayType>(PTy->getElementType());

    return false;
  }

  // getMemOPs: get memop pair list from BasicBlock, one pair item contains instruction pointer and array name,
  // avoid to find array name again.
  void getMemOPs(BasicBlock *BB, 
    std::vector<std::pair<Instruction *, std::string>> &MemReads, 
    std::vector<std::pair<Instruction *, std::string>> &MemWrites) {
    for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I) {
      if(LoadInst *LI = dyn_cast<LoadInst>(I)) {
        Value *BaseAddr = LI->getPointerOperand();
        Value *Base = getArrayBasePointer(BaseAddr);
        if(Base && shouldShowMemory(Base))
          MemReads.push_back({LI, Base->getName()});//pair<float, char*>(3.0f, "pear")
      } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
        Value *BaseAddr = SI->getPointerOperand();
        Value *Base = getArrayBasePointer(BaseAddr);
        if(Base && shouldShowMemory(Base))
          MemWrites.push_back({SI, Base->getName()});
      }
    }
    return ;
  }

  std::string getMemOpName(const std::pair<Instruction *, std::string> &MemOp, bool IsWrite) {
    std::string MemOpName = "";
    std::string Opname = IsWrite?"_write":"_read";
    std::string ArrayName = MemOp.second;
    Instruction * I = MemOp.first;
    if(!ArrayName.empty()) {
      MemOpName = ArrayName + Opname;
      int LineNumber = getLineNumber(I);
      if (LineNumber > -1) {
        MemOpName = MemOpName + ":" + std::to_string(LineNumber);
      }
     }
    return MemOpName;
  }

  std::string getMemOpList(std::vector<std::pair<Instruction *, std::string>> &MemOps, bool IsWrite) {
    if(MemOps.size() <= 0)
      return "";
    std::string MemOpList = getMemOpName(MemOps[0], IsWrite);
    for (unsigned i = 1, e = MemOps.size(); i != e; ++i)
      MemOpList = MemOpList + "; " + getMemOpName(MemOps[i], IsWrite);
    return MemOpList;
  }

  /// Display the raw branch weights from PGO.
  std::string getEdgeAttributes(BasicBlock *Node, succ_iterator I,
                                HierGraphFunction *G) {
    TerminatorInst *TI = Node->getTerminator();
    if (TI->getNumSuccessors() == 1)
      return "";

    MDNode *WeightsNode = TI->getMetadata(LLVMContext::MD_prof);
    if (!WeightsNode)
      return "";

    MDString *MDName = cast<MDString>(WeightsNode->getOperand(0));
    if (MDName->getString() != "branch_weights")
      return "";

    unsigned OpNo = I.getSuccessorIndex() + 1;
    if (OpNo >= WeightsNode->getNumOperands())
      return "";
    ConstantInt *Weight =
        mdconst::dyn_extract<ConstantInt>(WeightsNode->getOperand(OpNo));
    if (!Weight)
      return "";

    // Prepend a 'W' to indicate that this is a weight rather than the actual
    // profile count (due to scaling).
    std::string Attrs = "label=\"W:" + Twine(Weight->getZExtValue()).str() + "\"";
    return Attrs;
  }


  //////////////////////////////////
  ////  HLS customize:
  //////////////////////////////////

  //Print loop as subgraph. index and prefix are used as cluster names
  //such as cluster_0, cluster_0_0
  static void printLoopCluster(Loop *L, GraphHierWriter<HierGraphFunction *> &GW,
                               HierGraphFunction *HG,
                               unsigned index, std::string prefix,
                               std::set<BasicBlock*> &visited) {
    //cluster name
    std::stringstream ss;
    ss << index;
    prefix += "_" + ss.str();

    //Header of current loop
    writeSubGraphHeader(GW, L, prefix);

    //dump for subloops
    std::vector<Loop*> SubLoops(L->begin(), L->end());
    unsigned sub_count = 0;
    for (unsigned i = 0, e = SubLoops.size(); i != e; ++i) {
        printLoopCluster(SubLoops[i], GW, HG, sub_count, prefix,visited);
        ++sub_count;
    }

    //print each node (BB)
    for (auto BI = L->block_begin(); BI != L->block_end(); BI++) {
        BasicBlock* BB = *BI;
        if(visited.count(BB) == 0) {
            if(!isNodeHidden(BB, HG)) GW.writeNode(BB, HG);
            visited.insert(BB);
        }
    }

    //End of current subgraph
    writeSubGraphFooter(GW);
    return;

  }

  static void addCustomGraphFeatures(HierGraphFunction *HG, GraphHierWriter<HierGraphFunction *> &GW) {
    unsigned count = 0;
    for (LoopInfo::iterator I = HG->LI->begin();  I != HG->LI->end(); ++I) {
        Loop* L = *I;
        std::string prefix = "cluster";
        std::set<BasicBlock*> visited;
        printLoopCluster(L, GW, HG, count, prefix, visited);
        ++count;
    }
  }

//  //if BB has single in single out, return true
//  static bool isSISO(BasicBlock *BB, HierGraphFunction *HG) {
//    if(!BB) return false;
//    //Entry Block
//    if(&(BB->getParent()->getEntryBlock()) == BB) return false;
//
//    pred_iterator PI = pred_begin(BB), PE = pred_end(BB);
//    succ_iterator SI = succ_begin(BB), SE = succ_end(BB);
//    if(PI == PE || SI == SE)
//        return false;
//    else if(++PI == PE && ++SI == SE)
//        return true;
//    else
//        return false;
//  }

  //BB Hidden or not
  static bool isNodeHidden(BasicBlock *Node, HierGraphFunction *HG) {
    if(HG->HiddenBBs.count(Node) > 0) 
        return true;
    else
        return false;
  }

  static unsigned calculateTripCount(Loop *L) {
    unsigned tc = 0;
    if (!L) return tc;
    BasicBlock *ExitingBlock = L->getLoopLatch();
    if (!ExitingBlock || !L->isLoopExiting(ExitingBlock))
      ExitingBlock = L->getExitingBlock();
    if (!ExitingBlock) return tc; 

    TerminatorInst *TI = ExitingBlock->getTerminator();
    MDNode *WeightsNode = TI->getMetadata(LLVMContext::MD_prof);
    if (!WeightsNode) return tc;

    MDString *MDName = cast<MDString>(WeightsNode->getOperand(0));
    if (MDName->getString() != "branch_weights") return tc;

    if (WeightsNode->getNumOperands() >= 2) {
      ConstantInt *Weight =
        mdconst::dyn_extract<ConstantInt>(WeightsNode->getOperand(1));
      if (!Weight) return tc;

      tc = Weight->getZExtValue() - 1;
    }
    return tc;
  }

  static int getOpNumber(BasicBlock *CurBB, BasicBlock *SuccBB) {
      if(!CurBB || !SuccBB) return -1;
      succ_iterator SI = succ_begin(CurBB), SE = succ_end(CurBB);
      int OpNum = 0;
      while(SI != SE) {
        if(*SI == SuccBB)
          return OpNum;
        OpNum++;
        SI++;
      }
      return -1;
  }

  static int getLoopExecuteCount(Function *F) {
    MDNode *WeightsNode = F->getMetadata(LLVMContext::MD_prof);
    if (!WeightsNode)
      return -1;
    MDString *MDName = cast<MDString>(WeightsNode->getOperand(0));
    if(!MDName)
      return -1;
    if (MDName->getString() != "function_entry_count")
      return -1;
    ConstantInt *Weight = mdconst::dyn_extract<ConstantInt>(WeightsNode->getOperand(1));
    if (!Weight)
      return -1;
    return Weight->getSExtValue();
  }

  static int getLoopExecuteCount(BasicBlock *CurBB, BasicBlock *SuccBB) {
    TerminatorInst *TI = CurBB->getTerminator();
    if (TI->getNumSuccessors() == 1)
      return -1;

    MDNode *WeightsNode = TI->getMetadata(LLVMContext::MD_prof);
    if (!WeightsNode)
      return -1;

    MDString *MDName = cast<MDString>(WeightsNode->getOperand(0));
    if (MDName->getString() != "branch_weights")
      return -1;

    unsigned OpNo = getOpNumber(CurBB, SuccBB) + 1;
    if (OpNo < 0 || OpNo >= WeightsNode->getNumOperands())
      return -1;
    ConstantInt *Weight =
        mdconst::dyn_extract<ConstantInt>(WeightsNode->getOperand(OpNo));
    if (!Weight)
      return -1;
    return Weight->getSExtValue();
  }

  static bool canSkipLoopAndTrackCount(BasicBlock *&PreBB, BasicBlock *&BB, LoopInfo *LI) {
    if(LI) {
      if(Loop *L = LI->getLoopFor(PreBB)) {
        BasicBlock *Predecessor = L->getLoopPredecessor();
        BasicBlock *Header = L->getHeader();
        if(Predecessor && Header && L->getLoopLatch() == PreBB) {
          PreBB = Predecessor;
          BB = Header;
          return true;
        }
      }
    }
    return false;
  }

  static int getLoopExecuteCount(Loop *L, GraphHierWriter<HierGraphFunction *> &GW) {
    BasicBlock *PredBB = L->getLoopPredecessor();
    BasicBlock *Header = L->getHeader();
    const HierGraphFunction *G = GW.getGraph();
    if(!G)
      return -1;
    LoopInfo *LI = G->LI;
    if(!PredBB || !Header)
      return -1;
    Function *F = PredBB->getParent();
    BasicBlock *EntryBlock = &F->getEntryBlock();
    BasicBlock *CurBB = Header;

    while(PredBB && PredBB != EntryBlock) {
      // PredBB is a conditional branch
      if(PredBB->getSingleSuccessor() == NULL) {
        int ret =  getLoopExecuteCount(PredBB, CurBB);
        // got invoke time of loop
        if(ret > 0)
          return ret;
        // can not get invoke time of loop, try to skip loop and continue tracking
        if(canSkipLoopAndTrackCount(PredBB, CurBB, LI))
          continue;
        else
          return ret;
      }
      CurBB = PredBB;
      PredBB = PredBB->getSinglePredecessor();
    }

    if(!PredBB) {
      return -1;
    } else if(PredBB == EntryBlock) {
      return getLoopExecuteCount(F);
    } else if(PredBB->getSingleSuccessor() == NULL) {
      return getLoopExecuteCount(PredBB, CurBB);
    }
    return -1;
  }

  static StringRef getLoopName(Loop *L) {
    assert(L && "L should not be NULL");
    if (BasicBlock *PreHeader = L->getLoopPreheader())
      if (PreHeader->hasName())
        return PreHeader->getName();

    if (BasicBlock *Header = L->getHeader())
      if (Header->hasName())
        return Header->getName();
    return StringRef("", 0);
  }

  static void writeSubGraphHeader(GraphHierWriter<HierGraphFunction *> &GW, Loop *L, std::string prefix) {
    raw_ostream &O = GW.getOStream();
    std::string Title = "Loop_" + prefix;
    std::string LoopName = getLoopName(L);
    if(!LoopName.empty())
      Title = LoopName;

    if (!prefix.empty())
      O << "subgraph " << DOT::EscapeString(prefix) << " {\n";
    else
      O << "subgraph unnamed {\n";

    if (!Title.empty())
      O << "\tlabel=\"" << DOT::EscapeString(Title) << "\";\n";
      int tripcount = calculateTripCount(L);
      O << "\ttripcount=\"" << std::to_string(tripcount) << "\";\n";

      int Invocationtime = getLoopExecuteCount(L, GW);
      O << "\tinvocationtime=\"" << std::to_string(Invocationtime) << "\";\n";

    //O << "\t\tstyle=filled;\n";
    //O << "\t\tcolor=lightgrey;\n";

    O << "\n";
    return;
  }

  static void writeSubGraphFooter(GraphHierWriter<HierGraphFunction *> &GW) {
    raw_ostream &O = GW.getOStream();
    // Finish off the subgraph
    O << "}\n";
  }



};
} // End llvm namespace

namespace llvm {
  class FunctionPass;
  FunctionPass *createCFGHierOnlyPrinterLegacyPassPass ();
} // End llvm namespace

#endif
