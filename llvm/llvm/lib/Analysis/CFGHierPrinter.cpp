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
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/CFGHierPrinter.h"
#include "llvm/Pass.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"

#include <sstream>

using namespace llvm;

cl::opt<std::string>
    CFGHierType("cfg-hier-type", cl::Hidden,
        cl::desc("The option to specify "
          "the type of the graph "
          "either \"csim\" or \"csynth\"."));

cl::opt<bool>
    DumpAllBB("dump-all-bb", cl::desc("dump all the basic block"),
              cl::Hidden, cl::init(false));

cl::opt<std::string>
    CFGHierUserfilelist("cfg-hier-userfilelist", cl::Hidden,
        cl::desc("The option to specify the list of user file, "
          "check if function is defined by user"));

namespace {
  struct CFGHierOnlyPrinterLegacyPass : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    static bool isUserfileListInitialized;
    static std::vector<std::string> UserfileList;
    CFGHierOnlyPrinterLegacyPass() : FunctionPass(ID) {
      initializeCFGHierOnlyPrinterLegacyPassPass(*PassRegistry::getPassRegistry());
    }
    
    virtual bool runOnFunction(Function &F) {
      //std::string Filename = "cfg." + F.getName().str() + ".dot";
      //std::string Filename = "cfg_" + moveFuncNamePara(getDemangleName(&F)) + "_csim.dot";
      //std::string Filename = "cfg_" + moveFuncNamePara(getDemangleName(&F)) + "_" + CFGHierType + ".dot";
      std::string Filename = "cfg_" + F.getName().str() + "_" + CFGHierType + ".dot";
      int FID = getFunctionID(&F);
      if(FID > 0)
        Filename = "cfg_" + std::to_string(FID) + "_" + CFGHierType + ".dot";
      errs() << "Writing '" << Filename << "'...";

      CFGHierOnlyPrinterLegacyPass::initializeUserfileList();

      // Preprocess on BBs, insert new BB only with br instruction
      splitNonSISOBBs(&F);

      std::error_code EC;
      raw_fd_ostream File(Filename, EC, sys::fs::F_Text);

      // Initilization for necessary variables for CFG dump
      HierGraphFunction HG;
      initHierGraphFunction(&F, &HG);

      if (!EC)
        WriteHierGraph(File, &HG, true);
      else
        errs() << "  error opening file for writing!";
      errs() << "\n";
      return false;
    }
    void print(raw_ostream &OS, const Module* = 0) const {}

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesAll();
      AU.addRequired<llvm::LoopInfoWrapperPass>();
    }

    static std::string getFullPathFileName(Function* F) {
      std::string res = "";
      if (DISubprogram *SP = F->getSubprogram()) {
        if (DIFile * SourceFile = SP->getFile()) {
          std::string FileName = SourceFile->getFilename();
          if(!FileName.empty()) {
            if(!llvm::sys::path::is_absolute(FileName)) {
              std::string DirName = SourceFile->getDirectory();
              FileName = DirName + "/" + FileName;
            }
            res = getRealPath(FileName);
          }
        }
      }
      return res;
    }
    
    // boost::filesystem::canonical() and std::filesystem::canonical()
    // are the safe alternative to realpath
    static std::string getRealPath(std::string &FileName) {
      std::string Res = "";
#if defined(_WIN32) || defined(_WIN64)
      Res = FileName;
#else
      char RealPath[PATH_MAX + 1];
      RealPath[PATH_MAX] = 0;
      if (::realpath(FileName.c_str(), RealPath))
        Res = RealPath;
#endif
      return Res;
    }

    static bool splitString(std::string &str, std::vector<std::string> &result, char delimiter) {
      bool Changed = false;
      std::stringstream ss(str); // Turn the string into a stream.
      std::string tok;
      while(getline(ss, tok, delimiter)) {
        result.push_back(tok);
        Changed = true;
      }
      return Changed;
    }

    static void initializeUserfileList() {
      if(isUserfileListInitialized)
        return ;
      UserfileList.clear();
      isUserfileListInitialized = true;
      if(CFGHierUserfilelist.empty())
        return ;
    
      std::vector<std::string> OldUserFileList;
      splitString(CFGHierUserfilelist , OldUserFileList, ' ');
      for (unsigned i = 0; i < OldUserFileList.size(); ++i) {
        std::string FileName = getRealPath(OldUserFileList[i]);
        if(!FileName.empty())
          UserfileList.push_back(FileName);
      }
      return ;
    }
    
    static bool isFunctionDefinedByUser(Function *F) {
      std::string FileName = getFullPathFileName(F);
      return std::find(UserfileList.begin(), UserfileList.end(), FileName) != UserfileList.end();
    }

    bool isSISOBB(BasicBlock *BB) {
        if(!BB) return false;
        pred_iterator PI = pred_begin(BB), PE = pred_end(BB);
        succ_iterator SI = succ_begin(BB), SE = succ_end(BB);
        if(PI == PE || SI == SE)
            return false;
        else if(++PI == PE && ++SI == SE)
            return true;
        else
            return false;
    }

    bool isSIMOBB(BasicBlock *BB) {
        if(!BB) return false;
        pred_iterator PI = pred_begin(BB), PE = pred_end(BB);
        succ_iterator SI = succ_begin(BB), SE = succ_end(BB);
        if(PI == PE || SI == SE)
            return false;
        else if(++PI == PE && ++SI != SE)
            return true;
        else
            return false;
    }

    bool isMISOBB(BasicBlock *BB) {
        if(!BB) return false;
        pred_iterator PI = pred_begin(BB), PE = pred_end(BB);
        succ_iterator SI = succ_begin(BB), SE = succ_end(BB);
        if(PI == PE || SI == SE)
            return false;
        else if(++PI != PE && ++SI == SE)
            return true;
        else
            return false;
    }

    bool isMIMOBB(BasicBlock *BB) {
        if(!BB) return false;
        pred_iterator PI = pred_begin(BB), PE = pred_end(BB);
        succ_iterator SI = succ_begin(BB), SE = succ_end(BB);
        if(PI == PE || SI == SE)
            return false;
        else if(++PI != PE && ++SI != SE)
            return true;
        else
            return false;
    }

    bool isMultiPred(BasicBlock *BB) {
      if(!BB) return false;
      pred_iterator PI = pred_begin(BB), PE = pred_end(BB);
      if(PI == PE)
        return false;
      if(++PI != PE)
        return true;
      return false;
    }

    bool isMultiSucc(BasicBlock *BB) {
      if(!BB) return false;
      succ_iterator SI = succ_begin(BB), SE = succ_end(BB);
      if(SI == SE)
        return false;
      if(++SI != SE)
        return true;
      return false;
    }

    bool shouldSkip(BasicBlock *BB) {
      if(!BB)
        return true;
      if(BB->getTerminator() == nullptr)
        return true;
      // TODO: Added check if there call and mem options in BB
      return false;
    }

    bool shouldAddPredFake(BasicBlock *BB) {
      if(!BB)
        return false;
      if(isMultiPred(BB))
        return true;
      if(&BB->getParent()->getEntryBlock() == BB)
        return true;
      return false;
    }

    bool shouldAddSuccFake(BasicBlock *BB) {
      if(!BB)
        return false;
      if(isMultiSucc(BB))
        return true;
      if(isa<ReturnInst>(BB->getTerminator()))
        return true;
      return false;
    }

    BasicBlock *addPredFake(BasicBlock *BB, DominatorTree *DT, LoopInfo *LI) {
      std::string OldName = BB->getName();
      BB->setName(OldName + ".predFake");
      BasicBlock *NewBB = SplitBlock(BB, &*(BB->begin()), DT, LI);
      NewBB->setName(OldName);
      movePHIs(NewBB, BB);
      return NewBB;
    }

    BasicBlock *addSuccFake(BasicBlock *BB, DominatorTree *DT, LoopInfo *LI) {
      std::string OldName = BB->getName();
      BasicBlock *NewBB = SplitBlock(BB, BB->getTerminator(), DT, LI);
      NewBB->setName(OldName + ".succFake");
      return NewBB;
    }

    void movePHIs(BasicBlock *From, BasicBlock *To) {
      for (PHINode &PN : From->phis())
        PN.moveBefore(To->getTerminator());
    }

    void resolveLoopBBs(Loop* L, std::set<BasicBlock*> &visited) {
        if(!L) return;
        // Here not set Loop Latch NOT SISO as hidden
        BasicBlock *latch = L->getLoopLatch();
        if(latch && !isSISOBB(latch)) visited.insert(latch);
    }
        

    bool initHierGraphFunction(Function *F, HierGraphFunction *HG) {
        HG->F = F;
        HG->LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
        HG->TopBBs.clear();

        //1. Record BBs outside of each loop, for sub graph as hier-CFG
        std::set<BasicBlock*> visited;
        for (LoopInfo::iterator I = HG->LI->begin(), E = HG->LI->end(); I != E; ++I) {
            Loop* L = *I;
            for (auto BI = L->block_begin(); BI != L->block_end(); BI++) {
                BasicBlock* BB = *BI;
                visited.insert(BB);
            }
        }
        for (Function::iterator BI = F->begin(); BI != F->end(); ++BI) {
            BasicBlock* BB = &(*BI);
            if (visited.count(BB) == 0) {
                HG->TopBBs.push_back(BB);
            }
        }

        //2. Record BBs as hidden
        visited.clear();
        visited.insert(&(F->getEntryBlock())); //entry block
        for (LoopInfo::iterator I = HG->LI->begin(), E = HG->LI->end(); I != E; ++I) {
            //Latch block
            Loop* L = *I;
            resolveLoopBBs(L, visited);
            std::vector<Loop*> SubLoops(L->begin(), L->end());
            for (unsigned i = 0, e = SubLoops.size(); i != e; ++i) {
                resolveLoopBBs(SubLoops[i], visited);
            }
        }
        for (Function::iterator BI = F->begin(); BI != F->end(); ++BI) {
            BasicBlock* BB = &(*BI);
            if (visited.count(BB) == 0) {
                //1. not entry block
                //2. not loop latch, prehead block
                //3. SISO block
                if(!DumpAllBB && isSISOBB(BB)) {
                    HG->HiddenBBs.insert(BB);
                }
                visited.insert(BB);
            }
        }

        //3. Record execution time for each BB
        HG->BBExeNum.clear();
        collectBBExeInfo(F, HG->BBExeNum);
        return true;
    }


    /// splitNonSISOBBs - Splite non-SISO BBs to move most inst to hidden BBs.
    /// 1. collect NonSISOBBs
    /// 2. spilt NonSISOBBs: clear implementation to add fake node.
    /// 2.1 check if we should skip this BB, if yes, skip.
    /// the conditions for skipping node:
    /// (a) BB is null
    /// (b) Terminator is null
    /// (c) TODO: Added check if there call and memop in BB
    /// 2.2 check if we should add pred fake node for this BB, if yes, add pred fake node.
    /// the conditions for adding pred fake node:
    /// (a) entry block
    /// (b) have multipred
    /// 2.3 check if we should add succ fake node for this BB, if yes, add succ fake node.
    /// the conditions for adding succ fake node:
    /// (a) return block
    /// (b) have multisucc
    bool splitNonSISOBBs(Function *F) {
      LoopInfo *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
      DominatorTree *DT = NULL;
      bool Changed = false;

      // 1. collect NonSISOBBs
      std::set<BasicBlock*> NonSISOBBs;
      for (Function::iterator BI = F->begin(); BI != F->end(); ++BI) {
          BasicBlock* BB = &(*BI);
          if(BB && !isSISOBB(BB))
            NonSISOBBs.insert(BB);
      }

      // 2. spilt NonSISOBBs
      for (BasicBlock *BB : NonSISOBBs) {
        // 2.1 check if we should skip this BB
        if(shouldSkip(BB))
          continue;
        BasicBlock *TmpBB = BB;
        // 2.2 check if we should add pred fake node for this BB
        if(shouldAddPredFake(TmpBB)) {
          TmpBB = addPredFake(TmpBB, DT, LI);
          Changed = true;
        }

        // 2.3 check if we should add succ fake node for this BB
        if(shouldAddSuccFake(TmpBB)){
          TmpBB = addSuccFake(TmpBB, DT, LI);
          Changed = true;
        }
      }
      return Changed;
    }


    /// collectBBExeInfo - collect execusion times for each BB
    /// 1. collect BBs, from Entry, with BFS order
    /// 2. For each BB
    /// (a) If entry BB, set value directly
    /// (b) Add the values of all preBBs OR succBBs
    /// (c) If cannot caculate, caculate it next time
    void collectBBExeInfo(Function *F, std::map<BasicBlock*, uint64_t> &BBExeNum) {
      uint64_t FuncExeNum = getFuncExeNum(F);

      // 1. collect BBs under BFS order
      std::vector<BasicBlock *> BBList;
      BasicBlock *entry = &(F->getEntryBlock());
      scanBFS(entry, BBList);

      // 2. For each BB, record its execution times
      // (2.1) First iteration
      std::vector<BasicBlock *> missing;
      for (unsigned i = 0; i < BBList.size(); ++i) {
        BasicBlock *BB = BBList[i];
        if (!BB) 
          continue;
        // (a) entry BB, execution number is directly function number
        if (entry == BB) {
          BBExeNum[BB] = FuncExeNum;
          #ifdef Debug_Dump_Profiling
            setExeNumMetadata(entry, FuncExeNum);
          #endif
          continue;
        }
        // (b) others depend on pre BB profiling values
        else {
          bool changed = setExeNumber(BB, BBExeNum);
          if (!changed) missing.push_back(BB);
          continue;
        }
      }

      // (2.2) multi iterations only for missing ones: 
      bool changed = !(missing.empty());
      while (changed) {
        changed = false;
        for (unsigned i = 0; i < missing.size(); ++i) {
          BasicBlock *BB = missing[i];
          if (BBExeNum.count(BB) > 0) 
            continue;
          changed = setExeNumber(BB, BBExeNum);
        }
      }

      #ifdef Debug_Dump_Profiling
      // 3. only for debug
        printBBExeNum(BBList, BBExeNum);
      #endif
    }


    // collect BBs under BFS order
    void scanBFS(BasicBlock *Start, std::vector<BasicBlock *> &BBList) {
      std::set<BasicBlock *> visited;
      std::vector<BasicBlock *> temp;
      // start from Entry BB
      if (!Start) return;
      temp.push_back(Start);

      while (!temp.empty()) {
        BasicBlock *current = temp[0];
        temp.erase(temp.begin());
        if (visited.insert(current).second) {
          BBList.push_back(current);
          for (succ_iterator SI = succ_begin(current), E = succ_end(current); SI != E; ++SI) {
            BasicBlock *next = *SI;
            if (next)
              temp.push_back(next);
          }
        }
      }
      return;
    }


    uint64_t getProfileMetadata(BasicBlock *currentBB, unsigned OperandID) {
      if (!hasProfileMetadata(currentBB))
        return 0;
      Instruction *terminator = currentBB->getTerminator();
      if (!terminator)
        return 0;
      if (OperandID == 0)
        return 0;

      // return the corresponding metadata value
      MDNode *ProfileData = terminator->getMetadata(LLVMContext::MD_prof);
      if (ProfileData) {
        uint64_t result = mdconst::extract<ConstantInt>(ProfileData->getOperand(OperandID))->getZExtValue();
        return result;
      }
      return 0;
    }

    uint64_t getFuncExeNum(Function *F) {
      if(!F) return 0;
      MDNode *ProfileData = F->getMetadata(LLVMContext::MD_prof);
      if (ProfileData) {
        uint64_t result = mdconst::extract<ConstantInt>(ProfileData->getOperand(1))->getZExtValue();
        return result;
      }
      return 0;
    }

    // return the ith succBB id
    // -1 is the invalid value; 0 is only for unconditional br
    int getSuccID(BasicBlock *currentBB, BasicBlock *succBB) {
      if (!currentBB || !succBB) return -1;
      Instruction *terminator = currentBB->getTerminator();
      if (BranchInst *br = dyn_cast<BranchInst>(terminator)) {
        // (1) br directly, so 0th
        if (br->isUnconditional()) {
          return 0;
        }

        // (2) condition br, find same 
        for (unsigned i = 0; i < br->getNumSuccessors(); ++i) {
          if (br->getSuccessor(i) == succBB) {
            // (i + 1) because profiling metadata int value starts from 1
            return (i + 1);
          }
        }
      }

      else if (SwitchInst *sw = dyn_cast<SwitchInst>(terminator)) {
        // (3) switch, find same succ bb
        for (unsigned i = 0; i < sw->getNumSuccessors(); ++i) {
          if (sw->getSuccessor(i) == succBB) {
            // (i + 1) because profiling metadata int value starts from 1
            return (i + 1);
          }
        }
      }

      else {
        // not br or switch, like ret
        return -1;
      }
      return -1;
     }

   
    uint64_t getProfileWeight(BasicBlock *currentBB, BasicBlock *succBB) {
      if (hasProfileMetadata(currentBB) == false)
        return 0;
      int ID = getSuccID(currentBB, succBB);
      if (ID <= 0)
        return 0;
      return getProfileMetadata(currentBB, ID);
    }

    bool setExeNumber(BasicBlock *BB, std::map<BasicBlock*, uint64_t> &BBExeNum) {
      // Try to obtain value from pre OR succ BB
      if (setExeNumber_Forward(BB, BBExeNum))
        return true;
      else if (setExeNumber_Backward(BB, BBExeNum))
        return true;
      else
        return false;
    }

    // execution times: add from multi pre BB
    bool setExeNumber_Forward(BasicBlock *BB, std::map<BasicBlock*, uint64_t> &BBExeNum) {
      uint64_t result = 0;
      for (pred_iterator PI = pred_begin(BB), E = pred_end(BB); PI != E; ++PI) {
        BasicBlock *preBB = *PI;
        if (!preBB) 
          continue;
        if (hasProfileMetadata(preBB)) {
          // If profiling info exists in pre BB, use it directly
          result = result + (getProfileWeight(preBB, BB) - 1);
          continue;
        }
        else if (BBExeNum.count(preBB) > 0) {
          // pre BB has no profiling info, but we have caculated its exe number.
          result = result + BBExeNum[preBB];
          continue;
        }
        else {
          // pre BB has no profiling info, and also no caculated exe number
          // then do nothing: this BB will be caculated next time
          return false;
        }
      }

      // Caculation is done, record it
      BBExeNum[BB] = result;
      #ifdef Debug_Dump_Profiling
        setExeNumMetadata(BB, result);
      #endif
      return true;
    }

    // execution times: add from multi succ BB
    bool setExeNumber_Backward(BasicBlock *BB, std::map<BasicBlock*, uint64_t> &BBExeNum) {
      uint64_t result = 0;
      for (succ_iterator SI = succ_begin(BB), E = succ_end(BB); SI != E; ++SI) {
        BasicBlock *succBB = *SI;
        if (hasProfileMetadata(BB)) {
          // If profiling info exists in current BB, use it directly
          result = result + (getProfileWeight(BB, succBB) - 1);
          continue;
        }
        else if (BBExeNum.count(succBB) > 0) {
          // pre BB has no profiling info, but we have caculated its exe number.
          result = result + BBExeNum[succBB];
          continue;
        }
        else {
          // succ BB has no profiling info, and also no caculated exe number
          // then do nothing: this BB will be caculated next time
          return false;
        }
      }

      // Caculation is done, record it
      BBExeNum[BB] = result;
      #ifdef Debug_Dump_Profiling
        setExeNumMetadata(BB, result);
      #endif
      return true;
    }


    // If the terminator instruction contains profiling info, return true
    bool hasProfileMetadata(BasicBlock *BB) {
      if (!BB)
        return false;
      if (Instruction *terminator = BB->getTerminator()) {
        MDNode *MD = terminator->getMetadata("prof");
        if (MD)
          return true;
      }
      return false;
    }

  
    // set execution number to the BB terminator instruction
    void setExeNumMetadata(BasicBlock *BB, uint64_t value) {
      if (!BB)
        return;
      auto &Ctx = BB->getParent()->getContext();
      Instruction *terminator = BB->getTerminator();
      if (terminator) {
        terminator->setMetadata("exe_num", MDNode::get(Ctx, 
                 ConstantAsMetadata::get(ConstantInt::get(Type::getInt32Ty(Ctx), value))));
      }
      return;
    }

    // dump BBExeNum info to std output
    void printBBExeNum(std::vector<BasicBlock *> &BBList, 
               std::map<BasicBlock *, uint64_t> &BBExeNum) {
      std::vector<BasicBlock *> missing;
      llvm::errs()<<"Execution times for each BB:\n";
      llvm::errs()<<"===============================\n";
      for (unsigned i = 0; i < BBList.size(); ++i) {
        BasicBlock *BB = BBList[i];
        if (BBExeNum.count(BB) > 0) {
          llvm::errs()<<"BB:"<<BB->getName()<<"\t";
          llvm::errs()<<BBExeNum[BB]<<"\n";
        }
        else
          missing.push_back(BB);
      }
      llvm::errs()<<"===============================\n";
      llvm::errs()<<"Missing BBs:\n";
      for (unsigned i = 0; i < missing.size(); ++i) 
        llvm::errs()<<missing[i]->getName()<<"\t";
      llvm::errs()<<"\n";
      llvm::errs()<<"===============================\n\n";
      return; 
    }


  };
}

bool CFGHierOnlyPrinterLegacyPass::isUserfileListInitialized = false;
std::vector<std::string> CFGHierOnlyPrinterLegacyPass::UserfileList;
char CFGHierOnlyPrinterLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(CFGHierOnlyPrinterLegacyPass, "dot-cfg-hier-only",
   "Print Hier CFG of function to 'dot' file (with no function bodies)",
   false, true)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(CFGHierOnlyPrinterLegacyPass, "dot-cfg-hier-only",
   "Print Hier CFG of function to 'dot' file (with no function bodies)",
   false, true)

FunctionPass *llvm::createCFGHierOnlyPrinterLegacyPassPass () {
  return new CFGHierOnlyPrinterLegacyPass();
}

bool llvm::isFunctionDefinedByUser(Function *F) {
  return CFGHierOnlyPrinterLegacyPass::isFunctionDefinedByUser(F);
}

void llvm::initializeUserfileList() {
  return CFGHierOnlyPrinterLegacyPass::initializeUserfileList();
}

