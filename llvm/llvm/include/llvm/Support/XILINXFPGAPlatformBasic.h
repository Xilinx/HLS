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
// Xilinx's platform information. Please do not modify the content of this file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_XILINXFPGAPLATFORM_BASIC_H
#define LLVM_SUPPORT_XILINXFPGAPLATFORM_BASIC_H

#include <map>
#include <utility>
#include <vector>
#include <string>

namespace platform
{

/** the class for front-end to check op+impl and latency range **/
class PlatformBasic
{
public:
enum OP_TYPE
{
    OP_UNSUPPORTED = -1,
    OP_RET = 1,
    OP_BR = 2,
    OP_SWITCH = 3,
    OP_INDIRECTBR = 4,
    OP_INVOKE = 5,
    OP_RESUME = 6,
    OP_UNREACHABLE = 7,
    OP_ADD = 8,
    OP_FADD = 9,
    OP_SUB = 10,
    OP_FSUB = 11,
    OP_MUL = 12,
    OP_FMUL = 13,
    OP_UDIV = 14,
    OP_SDIV = 15,
    OP_FDIV = 16,
    OP_UREM = 17,
    OP_SREM = 18,
    OP_FREM = 19,
    OP_SHL = 20,
    OP_LSHR = 21,
    OP_ASHR = 22,
    OP_AND = 23,
    OP_OR = 24,
    OP_XOR = 25,
    OP_ALLOCA = 26,
    OP_LOAD = 27,
    OP_STORE = 28,
    OP_GETELEMENTPTR = 29,
    OP_FENCE = 30,
    OP_ATOMICCMPXCHG = 31,
    OP_ATOMICRMW = 32,
    OP_TRUNC = 33,
    OP_ZEXT = 34,
    OP_SEXT = 35,
    OP_FPTOUI = 36,
    OP_FPTOSI = 37,
    OP_UITOFP = 38,
    OP_SITOFP = 39,
    OP_FPTRUNC = 40,
    OP_FPEXT = 41,
    OP_PTRTOINT = 42,
    OP_INTTOPTR = 43,
    OP_BITCAST = 44,
    OP_ICMP = 45,
    OP_FCMP = 46,
    OP_PHI = 47,
    OP_CALL = 48,
    OP_SELECT = 49,
    OP_USEROP1 = 50,
    OP_USEROP2 = 51,
    OP_VAARG = 52,
    OP_EXTRACTELEMENT = 53,
    OP_INSERTELEMENT = 54,
    OP_SHUFFLEVECTOR = 55,
    OP_EXTRACTVALUE = 56,
    OP_INSERTVALUE = 57,
    OP_LANDINGPAD = 58,
    OP_FSQRT = 503,
    OP_FRSQRT = 504,
    OP_FRECIP = 505,
    OP_FLOG = 506,
    OP_FEXP = 507,
    OP_DADD = 508,
    OP_DSUB = 509,
    OP_DMUL = 510,
    OP_DDIV = 511,
    OP_DREM = 512,
    OP_DCMP = 513,
    OP_DPTOUI = 514,
    OP_DPTOSI = 515,
    OP_UITODP = 516,
    OP_SITODP = 517,
    OP_DSQRT = 518,
    OP_DRSQRT = 519,
    OP_DRECIP = 520,
    OP_DLOG = 521,
    OP_DEXP = 522,
    OP_CTPOP = 525,
    OP_SETEQ = 526,
    OP_SETNE = 527,
    OP_SETLE = 528,
    OP_SETGE = 529,
    OP_SETLT = 530,
    OP_SETGT = 531,
    OP_SPTOHP = 532,
    OP_HPTOSP = 533,
    OP_HADD = 534,
    OP_HSUB = 535,
    OP_HMUL = 536,
    OP_HDIV = 537,
    OP_HSQRT = 538,
    OP_HCMP = 539,
    OP_HPTODP = 540,
    OP_DPTOHP = 541,
    OP_MAC16_CLOCKX2 = 542,
    OP_MAC6_MAC8_CLOCKX2 = 543,
    OP_DSP = 547,
    OP_DSP_AB = 548,
    OP_BLACKBOX = 580,
    OP_CMUL = 581,
    OP_FACC = 582,
    OP_DACC = 583,
    OP_FMAC = 584,
    OP_MEMORY = 666,
    OP_ALL = 777,
    OP_ADAPTER = 888,
    OP_VIVADO_IP = 999,
    OP_BITSELECT = 1000,
    OP_BITSET = 1001,
    OP_PARTSELECT = 1002,
    OP_PARTSET = 1003,
    OP_BITCONCATENATE = 1004,
    OP_XORREDUCE = 1005,
    OP_XNORREDUCE = 1006,
    OP_ANDREDUCE = 1007,
    OP_NANDREDUCE = 1008,
    OP_ORREDUCE = 1009,
    OP_NORREDUCE = 1010,
    OP_IFREAD = 1100,
    OP_IFWRITE = 1101,
    OP_IFNBREAD = 1102,
    OP_IFNBWRITE = 1103,
    OP_IFCANREAD = 1104,
    OP_IFCANWRITE = 1105,
    OP_READ = 1150,
    OP_WRITE = 1151,
    OP_NBREAD = 1152,
    OP_NBWRITE = 1153,
    OP_READREQ = 1154,
    OP_WRITEREQ = 1155,
    OP_NBREADREQ = 1156,
    OP_NBWRITEREQ = 1157,
    OP_WRITERESP = 1158,
    OP_PEEK = 1160,
    OP_NBPEEK = 1161,
    OP_ADDMUL = 1170,
    OP_SUBMUL = 1171,
    OP_MULADD = 1172,
    OP_MULSUB = 1173,
    OP_MUL_SUB = 1174,
    OP_ADDMULADD = 1175,
    OP_SUBMULADD = 1176,
    OP_ADDMULSUB = 1177,
    OP_ADDMUL_SUB = 1178,
    OP_SUBMULSUB = 1179,
    OP_SUBMUL_SUB = 1180,
    OP_MULSELECT = 1181,
    OP_MUL_SELECT = 1182,
    OP_MULSELECTIVT = 1183,
    OP_MUL_SELECTIVT = 1184,
    OP_MULADDACC = 1185,
    OP_MULSUBACC = 1186,
    OP_ADDMULADDACC = 1187,
    OP_ADDMULSUBACC = 1188,
    OP_SUBMULADDACC = 1189,
    OP_SUBMULSUBACC = 1190,
    OP_QADDSUB = 1191,
    OP_ADDMULADDSEL = 1192,
    OP_MEMSHIFTREAD = 1195,
    OP_MUX = 1196,
    OP_WAITEVENT = 1200,
    OP_WAIT = 1201,
    OP_POLL = 1202,
    OP_RETURN = 1203,
    OP_DOTPRA3 = 1210,
    OP_DOTPRA3ADD = 1211,
    OP_SPECDT = 2000,
    OP_SPECPORT = 2001,
    OP_SPECINTERFACE = 2006,
    OP_SPECCHANNEL = 2007,
    OP_SPECMODULE = 2008,
    OP_SPECMODULEINST = 2009,
    OP_SPECPORTMAP = 2010,
    OP_SPECPROCESSDECL = 2011,
    OP_SPECPROCESSDEF = 2012,
    OP_SPECSENSITIVE = 2013,
    OP_SPECEXT = 2014,
    OP_SPECSTABLE = 2015,
    OP_SPECSTABLECONTENT = 2016,
    OP_SPECPIPODEPTH = 2017,
    OP_SPECPIPELINE = 2050,
    OP_SPECDATAFLOWPIPELINE = 2051,
    OP_SPECLOOPPIPELINE = 2052,
    OP_SPECFUNCTIONPIPELINE = 2053,
    OP_SPECREGIONBEGIN = 2100,
    OP_SPECREGIONEND = 2101,
    OP_SPECSTATEBEGIN = 2110,
    OP_SPECSTATEEND = 2111,
    OP_SPECLATENCY = 2120,
    OP_SPECPARALLEL = 2121,
    OP_SPECPROTOCOL = 2122,
    OP_SPECOCCURRENCE = 2123,
    OP_SPECBURST = 2124,
    OP_SPECBRAMWITHBYTEENABLE = 2125,
    OP_SPECRESOURCE = 2130,
    OP_SPECFUCORE = 2131,
    OP_SPECIFCORE = 2132,
    OP_SPECIPCORE = 2133,
    OP_SPECKEEPVALUE = 2134,
    OP_SPECMEMCORE = 2135,
    OP_SPECCHCORE = 2136,
    OP_SPECRESOURCELIMIT = 2137,
    OP_SPECLOOPBEGIN = 2140,
    OP_SPECLOOPEND = 2141,
    OP_SPECLOOPNAME = 2142,
    OP_SPECLOOPTRIPCOUNT = 2143,
    OP_SPECPARALLELLOOP = 2144,
    OP_SPECLOOPFLATTEN = 2145,
    OP_SPECCLOCKDOMAIN = 2150,
    OP_SPECPOWERDOMAIN = 2151,
    OP_SPECPLATFORM = 2152,
    OP_SPECRESET = 2160,
    OP_SPECTOPMODULE = 2200,
    OP_SPECSYNMODULE = 2201,
    OP_SPECBITSMAP = 2202,
    OP_COMPLEXMUL = 2205
};

enum IMPL_TYPE
{
    UNSUPPORTED = -1,
        AUTO = 0,
    AUTO_COMB,
    AUTO_PIPE,
    DSP,
    FABRIC,
    FABRIC_COMB,
    FABRIC_SEQ,
    FIFO_MEMORY,
    FIFO_BRAM,
    FIFO_LUTRAM,
    FIFO_SRL,
    FIFO_URAM,
    FULLDSP,
    MAXDSP,
    MEDDSP,
    NODSP,
    PRIMITIVEDSP,
    RAM_1P_AUTO,
    RAM_1P_BRAM,
    RAM_1P_LUTRAM,
    RAM_1P_URAM,
    RAM_2P_AUTO,
    RAM_2P_BRAM,
    RAM_2P_LUTRAM,
    RAM_2P_URAM,
    RAM_S2P_BRAM,
    RAM_S2P_BRAM_ECC,
    RAM_S2P_LUTRAM,
    RAM_S2P_URAM,
    RAM_S2P_URAM_ECC,
    RAM_T2P_BRAM,
    RAM_T2P_URAM,
    ROM_1P_AUTO,
    ROM_1P_BRAM,
    ROM_1P_LUTRAM,
    ROM_2P_AUTO,
    ROM_2P_BRAM,
    ROM_2P_LUTRAM,
    ROM_NP_BRAM,
    ROM_NP_LUTRAM,
    INTERNAL_START = 100,
    AUTO_2STAGE,
    AUTO_3STAGE,
    AUTO_4STAGE,
    AUTO_5STAGE,
    AUTO_6STAGE,
    AUTO_MUX,
    AUTO_SEL,
    AUTO_SEQ,
    AXI4LITES,
    AXI4M,
    AXI4STREAM,
    DPWOM_AUTO,
    DSP48,
    DSP58_DP,
    DSP_AM,
    DSP_AMA,
    DSP_AMA_2STAGE,
    DSP_AMA_3STAGE,
    DSP_AMA_4STAGE,
    DSP_AM_2STAGE,
    DSP_AM_3STAGE,
    DSP_AM_4STAGE,
    DSP_MAC,
    DSP_MAC_2STAGE,
    DSP_MAC_3STAGE,
    DSP_MAC_4STAGE,
    FSL,
    M_AXI,
    NPI64M,
    PLB46M,
    PLB46S,
    QADDER,
    RAM3S_AUTO,
    RAM4S_AUTO,
    RAM5S_AUTO,
    RAM_AUTO,
    RAM_1WNR,
    RAM_1WNR_LUTRAM,
    RAM_1WNR_BRAM,
    RAM_1WNR_URAM,
    RAM_COREGEN_AUTO,
    REGISTER_AUTO,
    ROM_1P_1S_AUTO,
    ROM3S_AUTO,
    ROM4S_AUTO,
    ROM5S_AUTO,
    ROM_AUTO,
    ROM_COREGEN_AUTO,
    ROM_NP_AUTO,
    SHIFTREG_AUTO,
    SPRAM_COREGEN_AUTO,
    SPROM_COREGEN_AUTO,
    SPWOM_AUTO,
    STREAM_BUNDLE,
    S_AXILITE,
    TADDER,
    VIVADO_DDS,
    VIVADO_FFT,
    VIVADO_FIR,
    VIVADO_DIVIDER,
    XPM_MEMORY_AUTO,
    XPM_MEMORY_DISTRIBUTE,
    XPM_MEMORY_BLOCK,
    XPM_MEMORY_URAM,
    //NOIMPL, for resource pragma without impl
    NOIMPL = 200,
    NOIMPL_SHIFTREG,
    NOIMPL_REGISTER,
    NOIMPL_XPM_MEMORY,
    NOIMPL_FIFO,
    NOIMPL_RAM,
    NOIMPL_RAM_1WNR,
    NOIMPL_RAM_1P,
    NOIMPL_RAM_2P,
    NOIMPL_RAM_2P_1S,
    NOIMPL_RAM_T2P,
    NOIMPL_RAM_S2P,
    NOIMPL_ROM,
    NOIMPL_ROM_1P,
    NOIMPL_ROM_1P_1S,
    NOIMPL_ROM_NP,
    NOIMPL_ROM_2P,
    NOIMPL_RAM3S,
    NOIMPL_RAM4S,
    NOIMPL_RAM5S,
    NOIMPL_RAM_COREGEN,
    NOIMPL_SPRAM_COREGEN,
    NOIMPL_ROM3S,
    NOIMPL_ROM4S,
    NOIMPL_ROM5S,
    NOIMPL_ROM_COREGEN,
    NOIMPL_SPROM_COREGEN,
    NOIMPL_SPROMD_ASYNC,
    NOIMPL_DPWOM,
    NOIMPL_SPWOM
};
enum MEMORY_TYPE
{
    MEMORY_UNSUPPORTED = UNSUPPORTED,
    // easy map form impl to memory type
    MEMORY_SHIFTREG = NOIMPL_SHIFTREG,
    MEMORY_REGISTER = NOIMPL_REGISTER,
    MEMORY_XPM_MEMORY = NOIMPL_XPM_MEMORY,
    MEMORY_FIFO = NOIMPL_FIFO,
    MEMORY_RAM  = NOIMPL_RAM,
    MEMORY_RAM_1WNR = NOIMPL_RAM_1WNR,
    MEMORY_RAM_1P = NOIMPL_RAM_1P,
    MEMORY_RAM_2P = NOIMPL_RAM_2P,
    MEMORY_RAM_2P_1S = NOIMPL_RAM_2P_1S ,
    MEMORY_RAM_T2P = NOIMPL_RAM_T2P,
    MEMORY_RAM_S2P = NOIMPL_RAM_S2P,
    MEMORY_ROM  = NOIMPL_ROM,
    MEMORY_ROM_1P = NOIMPL_ROM_1P,
    MEMORY_ROM_1P_1S  = NOIMPL_ROM_1P_1S,
    MEMORY_ROM_NP = NOIMPL_ROM_NP,
    MEMORY_ROM_2P = NOIMPL_ROM_2P,
    MEMORY_RAM3S = NOIMPL_RAM3S,
    MEMORY_RAM4S = NOIMPL_RAM4S,
    MEMORY_RAM5S = NOIMPL_RAM5S,
    MEMORY_RAM_COREGEN = NOIMPL_RAM_COREGEN,
    MEMORY_SPRAM_COREGEN = NOIMPL_SPRAM_COREGEN,
    MEMORY_ROM3S = NOIMPL_ROM3S,
    MEMORY_ROM4S = NOIMPL_ROM4S,
    MEMORY_ROM5S = NOIMPL_ROM5S,
    MEMORY_ROM_COREGEN = NOIMPL_ROM_COREGEN,
    MEMORY_SPROM_COREGEN = NOIMPL_SPROM_COREGEN,
    MEMORY_SPROMD_ASYNC = NOIMPL_SPROMD_ASYNC,
    MEMORY_DPWOM = NOIMPL_DPWOM,
    MEMORY_SPWOM = NOIMPL_SPWOM
};

enum MEMORY_IMPL
{
    MEMORY_IMPL_UNSUPPORTED = -1,
    MEMORY_IMPL_AUTO = 0,
    MEMORY_IMPL_BRAM,
    MEMORY_IMPL_LUTRAM,
    MEMORY_IMPL_URAM,
    MEMORY_IMPL_SRL,
    MEMORY_IMPL_BRAM_ECC,
    MEMORY_IMPL_URAM_ECC,
    // for xpm_memory
    MEMORY_IMPL_BLOCK,
    MEMORY_IMPL_DISTRIBUTE,
    // for fifo
    MEMORY_IMPL_MEMORY,
    MEMORY_NOIMPL // for NOIMPL
};


class CoreBasic
{
public:
    CoreBasic(PlatformBasic::OP_TYPE op, PlatformBasic::IMPL_TYPE impl,
            int maxLat, int minLat, std::string name, bool isPublic);
    ~CoreBasic() {}

public:
    PlatformBasic::OP_TYPE getOp() const { return mOp; }

    PlatformBasic::IMPL_TYPE getImpl() const { return mImpl; }

    std::pair<PlatformBasic::OP_TYPE, PlatformBasic::IMPL_TYPE> getOpAndImpl() const {return std::make_pair(mOp, mImpl);}

    int getMaxLatency() const { return mMaxLatency; }
    int getMinLatency() const { return mMinLatency; }
    std::pair<int, int> getLatencyRange() const
    {
        return std::make_pair(mMinLatency, mMaxLatency);
    }

  bool isValidLatency(int latency) const
  {
      if(latency < 0) return true;
      return latency >= getMinLatency() && latency <= getMaxLatency();
  }

    std::string getName() const { return mName; }

    bool isPublic() const { return mIsPublic; }

  std::string getMetadata() const {return mOp == OP_VIVADO_IP ? "parameterizable" : ""; }

  MEMORY_TYPE getMemoryType() const { return getMemoryTypeImpl().first; };
  MEMORY_IMPL getMemoryImpl() const { return getMemoryTypeImpl().second; };
  // get memory_type, memory_impl, return (-1, -1) if this is not memory
  std::pair<MEMORY_TYPE, MEMORY_IMPL> getMemoryTypeImpl() const;
  static std::pair<MEMORY_TYPE, MEMORY_IMPL> getMemoryTypeImpl(PlatformBasic::IMPL_TYPE impl);

  // Is it a ECC RAM?
  bool isEccRAM() const;
  // Does storage support Byte Write Enable (For FE)
  bool supportByteEnable() const;
  // Is storage initializable (For FE)
  bool isInitializable() const;

  bool isInitializableByAllZeros() const { return true; }

private:
  // is RAM ?
  bool isRAM() const;
  // is ROM ?
  bool isROM() const;

private:
  CoreBasic(); // private and not implemented
    bool mIsPublic;
    PlatformBasic::OP_TYPE mOp;
    PlatformBasic::IMPL_TYPE mImpl;
    int mMaxLatency;
    int mMinLatency;
    std::string mName;

};
public:
    ~PlatformBasic();

// for FE
public:
    static const PlatformBasic* getInstance();

    /*
    @brief for old resource pragma, convert to new resource pragma OP_TYPE, IMPL_TYPE
    @param coreName, user specified core name string
    @return a vector of pairs of OP_TYPE and IMPL_TYPE, empty vector if coreName is unsupported
    */
    std::vector<std::pair<OP_TYPE, IMPL_TYPE>> getOpImplFromCoreName(const std::string& coreName) const;

    /*
    @brief for new resource pragma: bind_op
    @param opStr, user specified op
    @param implStr, user specified impl, if not specified, pass empty string ""
    @return a pair of enum OP_TYPE and enum IMPL_TYPE, <OP_UNSUPPORTED, UNSUPPORTED> if opStr or implStr is unsupported
    */
    std::pair<OP_TYPE, IMPL_TYPE> verifyBindOp(const std::string& opStr, const std::string& implStr) const;

    /*
    @brief for new resource pragma: bind_storage
    @param typeStr, user specified memory type 
    @param implStr, user specified memory implemention, if not specified, pass empty string ""
    @return a pair of enum OP_TYPE and enum IMPL_TYPE, <OP_UNSUPPORTED, UNSUPPORTED> if opStr or implStr is unsupported
    */
    std::pair<OP_TYPE, IMPL_TYPE> verifyBindStorage(const std::string& typeStr, const std::string& implStr) const;

    /*
    @brief for new interface pragma option: storage_type
    @param typeStr, user specified memory type 
    @param coreEnums, if not null, wil be set to OP_TYPE and IMPL_TYPE values that should be passed to BE
    @return true if typeStr is valid otherwise false
    */
    bool verifyInterfaceStorage(std::string typeStr, std::pair<OP_TYPE, IMPL_TYPE> * coreEnums) const;

    /*
    @brief check latency range
    @param op, impl, a pair of op and impl from above functions
    @return a pair of latency bound <minLat, maxLat>
    */
    std::pair<int, int> verifyLatency(OP_TYPE op, IMPL_TYPE impl) const;
    
    /*
    @brief latency validity check
    @param op, impl, a pair of op and impl from above functions
    @param implRange will be set if only a single range is available otherwise -1,-1
    @return true if latency is valid
    */
    bool isValidLatency(OP_TYPE op, IMPL_TYPE impl, int latency) const;
    bool isValidLatency(OP_TYPE op, IMPL_TYPE impl, int latency, std::pair<int, int> & implRange) const;

// FIXME, for FE to support old code, will be delete in future
    std::vector<CoreBasic*> getCoreFromName(const std::string& coreName) const;

      CoreBasic* getCoreFromOpImpl(const std::string& op, const std::string& impl) const;

// enum-string converter
public:
    /// return op string, empty if op is invalid
    std::string getOpName(OP_TYPE op) const;
    /// return impl string, empty if impl is invalid
    std::string getImplName(IMPL_TYPE impl) const;
    /// return op type from the op string, -1 if opName is invalid
    PlatformBasic::OP_TYPE getOpFromName(const std::string& opName) const;
    /// return impl type from the impl string, -1 if implName is invalid
    PlatformBasic::IMPL_TYPE getImplFromName(const std::string& implName) const;

    /// memory type/impl enum-string converter
    static MEMORY_TYPE getMemTypeEnum(IMPL_TYPE impl);
    static MEMORY_IMPL getMemImplEnum(IMPL_TYPE impl);
    std::string getMemTypeName(MEMORY_TYPE type) const;
    std::string getMemImplName(MEMORY_IMPL impl) const;
    MEMORY_TYPE getMemTypeFromName(const std::string& memTypeName) const;
    MEMORY_IMPL getMemImplFromName(const std::string& memImplName) const;

    static bool isMemoryOp(OP_TYPE op);  
      static bool isNOIMPL(IMPL_TYPE impl) { return impl >= NOIMPL; }
// for PF
friend class CoreInstFactory;
friend class ConfigedLib;
private:
    /// return all valid coreBasics that match the pragma
    std::vector<CoreBasic*> getPragmaCoreBasics(OP_TYPE op, IMPL_TYPE impl, int latency) const;
    CoreBasic* getMemoryFromTypeImpl(MEMORY_TYPE type, MEMORY_IMPL impl) const;

// for TCL command: list_op/list_storage
public:
    std::vector<CoreBasic*> getAllCores() const;

// for src/shared/hls/syn/hw/synutil/SsdmNodes.cpp getMetadata()
public:
    /// get core from op+impl enum, null if op+immpl is invalid
    CoreBasic* getCoreFromOpImpl(OP_TYPE op, IMPL_TYPE impl) const;
private:
    
    bool isPublicOp(OP_TYPE op) const;
    bool isPublicType(MEMORY_TYPE type) const;
    /// get all available cores from op
    std::vector<CoreBasic*> getCoresFromOp(const std::string& op) const;
    /// get all available cores from memory type
    std::vector<CoreBasic*> getCoresFromMemoryType(const std::string& type) const;

    /// for @Ethan's CL 2761400
    CoreBasic* getMemoryFromOpImpl(const std::string& type, const std::string& impl) const;
    PlatformBasic::IMPL_TYPE getMemoryImpl(const std::string& type, const std::string& implName) const;

public:
    static std::string getAutoStr();
    static std::string getAllStr();
    static std::string getFifoStr();
    static std::string getAutoSrlStr();

    
    static void getAllConfigOpNames(std::vector<std::string> & names);
    static void getAllConfigOpImpls(std::vector<std::string> & impls);

    static void getAllConfigStorageTypes(std::vector<std::string> & names);
    static void getAllConfigStorageImpls(std::vector<std::string> & impls);

    static void getAllBindOpNames(std::vector<std::string> & names);
    static void getAllBindOpImpls(std::vector<std::string> & impls);

    static void getAllBindStorageTypes(std::vector<std::string> & names);
    static void getAllBindStorageImpls(std::vector<std::string> & impls);

    static void getAllInterfaceStorageTypes(std::vector<std::string> & names);

    static void getOpNameImpls(std::string opName, std::vector<std::string> & impls, bool forBind);
    static void getStorageTypeImpls(std::string storageType, std::vector<std::string> & impls, bool forBind);

    static std::string getOpNameDescription(std::string opName);
    static std::string getOpNameGroup(std::string opName);
    static std::string getOpImplDescription(std::string impl);
    static std::string getStorageTypeDescription(std::string storageType);
    static std::string getStorageImplDescription(std::string impl);

    static CoreBasic* getPublicCore(std::string name, std::string impl, bool isStorage);

private:
    PlatformBasic();

      std::map<int, PlatformBasic::CoreBasic*> createCoreMap();

      void createOpEnum2Str();
      void createOpStr2Enum();
      void createImplEnum2Str();
      void createImplStr2Enum();

    void createMemoryTypeConverter();
    void createMemoryImplConverter();

    std::map<int, CoreBasic*> mCoreMap;

    std::map<PlatformBasic::OP_TYPE, std::string> sOpEnum2Str;
    std::map<std::string, PlatformBasic::OP_TYPE> sOpStr2Enum;
    std::map<PlatformBasic::IMPL_TYPE, std::string> sImplEnum2Str;
    std::map<std::string, PlatformBasic::IMPL_TYPE> sImplStr2Enum;
    std::map<std::string, std::vector<CoreBasic*>> sNameMap; //< name to core for old pragma

    std::map<MEMORY_TYPE, std::string> mMemTypeEnum2Str;
    std::map<std::string, MEMORY_TYPE> mMemTypeStr2Enum;
    std::map<MEMORY_IMPL, std::string> mMemImplEnum2Str;
    std::map<std::string, MEMORY_IMPL> mMemImplStr2Enum;

}; //< class PlatformBasic


}//< namepsace platform

#endif
