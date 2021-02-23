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

#include "LabelAllLoopsCheck.h"
#include "XilinxTidyCommon.h"
#include "clang/AST/ASTContext.h"
#include "../utils/ASTUtils.h"

namespace clang {
namespace tidy {
namespace xilinx {

std::string LabelAllLoopsCheck::loopLabelPrefix = "VITIS_LOOP_";
    
std::string LabelAllLoopsCheck::getUniqueLabel(const Stmt *loopStmt,
                                               const MatchFinder::MatchResult &Result,
                                               unsigned int &loopCounter,
                                               const FunctionDecl *fDecl) {
    // Generate a unique label name that doesnt conflict with any named declaration
    std::string uniqueLabel = loopLabelPrefix;
    PresumedLoc PLoc = Result.SourceManager->getPresumedLoc(loopStmt->getLocStart());
    if (PLoc.isValid()) {
        uniqueLabel += std::to_string(PLoc.getLine()) + "_";
    }
    uniqueLabel += std::to_string(loopCounter);

    auto ConflictingDecl = namedDecl(hasName(uniqueLabel));
    if (!match(findAll(ConflictingDecl), *fDecl, *Result.Context).empty()) {
        ++loopCounter;
        return getUniqueLabel(loopStmt, Result, loopCounter, fDecl);
    }

    return uniqueLabel;
}

std::string LabelAllLoopsCheck::getLoopLabel(const MatchFinder::MatchResult &Result,
                                             const Stmt *loopStmt) {
    // Loop Labels are function scoped and so maintain a mapping between
    // the function decl and a loop counter. 
    const FunctionDecl *fDecl = utils::getSurroundingFunction(*Result.Context,
                                                              *loopStmt);
    assert(fDecl);
    unsigned int loopCounter = 1;
    if (funcLabelMap.find(fDecl) != funcLabelMap.end()) {
        loopCounter = ++funcLabelMap[fDecl];
    }

    std::string uniqueLabel = getUniqueLabel(loopStmt, Result, loopCounter,
                                             fDecl);
    // loopCounter may change in the call to getUniqueLabel and so update the map
    funcLabelMap[fDecl] = loopCounter; 

    // Return as a unique label statement
    return (uniqueLabel + ": ");
}

void LabelAllLoopsCheck::registerMatchers(MatchFinder *Finder) {
    if (getLangOpts().OpenCL)
    return;

    // Need to skip loops in function that has been implicitly added by the
    // compiler (eg. implicit default copy constructors).
    auto noImplicitLoop = unless(hasAncestor(functionDecl(isImplicit())));
    
    // Skip loops in constexpr function, because label in constexpr fucntion is
    // not allowed.
    auto noConstExpr = unless(hasAncestor(functionDecl(isConstexpr())));
    // Look for all for/for-range/do-while/while loops without labels
    auto attHasLabel = attributedStmt(hasParent(labelStmt()));
    auto noLabeledParent = unless(hasParent(attHasLabel));
    auto loop = anyOf(forStmt(), cxxForRangeStmt(), doStmt(), whileStmt());

    // Label those loops that are not not already labeled and are not implicit loops
    // created by the compiler.
    auto unlabeledLoops = allOf(loop, noLabeledParent, 
                                noImplicitLoop, noConstExpr);

    Finder->addMatcher(stmt(unlabeledLoops).bind("loop"), this);
}

void LabelAllLoopsCheck::check(const MatchFinder::MatchResult &Result) {

    if (const auto *MatchedLoop = Result.Nodes.getNodeAs<Stmt>("loop")) {
        std::string loopLabel = getLoopLabel(Result, MatchedLoop);
        diag(MatchedLoop->getLocStart(), "Added loop label %0 ")
            << loopLabel 
            << FixItHint::CreateInsertion(MatchedLoop->getLocStart(), loopLabel);
    }
}

} // namespace xilinx
} // namespace tidy
} // namespace clang
