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

#include "ApintLiteralConvertationCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <bitset>

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace xilinx {
enum QMode {
  AP_RND,           // rounding to plus infinity
  AP_RND_ZERO,      // rounding to zero
  AP_RND_MIN_INF,   // rounding to minus infinity
  AP_RND_INF,       // rounding to infinity
  AP_RND_CONV,      // convergent rounding
  AP_TRN,           // truncation
  AP_TRN_ZERO       // truncation to zero
};
enum OMode {
  AP_SAT,       // saturation
  AP_SAT_ZERO,  // saturation to zero
  AP_SAT_SYM,   // symmetrical saturation
  AP_WRAP,      // wrap-around (*)
  AP_WRAP_SM    // sign magnitude wrap-around (*)
};

static std::string oct2Bin(char oct) {
  switch (oct) {
  case '\0': return "";
  case '.': return ".";
  case '0': return "000";
  case '1': return "001";
  case '2': return "010";
  case '3': return "011";
  case '4': return "100";
  case '5': return "101";
  case '6': return "110";
  case '7': return "111";
  }
  assert(0 && "Invalid character in digit string");
  return "";
}

static std::string hex2Bin(char hex) {
  switch (hex) {
  case '\0': return "";
  case '.': return ".";
  case '0': return "0000";
  case '1': return "0001";
  case '2': return "0010";
  case '3': return "0011";
  case '4': return "0100";
  case '5': 
      return "0101";
  case '6': return "0110";
  case '7': return "0111";
  case '8': return "1000";
  case '9': return "1001";
  case 'A':
  case 'a': return "1010";
  case 'B':
  case 'b': return "1011";
  case 'C':
  case 'c': return "1100";
  case 'D':
  case 'd': return "1101";
  case 'E':
  case 'e': return "1110";
  case 'F':
  case 'f': return "1111";
  }
  assert(0 && "Invalid character in digit string");
  return "";
}

static bool handleQuantization(llvm::APInt& V, int Width, QMode Q, bool qb,
                               bool r, bool Neg) {
  bool carry=V[Width - 1];
  switch (Q) {
  case AP_TRN:
    return false;
  case AP_RND_ZERO:
    qb &= Neg || r;
    break;
  case AP_RND_MIN_INF:
    qb &= r;
    break;
  case AP_RND_INF:
    qb &= !Neg || r;
    break;
  case AP_RND_CONV:
    qb &= V[0] || r;
    break;
  case AP_TRN_ZERO:
    qb = Neg && ( qb || r );
    break;
  default:;
  }

  if(qb) ++V;
  //only when old V[AP_W-1]==1 && new V[AP_W-1]==0
  return carry && !V[Width - 1]; //(!V[AP_W-1]);
}

static void handleOverflow(llvm::APInt& V, int Width, OMode O, int NBits,
                           bool Sign, bool Underflow, bool Overflow, bool lD,
                           bool Neg) {
  if (!Overflow && !Underflow)
    return;

  switch (O) {
  case AP_WRAP:
    if (NBits == 0)
      return;
    if (Sign) {
      //signed SC_WRAP
      //n_bits == 1;
      if (NBits > 1) {
        llvm::APInt mask = llvm::APInt::getAllOnesValue(Width);
        if (NBits >= Width) mask = 0;
        else mask = mask.lshr(NBits);
        if (Neg)
          V &= mask;
        else
          V |= ~mask;
      }
      Neg ? V.setBit(Width - 1) : V.clearBit(Width - 1);
    } else {
      //unsigned SC_WRAP
      llvm::APInt mask = llvm::APInt::getAllOnesValue(Width);
      if (NBits >= Width) mask = 0;
      else mask = mask.lshr(NBits);
      mask.flipAllBits();
      V |= mask;
    }
    break;
  case AP_SAT_ZERO:
    V = 0;
    break;
  case AP_WRAP_SM:
    {
    bool Ro = V[Width - 1]; // V[AP_W -1];
    if (NBits == 0) {
      if (lD != Ro) {
        V.flipAllBits();
        lD ? V.setBit(Width - 1) : V.clearBit(Width -1);
      }
    } else {
      if (NBits == 1 && Neg != Ro) {
        V.flipAllBits();
      } else if (NBits > 1) {
        bool lNo = V[std::max(Width -NBits , 0)]; // V[AP_W - _AP_N];
        if (lNo == Neg)
          V.flipAllBits();
        llvm::APInt mask = llvm::APInt::getAllOnesValue(Width);
        if (NBits >= Width) mask = 0;
        else mask = mask.lshr(NBits);
        if (Neg)
          V &= mask;
        else {
          mask.flipAllBits();
          V |= mask;
        }
        Neg ? V.setBit(Width - 1) : V.clearBit(Width - 1);
      }
    }
    }
    break;
  default:
    if (Sign) {
      if (Overflow) {
        V = llvm::APInt::getAllOnesValue(Width); V.clearBit(Width - 1);
      } else if (Underflow) {
        V = 0;
        V.setBit(Width - 1);
        if(O == AP_SAT_SYM)
          V.setBit(0);
      }
    } else {
      if (Overflow)
        V = llvm::APInt::getAllOnesValue(Width);
      else if (Underflow)
        V = 0;
    }
  }
}


static llvm::APInt RoundingOverflow(llvm::APInt& V, int Width, int IntWidth,
                                    bool Sign, QMode Q, OMode O, int NBits,
                                    int AllWidth, int IntWidthExt) {
  int FractWidth = Width - IntWidth, FractWidthExt = AllWidth - IntWidthExt;
  bool QUAN_INC = Q != AP_TRN;
  bool carry = false;
  //handle quantization
  int ShAmt = FractWidthExt - FractWidth;
  bool Neg = V.isNegative();
  llvm::APInt Ret = V.ashr(ShAmt);
  Ret = Ret.trunc(Width);
  if (!V) return Ret;
  if (Q != AP_TRN) {
    bool qb = false;
    assert(FractWidthExt - FractWidth - 1 < AllWidth && "out of bound!");
    qb = V[FractWidthExt - FractWidth - 1];

    bool r = false;
    int Pos3 = FractWidthExt - FractWidth - 2; // pos right after mD
    assert(Pos3 < AllWidth - 1 );
    r = (V << (AllWidth - 1 - Pos3)) != 0;
    carry = handleQuantization(Ret, Width, Q, qb, r,Neg);
  }

  //hanle overflow/underflow
  if ((O != AP_WRAP || NBits != 0) &&
      (!Sign || IntWidth - Sign <
                IntWidthExt - 1 + (QUAN_INC|| (O == AP_SAT_SYM)))) {
    bool DeletedZeros = true,
         DeletedOnes = true;
    bool lD= V[AllWidth - IntWidthExt + IntWidth];
    int Pos1 = FractWidthExt - FractWidth + Width,
        Pos2 = FractWidthExt - FractWidth + Width + 1;
    if (Pos1 < AllWidth) {
      bool Range1AllOnes  = true;
      bool Range1AllZeros = true;
      if (Pos1 >= 0) {
        int W = std::max(AllWidth-Pos1,  1);
        llvm::APInt Range1   = V.lshr(Pos1).trunc(W);
        Range1AllOnes  = Range1.isMaxValue();
        Range1AllZeros = Range1.isMinValue();
      } else {
        Range1AllOnes  = false;
        Range1AllZeros = V.isMinValue();
      }
      bool Range2AllOnes=true;
      if (Pos2 < AllWidth && Pos2 >= 0) {
        int W = std::max(AllWidth-Pos2, 1);
        llvm::APInt Range2  = V.lshr(Pos2).trunc(W);
        Range2AllOnes = Range2.isMaxValue();
      } else if (Pos2<0)
        Range2AllOnes = false;

      DeletedZeros = DeletedZeros && (carry ? Range1AllOnes : Range1AllZeros);
      DeletedOnes = carry ? Range2AllOnes && !lD :Range1AllOnes;
      Neg= Neg && !(carry && Range1AllOnes);
    } else
      Neg = Neg && Ret[Width -1];

    bool NegTrg = Ret.isNegative() && Sign;
    bool Overflow  = (NegTrg || !DeletedZeros) && !V.isNegative();
    bool Underflow = (!NegTrg || !DeletedOnes) && Neg;
    if (O == AP_SAT_SYM && Sign)
      Underflow |= Neg && (Width > 1 ? Ret.isMinSignedValue() : true);

    handleOverflow(Ret, Width, O, NBits, Sign, Underflow, Overflow, lD,
                   Neg);
  }
  return Ret;
}

static llvm::APInt fromString(const std::string Val, unsigned char Radix,
                              int32_t Width, int32_t IntWidth, bool Sign,
                              QMode Q, OMode O, int32_t NBits) {
  assert(Radix == 2 || Radix == 8 || Radix == 10 || Radix == 16);
  unsigned StartPos = 0;
  auto EndPos = Val.length();
  auto DecPos = Val.find(".");
  if (DecPos == std::string::npos)
    DecPos = EndPos;
  bool isNegative = false;
  if (Val[0] == '-') {
    isNegative = true;
    ++StartPos;
  } else if (Val[0] == '+')
    ++StartPos;

  // If there are no integer bits, e.g.:
  // .0000XXXX, then keep at least one bit.
  // If the width is greater than the number of integer bits, e.g.:
  // XXXX.XXXX, then we keep the integer bits
  // if the number of integer bits is greater than the width, e.g.:
  // XXX000 then we keep the integer bits.
  // Always keep one bit.
  uint32_t IntWidthExt = std::max(IntWidth, 4) + 4;
  llvm::APInt IntegerBits(IntWidthExt, 0);
  llvm::APInt IntRadix(IntWidthExt, Radix);
  // Figure out if we can shift instead of multiply
  uint32_t Shift = (Radix == 16 ? 4 : Radix == 8 ? 3 : Radix == 2 ? 1 : 0);

  bool Sticky = false;
  // Traverse the integer digits from the MSD, multiplying by radix as we go.
  for (unsigned i = StartPos; i < DecPos; i++) {
    // Get a digit
    char C = Val[i];
    if (C == '\0') continue;
    Sticky |= IntegerBits[std::max(IntWidth,4)+4 - 1] |
              IntegerBits[std::max(IntWidth,4)+4 - 2] |
              IntegerBits[std::max(IntWidth,4)+4 - 3] |
              IntegerBits[std::max(IntWidth,4)+4 - 4];
    // Shift or multiply the value by the Radix
    if (Shift)
      IntegerBits = IntegerBits.shl (Shift);
    else
      IntegerBits *= IntRadix;

    llvm::APInt tmp(IntWidthExt, std::string(1, C), Radix);
    // Add in the digit we just interpreted
    IntegerBits += tmp;
  }

  if (Sticky)
    IntegerBits.setBit(std::max(IntWidth,4)+4 - 3);

  uint32_t FractBW = std::max(Width - IntWidth, 0) + 4 + 4;
  llvm::APInt FractionalBits (FractBW,  0);
  llvm::APInt FractRadix(FractBW, Radix);
  Sticky = false;

  // Traverse the fractional digits from the LSD, dividing by radix as we go.
  for (unsigned i = EndPos-1; i >= DecPos+1; i--) {
    // Get a digit
    char C = Val[i];
    if (C == '\0') continue;

    // Add in the digit we just interpreted
    Sticky |= FractionalBits[0] | FractionalBits[1] |
              FractionalBits[2] | FractionalBits[3];
    llvm::APInt tmp(FractBW, std::string(1, C), Radix);
    FractionalBits += tmp << (FractBW - 4);
    // Shift or divide the value by the radix
    if (Shift)
      FractionalBits = FractionalBits.lshr(Shift);
    else
      FractionalBits = FractionalBits.udiv(FractRadix);
  }

  if (Sticky)
    FractionalBits.setBit(0);
  //add one bit as signness bit
  uint32_t AllWidth = IntWidthExt + FractBW - 4 + 1;
  llvm::APInt ResultVal(AllWidth, 0);
  IntegerBits = IntegerBits.zext(AllWidth);
  IntegerBits = IntegerBits.shl(FractBW - 4);
  FractionalBits = FractionalBits.zext(AllWidth);
  ResultVal = IntegerBits + FractionalBits;
  if(isNegative)
     ResultVal = -ResultVal;
  return RoundingOverflow(ResultVal, Width, IntWidth, Sign, Q, O, NBits,
                          AllWidth, IntWidthExt + 1);
}

static bool isInvalidDigit(std::string Str, unsigned Radix, bool isFractPart) {
  if (Str[0] == '-' || Str[0] == '+')
    Str[0] = '0';

  if (isFractPart) {
    auto Pos = Str.find_first_of('.');
    if (Pos != std::string::npos)
      Str[Pos] = '0';
  }

  Str = llvm::StringRef(Str).lower();

  switch(Radix) {
  case 2:
    return !std::all_of(Str.begin(), Str.end(),
                       [](char C) {return C == '0' || C == '1';});
  case 8:
    return !std::all_of(Str.begin(), Str.end(),
                       [](char C) {return C >= '0' && C <= '7';});
  case 10:
    return !std::all_of(Str.begin(), Str.end(),
                       [](char C) {return C >= '0' && C <= '9';});
  case 16:
    return !std::all_of(Str.begin(), Str.end(),
                       [](char C) {return (C >= '0' && C <= '9') ||
                                          (C >= 'a' && C <= 'f');});
  default:
    return true;
  }

  return true;
}

// Determine the radix and remove the exponent
static std::string parseString(const std::string &Input, int &Radix, bool &Invalid) {

  int Len = Input.length();
  if(Len == 0)
    return Input;

  // Trim whitespace
  std::string Val = llvm::StringRef(Input).trim(" \t\n\r").str();
  Len = Val.length();
  size_t StartPos = 0;

  // If the length of the string is less than 2, then radix
  // is decimal and there is no exponent.
  if (Len < 2)
    return Val;

  bool isNegative = false;
  std::string Ans;

  // First check to see if we start with a sign indicator
  if (Val[0] == '-') {
    Ans = "-";
    ++StartPos;
    isNegative = true;
  } else if (Val[0] == '+')
    ++StartPos;

  if (Len - StartPos < 2)
    return Val;

  if (Val.substr(StartPos, 2) == "0x" || Val.substr(StartPos, 2) == "0X") {
    // If we start with "0x", then the radix is hex.
    Radix = 16;
    StartPos += 2;
  } else if (Val.substr(StartPos, 2) == "0b" || Val.substr(StartPos, 2) == "0B") {
    // If we start with "0b", then the radix is binary.
    Radix = 2;
    StartPos += 2;
  } if (Val.substr(StartPos, 2) == "0o" || Val.substr(StartPos, 2) == "0O") {
    // If we start with "0o", then the radix is octal.
    Radix = 8;
    StartPos += 2;
  }

  int Exp = 0;
  if (Radix == 10) {
    // If radix is decimal, then see if there is an
    // exponent indicator.
    int ExpPos = Val.find('e');
    bool HasExp = true;
    if (ExpPos < 0) ExpPos = Val.find('E');
    if (ExpPos < 0) {
      // No exponent indicator, so the mantissa goes to the end.
      ExpPos = Len;
      HasExp = false;
    }
    std::string FractPart = Val.substr(StartPos, ExpPos-StartPos);
    if (isInvalidDigit(FractPart, Radix, true)) {
      Invalid = true;
      return "";
    }

    Ans += FractPart;
    if(HasExp) {
      // Parse the exponent.
      std::string ExpPart = Val.substr(ExpPos+1, Len-ExpPos-1);
      if (isInvalidDigit(ExpPart, 10, false)
          || ExpPart.length() <= 0) {
        Invalid = true;
        return "";
      }
      llvm::APInt APExp(64,ExpPart , 10);
      Exp = APExp.getSExtValue();
    }
  } else {
    // Check for a binary exponent indicator.
    auto ExpPos = Val.find('p');
    bool HasExp = true;
    if (ExpPos == std::string::npos) ExpPos = Val.find('P');
    if (ExpPos == std::string::npos) {
      // No exponent indicator, so the mantissa goes to the end.
      ExpPos = Len;
      HasExp = false;
    }

    assert(StartPos <= ExpPos);
    std::string FractPart = Val.substr(StartPos, ExpPos-StartPos);
    if (isInvalidDigit(FractPart, Radix, true)) {
      Invalid = true;
      return "";
    }

    // Convert to binary as we go.
    for (unsigned i = 0; i < FractPart.size(); ++i) {
      if(Radix == 16) {
        Ans += hex2Bin(FractPart[i]);
      } else if(Radix == 8) {
        Ans += oct2Bin(FractPart[i]);
      } else { // radix == 2
        Ans += FractPart[i];
      }
    }

    // End in binary
    Radix = 2;
    if (HasExp) {
      // Parse the exponent.
      std::string ExpPart = Val.substr(ExpPos+1, Len-ExpPos-1);
      if (isInvalidDigit(ExpPart, 10, false) || ExpPart.length() <= 0) {
        Invalid = true;
        return "";
      }
      llvm::APInt APExp(64, ExpPart , 10);
      Exp = APExp.getSExtValue();
    }
  }
  if (Exp == 0)
    return Ans;

  int DecPos = Ans.find('.');
  if (DecPos < 0)
    DecPos = Ans.length();
  if (DecPos + Exp >= (int)Ans.length()) {
    int i = DecPos;
    for (; i<(int)Ans.length()-1; ++i)
        Ans[i] = Ans[i+1];
    for (; i<(int)Ans.length(); ++i)
        Ans[i] = '0';
    for (; i<DecPos + Exp; ++i)
        Ans += '0';
    return Ans;
  } else if (DecPos + Exp < (int) isNegative) {
    std::string dupAns = "0.";
    if (Ans[0] == '-')
      dupAns = "-0.";
    for (int i=0; i<isNegative-DecPos-Exp; ++i)
      dupAns += '0';
    for (int i=isNegative; i<(int)Ans.length(); ++i)
      if (Ans[i] != '.')
        dupAns += Ans[i];
    return dupAns;
  }

  if (Exp > 0)
    for (int i=DecPos; i<DecPos+Exp; ++i)
      Ans[i] = Ans[i+1];
  else {
    if (DecPos == (int)Ans.length())
      Ans += ' ';
    for (int i=DecPos; i>DecPos+Exp; --i)
      Ans[i] = Ans[i-1];
  }
  Ans[DecPos+Exp] = '.';
  return Ans;
}

static std::string evalString(std::string Str, int Width, int IntWidth,
                              bool Sign, QMode Q, OMode O, int NBits,
                              int Radix) {
  bool Invalid = false;
  std::string NewStr = parseString(Str, Radix, Invalid);
  if (Invalid) {
    //FIXME: error
    return "";
  }

  auto APRaw = fromString(NewStr, Radix, Width, IntWidth, Sign, Q, O, NBits);
  return "0x" + APRaw.toString(16, false) + "ui" + std::to_string(Width);
}

void ApintLiteralConvertationCheck::registerMatchers(MatchFinder *Finder) {
  // Add matchers for CXXConstructorCallExpr with type as string ap_int &
  // ap_uint.
  Finder->addMatcher(
      cxxConstructExpr(anyOf(argumentCountIs(2), argumentCountIs(1)),
          hasType(cxxRecordDecl(matchesName("^::ap_(int|uint|fixed|ufixed)$"))))
          .bind("x"),
      this);
}

void ApintLiteralConvertationCheck::check(
    const MatchFinder::MatchResult &Result) {
  // FIXME: Add callback implementation.
  const auto *MatchedExpr = Result.Nodes.getNodeAs<CXXConstructExpr>("x");
  const auto Num = MatchedExpr->getNumArgs();
  if (Num <= 0)
    return;
  // Initialization string
  const auto *Arg0 =
      dyn_cast<StringLiteral>(MatchedExpr->getArg(0)->IgnoreImpCasts());
  if (!Arg0)
    return;
  auto Str = Arg0->getBytes();

  auto &Ctx = *Result.Context;

  // Radix
  int Radix = 10;

  if (Num == 2) {
    auto Arg1 = dyn_cast<IntegerLiteral>(MatchedExpr->getArg(1)->
                                         IgnoreImpCasts());
    if (!Arg1 || Arg1->getValue().getSExtValue() <= 0)
      return;
    Radix = Arg1->getValue().getZExtValue();
  }


  auto TemplateSpecialTy =
      cast<TemplateSpecializationType>(MatchedExpr->getType().getTypePtr());

  if (TemplateSpecialTy->getNumArgs() < 1)
    return;

  // Total width
  llvm::APSInt Width;
  if (!TemplateSpecialTy->getArg(0).getAsExpr()->EvaluateAsInt(Width, Ctx))
    return;

  // Sign
  bool Sign = false;
  if (MatchedExpr->getConstructor()->getNameAsString() == "ap_int" ||
      MatchedExpr->getConstructor()->getNameAsString() == "ap_fixed")
    Sign = true;

  // Integer width
  llvm::APSInt IntWidth(Width);
  if (TemplateSpecialTy->getNumArgs() > 1)
    if (!TemplateSpecialTy->getArg(1).getAsExpr()->EvaluateAsInt(IntWidth, Ctx))
      return;

  // Quantization mode
  QMode Q = AP_TRN;
  if (TemplateSpecialTy->getNumArgs() > 2)
    Q = static_cast<QMode>(cast<EnumConstantDecl>(
        cast<DeclRefExpr>(TemplateSpecialTy->getArg(2).getAsExpr())->getDecl())
        ->getInitVal().getSExtValue());

  // Overflow(Saturation) mode
  OMode O = AP_WRAP;
  if (TemplateSpecialTy->getNumArgs() > 3)
    O = static_cast<OMode>(cast<EnumConstantDecl>(
        cast<DeclRefExpr>(TemplateSpecialTy->getArg(3).getAsExpr())->getDecl())
        ->getInitVal().getSExtValue());

  // NBits for Overflow(Saturation) mode
  llvm::APSInt NBits;
  if (TemplateSpecialTy->getNumArgs() > 4)
    if (!TemplateSpecialTy->getArg(4).getAsExpr()->EvaluateAsInt(NBits, Ctx))
      return;

  auto RawVal = evalString(Str, Width.getExtValue(), IntWidth.getExtValue(),
                           Sign, Q, O, NBits.getExtValue(), Radix);

  auto Replace = "(" + RawVal + ", true)";
  auto R = MatchedExpr->getParenOrBraceRange();
  if (R.isInvalid()) {
    auto Ty = MatchedExpr->getType().getAsString(Ctx.getPrintingPolicy());
    Replace = Ty + Replace;
    auto Start = MatchedExpr->getLocStart();
    auto End = Start.getLocWithOffset(Str.size() + 2);
    R.setBegin(Start);
    R.setEnd(End);
  } else {
    R.setEnd(R.getEnd().getLocWithOffset(1));
  }
  

  diag(MatchedExpr->getLocation(),
       "change ap_int string consturctor into int constructor")
      << FixItHint::CreateReplacement(CharSourceRange::getCharRange(R), Replace);
}
} // namespace xilinx
} // namespace tidy
} // namespace clang
