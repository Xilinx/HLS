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
// This file defines '-dot-div' analysis passes, which emit a div.<fnname>.dot
// file for each function in the program, with a graph of the CFG of that
// function where the basic blocks are colorized if detected as divergent.
//
// The '-dot-div-only' pass prints the names of the bbs but their content is
// hidden.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/DivergencePrinter.h"
#include "llvm/Analysis/DOTGraphTraitsPass.h"
#include "llvm/Analysis/DivergenceAnalysis.h"

using namespace llvm;

namespace llvm {

template <>
struct GraphTraits<DivergenceAnalysis *>
    : public GraphTraits<const Function *> {
  static NodeRef getEntryNode(DivergenceAnalysis *DA) {
    return &DA->getCurrentFunction()->getEntryBlock();
  }
  static nodes_iterator nodes_begin(DivergenceAnalysis *G) {
    return nodes_iterator(G->getCurrentFunction()->begin());
  }
  static nodes_iterator nodes_end(DivergenceAnalysis *G) {
    return nodes_iterator(G->getCurrentFunction()->end());
  }
  static size_t size(DivergenceAnalysis *G) {
    return G->getCurrentFunction()->size();
  }
};

template <>
struct DOTGraphTraits<DivergenceAnalysis *> : public DefaultDOTGraphTraits {
  DOTGraphTraits(bool isSimple = false) : DefaultDOTGraphTraits(isSimple) {}

  static std::string getGraphName(const DivergenceAnalysis *G) {
    return ("CFG of Function '" + G->getCurrentFunction()->getName() +
            "' with Divergence Info (purple is divergent)")
        .str();
  }

  std::string getNodeLabel(const BasicBlock *BB, DivergenceAnalysis *) {
    if (isSimple())
      return DOTGraphTraits<const Function *>::getSimpleNodeLabel(
          BB, BB->getParent());
    else
      return DOTGraphTraits<const Function *>::getCompleteNodeLabel(
          BB, BB->getParent());
  }

  std::string getNodeAttributes(const BasicBlock *Node, DivergenceAnalysis *G) {
    if (G->isControlDivergent(Node))
      return "color=purple";

    return "";
  }
};
}

namespace {
struct DivergenceViewer
    : public DOTGraphTraitsViewer<DivergenceAnalysis, false> {
  static char ID;
  DivergenceViewer()
      : DOTGraphTraitsViewer<DivergenceAnalysis, false>("div", ID) {
    initializeDivergenceViewerPass(*PassRegistry::getPassRegistry());
  }
};

struct DivergenceOnlyViewer
    : public DOTGraphTraitsViewer<DivergenceAnalysis, true> {
  static char ID;
  DivergenceOnlyViewer()
      : DOTGraphTraitsViewer<DivergenceAnalysis, true>("divonly", ID) {
    initializeDivergenceOnlyViewerPass(*PassRegistry::getPassRegistry());
  }
};
} // end anonymous namespace

char DivergenceViewer::ID = 0;
INITIALIZE_PASS(DivergenceViewer, "view-div",
                "View divergence info of function", false, false)

char DivergenceOnlyViewer::ID = 0;
INITIALIZE_PASS(DivergenceOnlyViewer, "view-div-only",
                "View divergence info of function (with no function bodies)",
                false, false)

namespace {
struct DivergencePrinter
    : public DOTGraphTraitsPrinter<DivergenceAnalysis, false> {
  static char ID;
  DivergencePrinter()
      : DOTGraphTraitsPrinter<DivergenceAnalysis, false>("div", ID) {
    initializeDivergencePrinterPass(*PassRegistry::getPassRegistry());
  }
};

struct DivergenceOnlyPrinter
    : public DOTGraphTraitsPrinter<DivergenceAnalysis, true> {
  static char ID;
  DivergenceOnlyPrinter()
      : DOTGraphTraitsPrinter<DivergenceAnalysis, true>("divonly", ID) {
    initializeDivergenceOnlyPrinterPass(*PassRegistry::getPassRegistry());
  }
};
} // end anonymous namespace

char DivergencePrinter::ID = 0;
INITIALIZE_PASS(DivergencePrinter, "dot-div",
                "Print divergence info of function to 'dot' file", false, false)

char DivergenceOnlyPrinter::ID = 0;
INITIALIZE_PASS(DivergenceOnlyPrinter, "dot-div-only",
                "Print divergence info of function to 'dot' file "
                "(with no function bodies)",
                false, false)

// Create methods available outside of this file, to use them
// "include/llvm/LinkAllPasses.h". Otherwise the pass would be deleted by
// the link time optimization.

FunctionPass *llvm::createDivergencePrinterPass() {
  return new DivergencePrinter();
}

FunctionPass *llvm::createDivergenceOnlyPrinterPass() {
  return new DivergenceOnlyPrinter();
}

FunctionPass *llvm::createDivergenceViewerPass() {
  return new DivergenceViewer();
}

FunctionPass *llvm::createDivergenceOnlyViewerPass() {
  return new DivergenceOnlyViewer();
}
