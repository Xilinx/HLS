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
// This file defines a simple interface that can be used to print out generic
// LLVM graphs to ".dot" files.  "dot" is a tool that is part of the AT&T
// graphviz package (http://www.research.att.com/sw/tools/graphviz/) which can
// be used to turn the files output by this interface into a variety of
// different graphics formats.
//
// Graphs do not need to implement any interface past what is already required
// by the GraphTraits template, but they can choose to implement specializations
// of the DOTGraphTraits template if they want to customize the graphs output in
// any way.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_GRAPHHIERWRITER_H
#define LLVM_SUPPORT_GRAPHHIERWRITER_H

#include "llvm/Support/GraphWriter.h"
#include "llvm/ADT/SetVector.h"

namespace llvm {

template<typename GraphType>
class GraphHierWriter {
  raw_ostream &O;
  const GraphType &G;

  using DOTTraits = DOTGraphTraits<GraphType>;
  using GTraits = GraphTraits<GraphType>;
  using NodeRef = typename GTraits::NodeRef;
  using node_iterator = typename GTraits::nodes_iterator;
  using child_iterator = typename GTraits::ChildIteratorType;
  DOTTraits DTraits;

  static_assert(std::is_pointer<NodeRef>::value,
                "FIXME: Currently GraphHierWriter requires the NodeRef type to be "
                "a pointer.\nThe pointer usage should be moved to "
                "DOTGraphTraits, and removed from GraphHierWriter itself.");

  // Writes the edge labels of the node to O and returns true if there are any
  // edge labels not equal to the empty string "".
  bool getEdgeSourceLabels(raw_ostream &O, NodeRef Node) {
    child_iterator EI = GTraits::child_begin(Node);
    child_iterator EE = GTraits::child_end(Node);
    bool hasEdgeSourceLabels = false;

    for (unsigned i = 0; EI != EE && i != 64; ++EI, ++i) {
      std::string label = DTraits.getEdgeSourceLabel(Node, EI);

      if (label.empty())
        continue;

      hasEdgeSourceLabels = true;

      if (i)
        O << "|";

      O << "<s" << i << ">" << DOT::EscapeString(label);
    }

    if (EI != EE && hasEdgeSourceLabels)
      O << "|<s64>truncated...";

    return hasEdgeSourceLabels;
  }

public:
  GraphHierWriter(raw_ostream &o, const GraphType &g, bool SN) : O(o), G(g) {
    DTraits = DOTTraits(SN);
  }

  void writeGraph(const std::string &Title = "") {
    // Output the header for the graph...
    writeHeader(Title);

    // Emit all of the nodes in the graph...
    writeNodes();

    // Output any customizations on the graph
    DOTGraphTraits<GraphType>::addCustomGraphFeatures(G, *this);

    // Output the end of the graph
    writeFooter();
  }

  void writeHeader(const std::string &Title) {
    std::string GraphName = DTraits.getGraphName(G);

    if (!Title.empty())
      O << "digraph \"" << DOT::EscapeString(Title) << "\" {\n";
    else if (!GraphName.empty())
      O << "digraph \"" << DOT::EscapeString(GraphName) << "\" {\n";
    else
      O << "digraph unnamed {\n";

    if (DTraits.renderGraphFromBottomUp())
      O << "\trankdir=\"BT\";\n";

    if (!Title.empty())
      O << "\tlabel=\"" << DOT::EscapeString(Title) << "\";\n";
    else if (!GraphName.empty())
      O << "\tlabel=\"" << DOT::EscapeString(GraphName) << "\";\n";
    O << DTraits.getGraphProperties(G);
    O << "\n";
  }

  void writeFooter() {
    // Finish off the graph
    O << "}\n";
  }

  void writeNodes() {
    // Loop over the graph, printing it out...
    for (const auto Node : nodes<GraphType>(G))
      if (!isNodeHidden(Node, G))
        writeNode(Node, G);
  }

  //bool isNodeHidden(NodeRef Node) {
  //  return DTraits.isNodeHidden(Node);
  //}

  bool isNodeHidden(NodeRef Node, GraphType G) {
    return DTraits.isNodeHidden(Node, G);
  }

  void writeNode(NodeRef Node, GraphType G) {
    std::string NodeAttributes = DTraits.getNodeAttributes(Node, G);

    O << "\tNode" << static_cast<const void*>(Node) << " [shape=record,";
    if (!NodeAttributes.empty()) O << NodeAttributes << ",";
    O << "label=\"{";

    if (!DTraits.renderGraphFromBottomUp()) {
      O << DOT::EscapeString(DTraits.getNodeLabel(Node, G));

      // If we should include the address of the node in the label, do so now.
      std::string Id = DTraits.getNodeIdentifierLabel(Node, G);
      if (!Id.empty())
        O << "|" << DOT::EscapeString(Id);

      std::string NodeDesc = DTraits.getNodeDescription(Node, G);
      if (!NodeDesc.empty())
        O << "|" << DOT::EscapeString(NodeDesc);
    }

    std::string edgeSourceLabels;
    raw_string_ostream EdgeSourceLabels(edgeSourceLabels);
    bool hasEdgeSourceLabels = getEdgeSourceLabels(EdgeSourceLabels, Node);

    if (hasEdgeSourceLabels) {
      if (!DTraits.renderGraphFromBottomUp()) O << "|";

      O << "{" << EdgeSourceLabels.str() << "}";

      if (DTraits.renderGraphFromBottomUp()) O << "|";
    }

    if (DTraits.renderGraphFromBottomUp()) {
      O << DOT::EscapeString(DTraits.getNodeLabel(Node, G));

      // If we should include the address of the node in the label, do so now.
      std::string Id = DTraits.getNodeIdentifierLabel(Node, G);
      if (!Id.empty())
        O << "|" << DOT::EscapeString(Id);

      std::string NodeDesc = DTraits.getNodeDescription(Node, G);
      if (!NodeDesc.empty())
        O << "|" << DOT::EscapeString(NodeDesc);
    }

    if (DTraits.hasEdgeDestLabels()) {
      O << "|{";

      unsigned i = 0, e = DTraits.numEdgeDestLabels(Node);
      for (; i != e && i != 64; ++i) {
        if (i) O << "|";
        O << "<d" << i << ">"
          << DOT::EscapeString(DTraits.getEdgeDestLabel(Node, i));
      }

      if (i != e)
        O << "|<d64>truncated...";
      O << "}";
    }

    O << "}\"];\n";   // Finish printing the "node" line

    // Output all of the edges now
    child_iterator EI = GTraits::child_begin(Node);
    child_iterator EE = GTraits::child_end(Node);
    for (unsigned i = 0; EI != EE && i != 64; ++EI, ++i)
      if (!DTraits.isNodeHidden(*EI, G))
        writeEdge(Node, i, EI);
      else {
        child_iterator newEI = EI;
        bool found = findNextNode(EI, newEI, G);
        if(found) writeVirtualEdge(Node, i, EI, newEI);
      }
    for (; EI != EE; ++EI)
      if (!DTraits.isNodeHidden(*EI, G))
        writeEdge(Node, 64, EI);
      else {
        child_iterator newEI = EI;
        bool found = findNextNode(EI, newEI, G);
        if(found) writeVirtualEdge(Node, 64, EI, newEI);
      }
 
  }

  //Find non-hidden node via DFS exploring
  bool findNextNode(child_iterator oldNode, child_iterator &newNode, GraphType G) {
    if(!DTraits.isNodeHidden(*oldNode, G)) return false;
    child_iterator EI = GTraits::child_begin(*oldNode);
    child_iterator EE = GTraits::child_end(*oldNode);
    for (; EI != EE; ++EI) {
        if (!DTraits.isNodeHidden(*EI, G)) {
            newNode = EI;
            return true;
        }
        else {
            // explore in deeper
            bool found = findNextNode(EI, newNode, G);
            if(found) return true;
        }
    }
    return false;
  }

  bool getHiddenAttributes(child_iterator Node,
                           SetVector<CallInst *> &CIs,
                           std::vector<std::pair<Instruction *, std::string>> &MemReads,
                           std::vector<std::pair<Instruction *, std::string>> &MemWrites,
                           uint64_t *ExeNum,
                           GraphType G) {
    if(!DTraits.isNodeHidden(*Node, G)) return false;
    DTraits.getCIs(*Node, CIs);
    DTraits.getMemOPs(*Node, MemReads, MemWrites);
    *ExeNum = DTraits.getNodeExeNum(*Node, G); //Suppose hidden BBs in one edge have same exe number
    child_iterator EI = GTraits::child_begin(*Node);
    child_iterator EE = GTraits::child_end(*Node);
    for (; EI != EE; ++EI) {
        if (!DTraits.isNodeHidden(*EI, G)) {
          return true;
        } else {
          bool found = getHiddenAttributes(EI, CIs, MemReads, MemWrites, ExeNum, G);
          if(found) return true;
        }
    }
    return false;
  }

  //Find attributes of virtual edge
  std::string getVirtualEdgeAttributes(NodeRef Node, child_iterator EI, GraphType G) {
    SetVector<CallInst *> CIs;
    std::vector<std::pair<Instruction *, std::string>> MemReads;
    std::vector<std::pair<Instruction *, std::string>> MemWrites;
    uint64_t EdgeExeNum; //execusion time for each edge
    getHiddenAttributes(EI, CIs, MemReads, MemWrites, &EdgeExeNum, G);

    std::string Attrs = DTraits.getEdgeAttributes(Node, EI, G);
    // 1. add calllist to virtual edge
    Attrs = Attrs + " callList=\"" + DTraits.getCallList(CIs) + "\"";

    // 2. add memory operation list to virtual edge
    std::string ReadAttrList = DTraits.getMemOpList(MemReads, false);
    std::string WriteAttrList = DTraits.getMemOpList(MemWrites, true);
    if(!WriteAttrList.empty())
      ReadAttrList = ReadAttrList + "; " + WriteAttrList;
    Attrs = Attrs + " memoryops=\"" + ReadAttrList + "\"";

    // 3. add filename to virtual edge
    Attrs = Attrs + " filename=\"" + DTraits.getFileName(*EI) + "\"";

    // 4. add exe_number to virtual edge
    Attrs = Attrs + " execusionnum=\"" + std::to_string(EdgeExeNum) + "\"";

    return Attrs;
  }

  void writeEdge(NodeRef Node, unsigned edgeidx, child_iterator EI) {
    if (NodeRef TargetNode = *EI) {
      int DestPort = -1;
      if (DTraits.edgeTargetsEdgeSource(Node, EI)) {
        child_iterator TargetIt = DTraits.getEdgeTarget(Node, EI);

        // Figure out which edge this targets...
        unsigned Offset =
          (unsigned)std::distance(GTraits::child_begin(TargetNode), TargetIt);
        DestPort = static_cast<int>(Offset);
      }

      if (DTraits.getEdgeSourceLabel(Node, EI).empty())
        edgeidx = -1;

      emitEdge(static_cast<const void*>(Node), edgeidx,
               static_cast<const void*>(TargetNode), DestPort,
               DTraits.getEdgeAttributes(Node, EI, G));
    }
  }

  //For hidden node, write a virtual edge instead of real edge
  void writeVirtualEdge(NodeRef Node, unsigned edgeidx, child_iterator oldEI, child_iterator newEI) {
    if (NodeRef TargetNode = *oldEI) {
      if (NodeRef VirtualTargetNode = *newEI) {
          int DestPort = -1;
          if (DTraits.edgeTargetsEdgeSource(Node, oldEI)) {
            child_iterator TargetIt = DTraits.getEdgeTarget(Node, oldEI);

            // Figure out which edge this targets...
            unsigned Offset =
              (unsigned)std::distance(GTraits::child_begin(TargetNode), TargetIt);
            DestPort = static_cast<int>(Offset);
          }

          if (DTraits.getEdgeSourceLabel(Node, oldEI).empty())
            edgeidx = -1;

          //All attribute of edge uses the old one
          //but the edge on dot graph will directly connect new node
          emitEdge(static_cast<const void*>(Node), edgeidx,
                   static_cast<const void*>(VirtualTargetNode), DestPort,
                   getVirtualEdgeAttributes(Node, oldEI, G));
      }
    }
  }


  /// emitSimpleNode - Outputs a simple (non-record) node
  void emitSimpleNode(const void *ID, const std::string &Attr,
                   const std::string &Label, unsigned NumEdgeSources = 0,
                   const std::vector<std::string> *EdgeSourceLabels = nullptr) {
    O << "\tNode" << ID << "[ ";
    if (!Attr.empty())
      O << Attr << ",";
    O << " label =\"";
    if (NumEdgeSources) O << "{";
    O << DOT::EscapeString(Label);
    if (NumEdgeSources) {
      O << "|{";

      for (unsigned i = 0; i != NumEdgeSources; ++i) {
        if (i) O << "|";
        O << "<s" << i << ">";
        if (EdgeSourceLabels) O << DOT::EscapeString((*EdgeSourceLabels)[i]);
      }
      O << "}}";
    }
    O << "\"];\n";
  }

  /// emitEdge - Output an edge from a simple node into the graph...
  void emitEdge(const void *SrcNodeID, int SrcNodePort,
                const void *DestNodeID, int DestNodePort,
                const std::string &Attrs) {
    if (SrcNodePort  > 64) return;             // Eminating from truncated part?
    if (DestNodePort > 64) DestNodePort = 64;  // Targeting the truncated part?

    O << "\tNode" << SrcNodeID;
    if (SrcNodePort >= 0)
      O << ":s" << SrcNodePort;
    O << " -> Node" << DestNodeID;
    if (DestNodePort >= 0 && DTraits.hasEdgeDestLabels())
      O << ":d" << DestNodePort;

    if (!Attrs.empty())
      O << "[" << Attrs << "]";
    O << ";\n";
  }

  /// getOStream - Get the raw output stream into the graph file. Useful to
  /// write fancy things using addCustomGraphFeatures().
  raw_ostream &getOStream() {
    return O;
  }

  const GraphType &getGraph() {
    return G;
  }
};

template<typename GraphType>
raw_ostream &WriteHierGraph(raw_ostream &O, const GraphType &G,
                        bool ShortNames = false,
                        const Twine &Title = "") {
  // Start the graph emission process...
  GraphHierWriter<GraphType> W(O, G, ShortNames);

  // Emit the graph.
  W.writeGraph(Title.str());

  return O;
}

//template <typename GraphType>
//std::string WriteGraph(const GraphType &G, const Twine &Name,
//                       bool ShortNames = false, const Twine &Title = "") {
//  int FD;
//  // Windows can't always handle long paths, so limit the length of the name.
//  std::string N = Name.str();
//  N = N.substr(0, std::min<std::size_t>(N.size(), 140));
//  std::string Filename = createGraphFilename(N, FD);
//  raw_fd_ostream O(FD, /*shouldClose=*/ true);
//
//  if (FD == -1) {
//    errs() << "error opening file '" << Filename << "' for writing!\n";
//    return "";
//  }
//
//  llvm::WriteGraph(O, G, ShortNames, Title);
//  errs() << " done. \n";
//
//  return Filename;
//}

///// ViewGraph - Emit a dot graph, run 'dot', run gv on the postscript file,
///// then cleanup.  For use from the debugger.
/////
//template<typename GraphType>
//void ViewGraph(const GraphType &G, const Twine &Name,
//               bool ShortNames = false, const Twine &Title = "",
//               GraphProgram::Name Program = GraphProgram::DOT) {
//  std::string Filename = llvm::WriteGraph(G, Name, ShortNames, Title);
//
//  if (Filename.empty())
//    return;
//
//  DisplayGraph(Filename, false, Program);
//}

} // end namespace llvm

#endif // LLVM_SUPPORT_GRAPHWRITER_H
