
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

#if XILINX_HLS_FE_STANDALONE
#include "llvm/Support/XILINXFPGAPlatformBasic.h"
#else
#include "XILINXFPGAPlatformBasic.h"
#endif

#include <iostream>
#include <algorithm>
#include <map>
#include <limits>


// WARNING: GRPSIZE must > IMPL_MAX
#define GRPSIZE 1000 

#define STR_ALL     "all"
#define STR_AUTO    "auto"
#define STR_FABRIC  "fabric"
#define STR_DSP     "dsp"
#define STR_FIFO    "fifo"
#define STR_AUTOSRL "autosrl"
#define STR_MEMORY  "memory"

namespace 
{
std::string getLowerString(std::string str)
{
  std::transform(str.begin(), str.end(), str.begin(), ::tolower);
  return str;
}
bool iequals(const std::string& a, const std::string& b)
{
    unsigned int sz = a.size();
    if (b.size() != sz)
      return false;
    for (unsigned int i = 0; i < sz; ++i)
      if (tolower(a[i]) != tolower(b[i]))
        return false;
    return true;
}

class PFUserControl 
{
public:
  PFUserControl() { init(); }

  const std::vector<std::string> & getAllNames(bool isStorage) const
  {
    return isStorage ? m_storage.getKeys() : m_ops.getKeys();
  }
  const std::vector<std::string> & getAllImpls(bool isStorage) const
  {
    return isStorage ? m_storageimpls.getKeys() : m_opimpls.getKeys();
  }

  std::string getGroup(std::string name) const
  {
    return m_opgroups.getVal(name, false/*defaultIsKey*/);
  }
  std::string getDesc(std::string name, bool isStorage) const
  {
    return isStorage ? m_storage.getVal(name) : m_ops.getVal(name);
  }
  std::string getImplDesc(std::string name, bool isStorage) const
  {
    return isStorage ? m_storageimpls.getVal(name) : m_opimpls.getVal(name);
  }

  std::string getCoreImpl(std::string op, std::string impl, bool isStorage) const
  {
    if(iequals(impl, STR_ALL))
        return "";
    if (isStorage)
    {
      if (iequals(impl, STR_AUTOSRL))
        return "";
      return impl;
    }
    op = getLowerString(op);
    auto itor = m_fpdsp.find(op);
    if (itor != m_fpdsp.end())
    { 
      if (iequals(impl, STR_DSP))
      {
        const int val = itor->second;
        if (val & MAXDSP)
          return toString(MAXDSP);
        if (val & FULLDSP)
          return toString(FULLDSP);
        if (val & MEDDSP)
          return toString(MEDDSP);
      }
    }
    return impl;
  }

  bool isFPOp(std::string op) const
  {
    return (m_fpdsp.find(getLowerString(op)) != m_fpdsp.end());
  }

private:
  struct DataPairs 
  {
    const std::vector<std::string> & getKeys() const { return m_vals; }
    std::string getVal(std::string key, bool defaultIsKey = true) const 
    {
      auto itor = m_desc.find(getLowerString(key));
      if (itor != m_desc.end())
        return itor->second;
      return defaultIsKey ? key : "";
    }
    void addpair(const std::string & key, const std::string & val)
    {
      m_vals.push_back(key);
      if (val.size())
      {
        m_desc[key] = val;
      }
    }
  private:
    std::vector<std::string> m_vals;
    std::map<std::string,std::string> m_desc;
  };
  void addop(const std::string & value, const std::string & group, const std::string & desc)
  {
    m_ops.addpair(value, desc);
    m_opgroups.addpair(value, group);
  }
  void addopimpl(const std::string & value, const std::string & desc)
  {
    m_opimpls.addpair(value, desc);
  }
  void addstorage(const std::string & value, const std::string & desc)
  {
    m_storage.addpair(value, desc);
  }
  void addstorageimpl(const std::string & value, const std::string & desc)
  {
    m_storageimpls.addpair(value, desc);
  }

  // Enum used to encode
  enum FPDSP
  {
    FPDSP_UNKNOWN = 0,
    NODSP    = (1<<1),
    FULLDSP  = (1<<2),
    MEDDSP   = (1<<3),
    MAXDSP   = (1<<4),
    PRIMDSP  = (1<<5)
  };

  static std::string toString(FPDSP val)
  {
    switch(val)
    {
      case NODSP:  return STR_FABRIC;
      case FULLDSP: return "fulldsp";
      case MEDDSP:  return "meddsp";
      case MAXDSP:  return "maxdsp";
      case PRIMDSP: return "primitivedsp";
      default: return "";
    }
  }

  void fpdsp(const std::string & op, bool full, bool med, bool max, bool prim, bool nodsp)
  {
    unsigned val = 0;
    if (full)
      val |= FULLDSP;
    if (med)
      val |= MEDDSP;
    if (max)
      val |= MAXDSP;
    if (prim)
      val |= PRIMDSP;
    if (nodsp)
      val |= NODSP;
    m_fpdsp[op] = val;
  }

  void init();

private:
  std::map<std::string, int> m_fpdsp;
  DataPairs m_ops;
  DataPairs m_opgroups;
  DataPairs m_opimpls;
  DataPairs m_storage;
  DataPairs m_storageimpls;
};

void PFUserControl::init()
{
  addstorage(STR_FIFO,      "FIFO");
  addstorage("ram_1p",      "Single-port RAM");
  addstorage("ram_1wnr",    "RAM with 1 write port and n read ports, using n banks internally");
  addstorage("ram_2p",      "Dual-port RAM that allows read operations on one port and both read and write operations on the other port.");
  addstorage("ram_s2p",     "Dual-port RAM that allows read operations on one port and write operations on the other port.");
  addstorage("ram_t2p",     "True dual-port RAM with support for both read and write on both ports with read-first mode");
  addstorage("rom_1p",      "Single-port ROM");
  addstorage("rom_2p",      "Dual-port ROM");
  addstorage("rom_np",      "Multi-port ROM");
  //addstorage("pipo",        "Ping-Pong memory (merged/unmerged is selected by the scheduler automatically)");
  //addstorage("pipo_1bank",  "Merged Ping-Pong memory using 1 bank (producer and consumer limited to 1 port each)");
  //addstorage("pipo_nbank",  "Unmerged Ping-Pong memory using n separate memory banks for depth=n (each process gets as many ports as the memory offers)");
  //addstorage("smem_sync",   "Synchronized shared memory");
  //addstorage("smem_unsync", "Unsynchronized shared memory");

  //addstorageimpl(STR_ALL,  "All implementations");
  //addstorageimpl(STR_AUTO,    "Automatic selection");
  addstorageimpl(STR_AUTOSRL, "Automatic SRL");
  addstorageimpl("bram",      "Block RAM");
  addstorageimpl("bram_ecc",  "Block RAM with ECC mode");
  addstorageimpl("lutram",    "Distributed RAM");
  addstorageimpl("uram",      "Ultra RAM");
  addstorageimpl("uram_ecc",  "Ultra RAM with ECC mode");
  addstorageimpl(STR_MEMORY,  "Generic memory, vivado will choose implementation"); // VIVADO-DETERMINE
  addstorageimpl("srl",       "Shift Register Logic");

  addop("mul",    "integer", "integer multiplication operation");
  addop("add",    "integer", "integer add operation");
  addop("sub",    "integer", "integer subtraction operation");
  //addop("udiv",   "integer", "integer unsigned divide operation");
  //addop("sdiv",   "integer", "integer signed divide operation");
  //addop("srem",   "integer", "integer signed module operation");
  //addop("urem",   "integer", "integer unsigned module operation");
  addop("fadd",   "single", "single precision floating-point add operation");
  addop("fsub",   "single", "single precision floating-point subtraction operation");
  addop("fdiv",   "single", "single precision floating-point divide operation");
  addop("fexp",   "single", "single precision floating-point exponential operation");
  addop("flog",   "single", "single precision floating-point logarithmic operation");
  addop("fmul",   "single", "single precision floating-point multiplication operation");
  addop("frsqrt", "single", "single precision floating-point reciprocal square root operation");
  addop("frecip", "single", "single precision floating-point reciprocal operation");
  addop("fsqrt",  "single", "single precision floating-point square root operation");
  addop("dadd",   "double", "double precision floating-point add operation");
  addop("dsub",   "double", "double precision floating-point subtraction operation");
  addop("ddiv",   "double", "double precision floating-point divide operation");
  addop("dexp",   "double", "double precision floating-point exponential operation");
  addop("dlog",   "double", "double precision floating-point logarithmic operation");
  addop("dmul",   "double", "double precision floating-point multiplication operation");
  addop("drsqrt", "double", "double precision floating-point reciprocal square root operation");
  addop("drecip", "double", "double precision floating-point reciprocal operation");
  addop("dsqrt",  "double", "double precision floating-point square root operation");
  addop("hadd",   "half", "half precision floating-point add operation");
  addop("hsub",   "half", "half precision floating-point subtraction operation");
  addop("hdiv",   "half", "half precision floating-point divide operation");
  addop("hmul",   "half", "half precision floating-point multiplication operation");
  addop("hsqrt",  "half", "half precision floating-point square root operation");

  //addopimpl(STR_ALL,           "All implementations");
  //addopimpl(STR_AUTO,          "Plain RTL implementation");
  addopimpl(STR_DSP,           "Use DSP resources");
  addopimpl(STR_FABRIC,        "Use non-DSP resources");
  addopimpl(toString(MEDDSP),  "Floating Point IP Medium Usage of DSP resources");
  addopimpl(toString(FULLDSP), "Floating Point IP Full Usage of DSP resources");
  addopimpl(toString(MAXDSP),  "Floating Point IP Max Usage of DSP resources");
  addopimpl(toString(PRIMDSP), "Floating Point IP Primitive Usage of DSP resources");

  //       op       full    med    max   prim   nodsp
  fpdsp( "dmul",    true,  true,  true, false,  true );
  fpdsp( "fmul",    true,  true,  true,  true,  true );
  fpdsp( "hmul",    true, false,  true, false,  true );

  fpdsp( "dadd",    true, false, false, false,  true );
  fpdsp( "fadd",    true,  true, false,  true,  true );
  fpdsp( "hadd",    true,  true, false, false,  true );

  fpdsp( "dsub",    true, false, false, false,  true );
  fpdsp( "fsub",    true,  true, false,  true,  true );
  fpdsp( "hsub",    true,  true, false, false,  true );
  
  fpdsp( "ddiv",   false, false, false, false,  true );
  fpdsp( "fdiv",   false, false, false, false,  true );
  fpdsp( "hdiv",   false, false, false, false,  true );

  fpdsp( "dexp",   false,  true, false, false,  true );
  fpdsp( "fexp",   false,  true, false, false,  true );
  fpdsp( "hexp",    true, false, false, false, false );

  fpdsp( "dlog",   false,  true, false, false,  true );
  fpdsp( "flog",   false,  true, false, false,  true );
  fpdsp( "hlog",   false,  true, false, false,  true );

  fpdsp( "dsqrt",  false, false, false, false,  true );
  fpdsp( "fsqrt",  false, false, false, false,  true );
  fpdsp( "hsqrt",  false, false, false, false,  true );

  fpdsp( "drsqrt",  true, false, false, false,  true );
  fpdsp( "frsqrt",  true, false, false, false,  true );
  fpdsp( "hrsqrt", false, false, false, false,  true );

  fpdsp( "drecip",  true, false, false, false,  true );
  fpdsp( "frecip",  true, false, false, false,  true );
  fpdsp( "hrecip", false, false, false, false,  true );
}

const PFUserControl & getUserControlData()
{
  static PFUserControl mydata;
  return mydata;
}

} // end namespace

namespace platform
{

std::string PlatformBasic::getAutoSrlStr() { return STR_AUTOSRL; }
std::string PlatformBasic::getAutoStr() { return STR_AUTO; }
std::string PlatformBasic::getAllStr() { return STR_ALL; }
std::string PlatformBasic::getFifoStr() { return STR_FIFO; }

void PlatformBasic::getAllConfigOpNames(std::vector<std::string> & names)
{
  auto allnames = getUserControlData().getAllNames(false/*isStorage*/);
  for (auto val : allnames) names.push_back(val);
}
void PlatformBasic::getAllBindOpNames(std::vector<std::string> & names)
{
  auto allnames = getUserControlData().getAllNames(false/*isStorage*/);
  for (auto val : allnames) names.push_back(val);
}

void PlatformBasic::getAllConfigStorageTypes(std::vector<std::string> & names)
{
  names.push_back(PlatformBasic::getFifoStr());
}
void PlatformBasic::getAllBindStorageTypes(std::vector<std::string> & names)
{
  auto allnames = getUserControlData().getAllNames(true/*isStorage*/);
  for (auto val : allnames) names.push_back(val);
}
void PlatformBasic::getAllInterfaceStorageTypes(std::vector<std::string> & names)
{
  auto allnames = getUserControlData().getAllNames(true/*isStorage*/);
  for (auto val : allnames) if (val != STR_FIFO) names.push_back(val);
}
void PlatformBasic::getAllBindOpImpls(std::vector<std::string> & impls)
{
  auto allimpls = getUserControlData().getAllImpls(false/*isStorage*/);
  for (auto val : allimpls) impls.push_back(val);
}
void PlatformBasic::getAllConfigOpImpls(std::vector<std::string> & impls)
{
  auto allimpls = getUserControlData().getAllImpls(false/*isStorage*/);
  impls.push_back(STR_ALL);
  for (auto val : allimpls) impls.push_back(val);
}
void PlatformBasic::getAllBindStorageImpls(std::vector<std::string> & impls)
{
  auto allimpls = getUserControlData().getAllImpls(true/*isStorage*/);
  for (auto val : allimpls) impls.push_back(val);
}
void PlatformBasic::getAllConfigStorageImpls(std::vector<std::string> & impls)
{
  auto allimpls = getUserControlData().getAllImpls(true/*isStorage*/);
  for (auto val : allimpls) impls.push_back(val);
}

std::string PlatformBasic::getOpNameDescription(std::string name)
{
  return getUserControlData().getDesc(name, false/*isStorage*/);
}
std::string PlatformBasic::getOpImplDescription(std::string impl)
{
  return getUserControlData().getImplDesc(impl, false/*isStorage*/);
}
std::string PlatformBasic::getStorageTypeDescription(std::string name)
{
  return getUserControlData().getDesc(name, true/*isStorage*/);
}
std::string PlatformBasic::getStorageImplDescription(std::string impl)
{
  return getUserControlData().getImplDesc(impl, true/*isStorage*/);
}
std::string PlatformBasic::getOpNameGroup(std::string name)
{
  return getUserControlData().getGroup(name);
}

void PlatformBasic::getOpNameImpls(std::string opName, std::vector<std::string> & impls, bool forBind)
{
  std::vector<std::string> allimpls;
  if (forBind)
  {
    getAllBindOpImpls(allimpls);
  }
  else
  {
    getAllConfigOpImpls(allimpls);
  }
  for (auto impl : allimpls)
  {
    if (impl == STR_ALL || PlatformBasic::getPublicCore(opName, impl, false/*isStorage*/))
    {
      impls.push_back(impl);
    }
  }
}

void PlatformBasic::getStorageTypeImpls(std::string storageType, std::vector<std::string> & impls, bool forBind)
{
  std::vector<std::string> allimpls;
  if (forBind)
    getAllBindStorageImpls(allimpls);
  else
    getAllConfigStorageImpls(allimpls);
  for (auto impl : allimpls)
  {
    if (impl == STR_AUTOSRL || PlatformBasic::getPublicCore(storageType, impl, true/*isStorage*/))
    {
      impls.push_back(impl);
    }
  }
}

PlatformBasic::CoreBasic* PlatformBasic::getMemoryFromOpImpl(const std::string& type, const std::string& impl) const
{
    auto implCode = getMemoryImpl(type, impl);
    return getCoreFromOpImpl(OP_MEMORY, implCode);
}

PlatformBasic::IMPL_TYPE PlatformBasic::getMemoryImpl(const std::string& type, const std::string& implName) const
{
    IMPL_TYPE impl = UNSUPPORTED;
    std::string name = getLowerString(type);
    if (implName.size())
    {
        name += "_" + getLowerString(implName);
    }
    auto it = sImplStr2Enum.find(name);
    if(it != sImplStr2Enum.end())
  {
      impl = it->second;
  }
  return impl;
}

PlatformBasic::CoreBasic* PlatformBasic::getPublicCore(std::string name, std::string impl, bool isStorage)
{
  PlatformBasic::CoreBasic * coreBasic = nullptr;
  std::string coreimpl = getUserControlData().getCoreImpl(name, impl, isStorage);
  if (isStorage)
  {
    coreBasic = getInstance()->getMemoryFromOpImpl(name, coreimpl);
    // TODO: cleanup mapping of core AUTO
    if (!coreBasic && iequals(coreimpl, STR_AUTO))
    {
      coreBasic = getInstance()->getMemoryFromOpImpl(name, "");
    }
  }
  else 
  {
    coreBasic = getInstance()->getCoreFromOpImpl(name, coreimpl);
  }

  if (coreBasic && !coreBasic->isPublic())
  {
    coreBasic = nullptr;
  }
  return coreBasic;
}

template<class T>
void createEnumStrConverter(std::map<T, std::string>& enum2Str,
                            std::map<std::string, T>& str2Enum,
                            T enumValue, std::string str)
{
    enum2Str[enumValue] = str;
    str2Enum[str] = enumValue;
}

/** basic information of a core **/
/// create a map of all available cores, <op+impl, core>
std::map<int, PlatformBasic::CoreBasic*> PlatformBasic::createCoreMap()
{
    sNameMap.clear();

    std::map<int, PlatformBasic::CoreBasic*> coreMap;
    PlatformBasic::CoreBasic* core = NULL;
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::FABRIC, 4, 0, "Adder", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["adder"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::FABRIC, 4, 0, "Adder", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["adder"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::FABRIC_COMB, 0, 0, "AddSub", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["addsub"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::FABRIC_COMB, 0, 0, "AddSub", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["addsub"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::FABRIC_SEQ, 3, 1, "AddSubnS", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["addsubns"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::FABRIC_SEQ, 3, 1, "AddSubnS", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["addsubns"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::DSP, 0, 0, "AddSub_DSP", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["addsub_dsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::DSP, 0, 0, "AddSub_DSP", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["addsub_dsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ALL, PlatformBasic::QADDER, 0, 0, "QAddSub_DSP", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["qaddsub_dsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::QADDER, 0, 0, "QAddSub_DSP", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["qaddsub_dsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::QADDER, 0, 0, "QAddSub_DSP", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["qaddsub_dsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ALL, PlatformBasic::TADDER, 0, 0, "TAddSub", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["taddsub"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::TADDER, 0, 0, "TAddSub", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["taddsub"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::TADDER, 0, 0, "TAddSub", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["taddsub"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_UDIV, PlatformBasic::AUTO, -1, -1, "Divider", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["divns"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SDIV, PlatformBasic::AUTO, -1, -1, "Divider", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["divns"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_UREM, PlatformBasic::AUTO, -1, -1, "Divider", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["divns"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SREM, PlatformBasic::AUTO, -1, -1, "Divider", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["divns"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_UDIV, PlatformBasic::AUTO_SEQ, -1, -1, "DivnS_SEQ", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["divns_seq"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SDIV, PlatformBasic::AUTO_SEQ, -1, -1, "DivnS_SEQ", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["divns_seq"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_UREM, PlatformBasic::AUTO_SEQ, -1, -1, "DivnS_SEQ", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["divns_seq"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SREM, PlatformBasic::AUTO_SEQ, -1, -1, "DivnS_SEQ", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["divns_seq"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_UDIV, PlatformBasic::VIVADO_DIVIDER, -1, -1, "Divider_IP", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["divider_ip"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SDIV, PlatformBasic::VIVADO_DIVIDER, -1, -1, "Divider_IP", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["divider_ip"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_UREM, PlatformBasic::VIVADO_DIVIDER, -1, -1, "Divider_IP", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["divider_ip"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SREM, PlatformBasic::VIVADO_DIVIDER, -1, -1, "Divider_IP", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["divider_ip"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::AUTO, 4, 0, "Multiplier", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["multiplier"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::AUTO_COMB, 0, 0, "Mul", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mul"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::AUTO_PIPE, 4, 1, "MulnS", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mulns"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::FABRIC, 4, 0, "Mul_LUT", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mul_lut"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::DSP, 4, 0, "Mul_DSP", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mul_dsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::AUTO_2STAGE, 1, 1, "Mul2S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mul2s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::AUTO_3STAGE, 2, 2, "Mul3S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mul3s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::AUTO_4STAGE, 3, 3, "Mul4S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mul4s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::AUTO_5STAGE, 4, 4, "Mul5S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mul5s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::AUTO_6STAGE, 5, 5, "Mul6S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mul6s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_CMUL, PlatformBasic::AUTO, 4, 0, "CMult", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["cmult"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ICMP, PlatformBasic::AUTO, 0, 0, "Cmp", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["cmp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SELECT, PlatformBasic::AUTO_SEL, 0, 0, "Sel", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["sel"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SELECT, PlatformBasic::AUTO_MUX, 0, 0, "Mux", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mux"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUX, PlatformBasic::AUTO, 12, 0, "Multiplexer", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["muxns"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_AND, PlatformBasic::AUTO, 0, 0, "LogicGate", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["logicgate"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_OR, PlatformBasic::AUTO, 0, 0, "LogicGate", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["logicgate"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_XOR, PlatformBasic::AUTO, 0, 0, "LogicGate", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["logicgate"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SHL, PlatformBasic::AUTO_PIPE, 6, 0, "Shifter", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["shiftns"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_LSHR, PlatformBasic::AUTO_PIPE, 6, 0, "Shifter", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["shiftns"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ASHR, PlatformBasic::AUTO_PIPE, 6, 0, "Shifter", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["shiftns"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ALL, PlatformBasic::DSP48, 4, 0, "DSP48", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dsp48"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::DSP48, 4, 0, "DSP48", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dsp48"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::DSP48, 4, 0, "DSP48", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dsp48"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::DSP48, 4, 0, "DSP48", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dsp48"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SELECT, PlatformBasic::DSP48, 4, 0, "DSP48", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dsp48"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MAC16_CLOCKX2, PlatformBasic::AUTO, 3, 3, "DSP_Double_Pump_Mac16", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dsp_double_pump_mac16"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MAC6_MAC8_CLOCKX2, PlatformBasic::AUTO, 3, 3, "DSP_Double_Pump_Mac8", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dsp_double_pump_mac8"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DSP, PlatformBasic::AUTO, 4, 0, "DSP_Macro", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dsp_macro"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DSP_AB, PlatformBasic::AUTO, 4, 0, "DSP_Macro", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dsp_macro"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ALL, PlatformBasic::DSP_AM, 0, 0, "AM", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["am"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::DSP_AM, 0, 0, "AM", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["am"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::DSP_AM, 0, 0, "AM", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["am"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::DSP_AM, 0, 0, "AM", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["am"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ALL, PlatformBasic::DSP_AM_2STAGE, 1, 1, "AM2S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["am2s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::DSP_AM_2STAGE, 1, 1, "AM2S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["am2s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::DSP_AM_2STAGE, 1, 1, "AM2S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["am2s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::DSP_AM_2STAGE, 1, 1, "AM2S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["am2s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ALL, PlatformBasic::DSP_AM_3STAGE, 2, 2, "AM3S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["am3s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::DSP_AM_3STAGE, 2, 2, "AM3S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["am3s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::DSP_AM_3STAGE, 2, 2, "AM3S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["am3s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::DSP_AM_3STAGE, 2, 2, "AM3S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["am3s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ALL, PlatformBasic::DSP_AM_4STAGE, 3, 3, "AM4S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["am4s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::DSP_AM_4STAGE, 3, 3, "AM4S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["am4s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::DSP_AM_4STAGE, 3, 3, "AM4S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["am4s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::DSP_AM_4STAGE, 3, 3, "AM4S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["am4s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ALL, PlatformBasic::DSP_MAC, 0, 0, "MAC", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mac"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::DSP_MAC, 0, 0, "MAC", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mac"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::DSP_MAC, 0, 0, "MAC", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mac"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::DSP_MAC, 0, 0, "MAC", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mac"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ALL, PlatformBasic::DSP_MAC_2STAGE, 1, 1, "MAC2S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mac2s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::DSP_MAC_2STAGE, 1, 1, "MAC2S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mac2s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::DSP_MAC_2STAGE, 1, 1, "MAC2S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mac2s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::DSP_MAC_2STAGE, 1, 1, "MAC2S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mac2s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ALL, PlatformBasic::DSP_MAC_3STAGE, 2, 2, "MAC3S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mac3s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::DSP_MAC_3STAGE, 2, 2, "MAC3S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mac3s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::DSP_MAC_3STAGE, 2, 2, "MAC3S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mac3s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::DSP_MAC_3STAGE, 2, 2, "MAC3S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mac3s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ALL, PlatformBasic::DSP_MAC_4STAGE, 3, 3, "MAC4S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mac4s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::DSP_MAC_4STAGE, 3, 3, "MAC4S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mac4s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::DSP_MAC_4STAGE, 3, 3, "MAC4S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mac4s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::DSP_MAC_4STAGE, 3, 3, "MAC4S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["mac4s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ALL, PlatformBasic::DSP_AMA, 0, 0, "AMA", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ama"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::DSP_AMA, 0, 0, "AMA", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ama"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::DSP_AMA, 0, 0, "AMA", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ama"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::DSP_AMA, 0, 0, "AMA", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ama"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ALL, PlatformBasic::DSP_AMA_2STAGE, 1, 1, "AMA2S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ama2s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::DSP_AMA_2STAGE, 1, 1, "AMA2S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ama2s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::DSP_AMA_2STAGE, 1, 1, "AMA2S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ama2s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::DSP_AMA_2STAGE, 1, 1, "AMA2S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ama2s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ALL, PlatformBasic::DSP_AMA_3STAGE, 2, 2, "AMA3S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ama3s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::DSP_AMA_3STAGE, 2, 2, "AMA3S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ama3s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::DSP_AMA_3STAGE, 2, 2, "AMA3S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ama3s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::DSP_AMA_3STAGE, 2, 2, "AMA3S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ama3s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ALL, PlatformBasic::DSP_AMA_4STAGE, 3, 3, "AMA4S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ama4s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::DSP_AMA_4STAGE, 3, 3, "AMA4S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ama4s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::DSP_AMA_4STAGE, 3, 3, "AMA4S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ama4s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::DSP_AMA_4STAGE, 3, 3, "AMA4S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ama4s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ALL, PlatformBasic::DSP58_DP, 4, 0, "DSP58_DotProduct", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dsp58_dotproduct"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MUL, PlatformBasic::DSP58_DP, 4, 0, "DSP58_DotProduct", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dsp58_dotproduct"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADD, PlatformBasic::DSP58_DP, 4, 0, "DSP58_DotProduct", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dsp58_dotproduct"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SUB, PlatformBasic::DSP58_DP, 4, 0, "DSP58_DotProduct", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dsp58_dotproduct"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_CALL, PlatformBasic::AUTO, std::numeric_limits<int>::max(), 0, "BlackBox", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["blackbox"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_BLACKBOX, PlatformBasic::AUTO, std::numeric_limits<int>::max(), 0, "BlackBox", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["blackbox"].push_back(core);

    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FACC, PlatformBasic::FABRIC, 17, 1, "FAcc_nodsp", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["facc_nodsp"].push_back(core);

    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FACC, PlatformBasic::MEDDSP, 22, 1, "FAcc_meddsp", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["facc_meddsp"].push_back(core);

    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FACC, PlatformBasic::FULLDSP, 23, 1, "FAcc_fulldsp", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["facc_fulldsp"].push_back(core);

    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FACC, PlatformBasic::PRIMITIVEDSP, 2, 1, "FAcc_primitivedsp", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["facc_primitivedsp"].push_back(core);

    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DACC, PlatformBasic::FABRIC, 17, 2, "DAcc_nodsp", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dacc_nodsp"].push_back(core);

    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DACC, PlatformBasic::MEDDSP, 22, 2, "DAcc_meddsp", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dacc_meddsp"].push_back(core);

    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DACC, PlatformBasic::FULLDSP, 24, 2, "DAcc_fulldsp", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dacc_fulldsp"].push_back(core);

    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FMAC, PlatformBasic::PRIMITIVEDSP, 4, 1, "FMac_primitivedsp", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["fmac_primitivedsp"].push_back(core);

    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FADD, PlatformBasic::FABRIC, 13, 0, "FAddSub_nodsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["faddsub_nodsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FSUB, PlatformBasic::FABRIC, 13, 0, "FAddSub_nodsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["faddsub_nodsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FADD, PlatformBasic::FULLDSP, 12, 0, "FAddSub_fulldsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["faddsub_fulldsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FSUB, PlatformBasic::FULLDSP, 12, 0, "FAddSub_fulldsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["faddsub_fulldsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FADD, PlatformBasic::PRIMITIVEDSP, 3, 0, "FAddSub_primitivedsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["faddsub_primitivedsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FSUB, PlatformBasic::PRIMITIVEDSP, 3, 0, "FAddSub_primitivedsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["faddsub_primitivedsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FMUL, PlatformBasic::FABRIC, 9, 0, "FMul_nodsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["fmul_nodsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FMUL, PlatformBasic::MEDDSP, 9, 0, "FMul_meddsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["fmul_meddsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FMUL, PlatformBasic::FULLDSP, 9, 0, "FMul_fulldsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["fmul_fulldsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FMUL, PlatformBasic::MAXDSP, 7, 0, "FMul_maxdsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["fmul_maxdsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FMUL, PlatformBasic::PRIMITIVEDSP, 4, 0, "FMul_primitivedsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["fmul_primitivedsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FDIV, PlatformBasic::FABRIC, 29, 0, "FDiv", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["fdiv"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FSQRT, PlatformBasic::FABRIC, 29, 0, "FSqrt", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["fsqrt"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FRSQRT, PlatformBasic::FABRIC, 38, 0, "FRSqrt_nodsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["frsqrt_nodsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FRSQRT, PlatformBasic::FULLDSP, 33, 0, "FRSqrt_fulldsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["frsqrt_fulldsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FRECIP, PlatformBasic::FABRIC, 37, 0, "FRecip_nodsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["frecip_nodsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FRECIP, PlatformBasic::FULLDSP, 30, 0, "FRecip_fulldsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["frecip_fulldsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FEXP, PlatformBasic::FABRIC, 24, 0, "FExp_nodsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["fexp_nodsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FEXP, PlatformBasic::MEDDSP, 21, 0, "FExp_meddsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["fexp_meddsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FEXP, PlatformBasic::FULLDSP, 30, 0, "FExp_fulldsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["fexp_fulldsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FLOG, PlatformBasic::FABRIC, 24, 0, "FLog_nodsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["flog_nodsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FLOG, PlatformBasic::MEDDSP, 23, 0, "FLog_meddsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["flog_meddsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FLOG, PlatformBasic::FULLDSP, 29, 0, "FLog_fulldsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["flog_fulldsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FCMP, PlatformBasic::AUTO, 9, 0, "FCompare", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["fcompare"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DADD, PlatformBasic::FABRIC, 13, 0, "DAddSub_nodsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["daddsub_nodsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DSUB, PlatformBasic::FABRIC, 13, 0, "DAddSub_nodsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["daddsub_nodsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DADD, PlatformBasic::FULLDSP, 15, 0, "DAddSub_fulldsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["daddsub_fulldsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DSUB, PlatformBasic::FULLDSP, 15, 0, "DAddSub_fulldsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["daddsub_fulldsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DMUL, PlatformBasic::FABRIC, 10, 0, "DMul_nodsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dmul_nodsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DMUL, PlatformBasic::MEDDSP, 13, 0, "DMul_meddsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dmul_meddsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DMUL, PlatformBasic::FULLDSP, 13, 0, "DMul_fulldsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dmul_fulldsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DMUL, PlatformBasic::MAXDSP, 14, 0, "DMul_maxdsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dmul_maxdsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DDIV, PlatformBasic::FABRIC, 58, 0, "DDiv", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ddiv"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DSQRT, PlatformBasic::FABRIC, 58, 0, "DSqrt", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dsqrt"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DRSQRT, PlatformBasic::FULLDSP, 111, 0, "DRSqrt", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["drsqrt"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DRECIP, PlatformBasic::FULLDSP, 36, 0, "DRecip", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["drecip"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DEXP, PlatformBasic::FABRIC, 40, 0, "DExp_nodsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dexp_nodsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DEXP, PlatformBasic::MEDDSP, 45, 0, "DExp_meddsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dexp_meddsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DEXP, PlatformBasic::FULLDSP, 57, 0, "DExp_fulldsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dexp_fulldsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DLOG, PlatformBasic::FABRIC, 38, 0, "DLog_nodsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dlog_nodsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DLOG, PlatformBasic::MEDDSP, 49, 0, "DLog_meddsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dlog_meddsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DLOG, PlatformBasic::FULLDSP, 65, 0, "DLog_fulldsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dlog_fulldsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DCMP, PlatformBasic::AUTO, 9, 0, "DCompare", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dcompare"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_HADD, PlatformBasic::FULLDSP, 12, 0, "HAddSub_fulldsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["haddsub_fulldsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_HSUB, PlatformBasic::FULLDSP, 12, 0, "HAddSub_fulldsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["haddsub_fulldsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_HADD, PlatformBasic::MEDDSP, 12, 0, "HAddSub_meddsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["haddsub_meddsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_HSUB, PlatformBasic::MEDDSP, 12, 0, "HAddSub_meddsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["haddsub_meddsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_HADD, PlatformBasic::FABRIC, 9, 0, "HAddSub_nodsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["haddsub_nodsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_HSUB, PlatformBasic::FABRIC, 9, 0, "HAddSub_nodsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["haddsub_nodsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_HMUL, PlatformBasic::FABRIC, 7, 0, "HMul_nodsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["hmul_nodsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_HMUL, PlatformBasic::FULLDSP, 7, 0, "HMul_fulldsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["hmul_fulldsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_HMUL, PlatformBasic::MAXDSP, 9, 0, "HMul_maxdsp", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["hmul_maxdsp"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_HDIV, PlatformBasic::FABRIC, 16, 0, "HDiv", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["hdiv"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_HSQRT, PlatformBasic::FABRIC, 16, 0, "HSqrt", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["hsqrt"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_HCMP, PlatformBasic::AUTO, 9, 0, "HCompare", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["hcompare"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_HPTOSP, PlatformBasic::AUTO, 9, 0, "Half2Float", false);
    coreMap[core->getOp() * 1000 + core->getImpl()] = core;
    sNameMap["half2float"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_HPTODP, PlatformBasic::AUTO, 9, 0, "Half2Double", false);
    coreMap[core->getOp() * 1000 + core->getImpl()] = core;
    sNameMap["half2double"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SPTOHP, PlatformBasic::AUTO, 10, 0, "Float2Half", false);
    coreMap[core->getOp() * 1000 + core->getImpl()] = core;
    sNameMap["float2half"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DPTOHP, PlatformBasic::AUTO, 10, 0, "Double2Half", false);
    coreMap[core->getOp() * 1000 + core->getImpl()] = core;
    sNameMap["double2half"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SITOFP, PlatformBasic::AUTO, 13, 0, "Int2Float", false);
    coreMap[core->getOp() * 1000 + core->getImpl()] = core;
    sNameMap["int2float"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_UITOFP, PlatformBasic::AUTO, 13, 0, "Int2Float", false);
    coreMap[core->getOp() * 1000 + core->getImpl()] = core;
    sNameMap["int2float"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_SITODP, PlatformBasic::AUTO, 13, 0, "Int2Double", false);
    coreMap[core->getOp() * 1000 + core->getImpl()] = core;
    sNameMap["int2double"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_UITODP, PlatformBasic::AUTO, 13, 0, "Int2Double", false);
    coreMap[core->getOp() * 1000 + core->getImpl()] = core;
    sNameMap["int2double"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FPTOSI, PlatformBasic::AUTO, 13, 0, "Float2Int", false);
    coreMap[core->getOp() * 1000 + core->getImpl()] = core;
    sNameMap["float2int"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FPTOUI, PlatformBasic::AUTO, 13, 0, "Float2Int", false);
    coreMap[core->getOp() * 1000 + core->getImpl()] = core;
    sNameMap["float2int"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DPTOSI, PlatformBasic::AUTO, 13, 0, "Double2Int", false);
    coreMap[core->getOp() * 1000 + core->getImpl()] = core;
    sNameMap["double2int"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_DPTOUI, PlatformBasic::AUTO, 13, 0, "Double2Int", false);
    coreMap[core->getOp() * 1000 + core->getImpl()] = core;
    sNameMap["double2int"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FPEXT, PlatformBasic::AUTO, 9, 0, "Float2Double", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["float2double"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_FPTRUNC, PlatformBasic::AUTO, 10, 0, "Double2Float", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["double2float"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::SHIFTREG_AUTO, 0, 0, "ShiftReg", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["shiftreg"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::REGISTER_AUTO, 0, 0, "Register", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["register"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::XPM_MEMORY_AUTO, 3, 0, "XPM_MEMORY", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["xpm_memory"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::XPM_MEMORY_DISTRIBUTE, 3, 0, "XPM_MEMORY_Distributed", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["xpm_memory_distributed"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::XPM_MEMORY_BLOCK, 3, 0, "XPM_MEMORY_Block", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["xpm_memory_block"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::XPM_MEMORY_URAM, 3, 0, "XPM_MEMORY_Ultra", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["xpm_memory_ultra"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::FIFO_MEMORY, -1, -1, "FIFO", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["fifo"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::FIFO_SRL, -1, -1, "FIFO_SRL", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["fifo_srl"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::FIFO_BRAM, -1, -1, "FIFO_BRAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["fifo_bram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::FIFO_URAM, -1, -1, "FIFO_URAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["fifo_uram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::FIFO_LUTRAM, -1, -1, "FIFO_LUTRAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["fifo_lutram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_AUTO, 9, 1, "RAM", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_1WNR, 3, 1, "RAM_1WnR", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_1wnr"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_1WNR_LUTRAM, 3, 1, "RAM_1WnR_LUTRAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_1wnr_lutram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_1WNR_BRAM, 3, 1, "RAM_1WnR_BRAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_1wnr_bram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_1WNR_URAM, 3, 1, "RAM_1WnR_URAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_1wnr_uram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_1P_AUTO, 3, 1, "RAM_1P", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_1p"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_1P_LUTRAM, 3, 1, "RAM_1P_LUTRAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_1p_lutram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_1P_BRAM, 3, 1, "RAM_1P_BRAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_1p_bram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_1P_URAM, 3, 1, "RAM_1P_URAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_1p_uram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_2P_AUTO, 3, 1, "RAM_2P", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_2p"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_2P_LUTRAM, 3, 1, "RAM_2P_LUTRAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_2p_lutram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_2P_BRAM, 3, 1, "RAM_2P_BRAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_2p_bram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_2P_URAM, 3, 1, "RAM_2P_URAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_2p_uram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_T2P_BRAM, 3, 1, "RAM_T2P_BRAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_t2p_bram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_T2P_URAM, 3, 1, "RAM_T2P_URAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_t2p_uram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_S2P_LUTRAM, 3, 1, "RAM_S2P_LUTRAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_s2p_lutram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_S2P_BRAM, 3, 1, "RAM_S2P_BRAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_s2p_bram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_S2P_URAM, 3, 1, "RAM_S2P_URAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_s2p_uram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_S2P_BRAM_ECC, 3, 1, "RAM_S2P_BRAM_ECC", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_s2p_bram_ecc"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_S2P_URAM_ECC, 3, 1, "RAM_S2P_URAM_ECC", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_s2p_uram_ecc"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::ROM_AUTO, 8, 1, "ROM", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["rom"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::ROM_1P_AUTO, 3, 1, "ROM_1P", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["rom_1p"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::ROM_1P_LUTRAM, 3, 1, "ROM_1P_LUTRAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["rom_1p_lutram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::ROM_1P_BRAM, 3, 1, "ROM_1P_BRAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["rom_1p_bram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::ROM_1P_1S_AUTO, 0, 0, "ROM_1P_1S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["rom_1p_1s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::ROM_NP_AUTO, 8, 1, "ROM_nP", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["rom_np"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::ROM_NP_LUTRAM, 3, 1, "ROM_nP_LUTRAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["rom_np_lutram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::ROM_NP_BRAM, 3, 1, "ROM_nP_BRAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["rom_np_bram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::ROM_2P_AUTO, 3, 1, "ROM_2P", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["rom_2p"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::ROM_2P_LUTRAM, 3, 1, "ROM_2P_LUTRAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["rom_2p_lutram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::ROM_2P_BRAM, 3, 1, "ROM_2P_BRAM", true);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["rom_2p_bram"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM3S_AUTO, 2, 2, "RAM3S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram3s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM4S_AUTO, 3, 3, "RAM4S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram4s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM5S_AUTO, 4, 4, "RAM5S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram5s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::RAM_COREGEN_AUTO, 8, 1, "RAM_coregen", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["ram_coregen"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::SPRAM_COREGEN_AUTO, 8, 1, "SPRAM_coregen", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["spram_coregen"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::ROM3S_AUTO, 2, 2, "ROM3S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["rom3s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::ROM4S_AUTO, 3, 3, "ROM4S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["rom4s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::ROM5S_AUTO, 4, 4, "ROM5S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["rom5s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::ROM_COREGEN_AUTO, 8, 1, "ROM_coregen", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["rom_coregen"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::SPROM_COREGEN_AUTO, 8, 1, "SPROM_coregen", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["sprom_coregen"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::DPWOM_AUTO, 8, 1, "DPWOM", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["dpwom"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_MEMORY, PlatformBasic::SPWOM_AUTO, 8, 1, "SPWOM", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["spwom"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADAPTER, PlatformBasic::AXI4STREAM, std::numeric_limits<int>::max(), 0, "AXI4Stream", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["axi4stream"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADAPTER, PlatformBasic::AXI4LITES, std::numeric_limits<int>::max(), 0, "AXI4LiteS", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["axi4lites"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADAPTER, PlatformBasic::STREAM_BUNDLE, std::numeric_limits<int>::max(), 0, "STREAM_BUNDLE", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["stream_bundle"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADAPTER, PlatformBasic::AXI4M, 4, 4, "AXI4M", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["axi4m"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADAPTER, PlatformBasic::FSL, std::numeric_limits<int>::max(), 0, "FSL", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["fsl"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADAPTER, PlatformBasic::M_AXI, 6, 3, "m_axi", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["m_axi"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADAPTER, PlatformBasic::PLB46M, std::numeric_limits<int>::max(), 0, "PLB46M", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["plb46m"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADAPTER, PlatformBasic::NPI64M, std::numeric_limits<int>::max(), 0, "NPI64M", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["npi64m"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADAPTER, PlatformBasic::PLB46S, std::numeric_limits<int>::max(), 0, "PLB46S", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["plb46s"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_ADAPTER, PlatformBasic::S_AXILITE, std::numeric_limits<int>::max(), 0, "s_axilite", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["s_axilite"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_VIVADO_IP, PlatformBasic::VIVADO_DDS, 0, 0, "Vivado_DDS", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["vivado_dds"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_VIVADO_IP, PlatformBasic::VIVADO_FFT, std::numeric_limits<int>::max(), 0, "Vivado_FFT", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["vivado_fft"].push_back(core);
    core = new PlatformBasic::CoreBasic(PlatformBasic::OP_VIVADO_IP, PlatformBasic::VIVADO_FIR, std::numeric_limits<int>::max(), 0, "Vivado_FIR", false);
    coreMap[core->getOp() * GRPSIZE + core->getImpl()] = core;
    sNameMap["vivado_fir"].push_back(core);
   
    // alias_core

    sNameMap["divider"] = sNameMap["divns"];
    sNameMap["sdivider"] = sNameMap["divns"];
    sNameMap["udivider"] = sNameMap["divns"];
    sNameMap["comparator"] = sNameMap["cmp"];
    sNameMap["selector"] = sNameMap["sel"];
    sNameMap["shifter"] = sNameMap["shiftns"];
    sNameMap["multiplexer"] = sNameMap["muxns"];
    sNameMap["fifob"] = sNameMap["fifo_bram"];
    sNameMap["fifod"] = sNameMap["fifo_lutram"];
    sNameMap["fifos"] = sNameMap["fifo_srl"];
    sNameMap["true_dpram"] = sNameMap["ram_t2p_bram"];
    sNameMap["dpram"] = sNameMap["ram_2p"];
    sNameMap["dpramd"] = sNameMap["ram_2p_lutram"];
    sNameMap["dpramb"] = sNameMap["ram_2p_bram"];
    sNameMap["spram"] = sNameMap["ram_1p"];
    sNameMap["spramd"] = sNameMap["ram_1p_lutram"];
    sNameMap["spramb"] = sNameMap["ram_1p_bram"];
    sNameMap["dprom"] = sNameMap["rom_2p"];
    sNameMap["dpromd"] = sNameMap["rom_2p_lutram"];
    sNameMap["dpromb"] = sNameMap["rom_2p_bram"];
    sNameMap["sprom"] = sNameMap["rom_1p"];
    sNameMap["spromd"] = sNameMap["rom_1p_lutram"];
    sNameMap["spromb"] = sNameMap["rom_1p_bram"];
    sNameMap["rom_async"] = sNameMap["rom_1p_1s"];
    sNameMap["pipemult3s"] = sNameMap["mul3s"];
    sNameMap["pipemult4s"] = sNameMap["mul4s"];
    sNameMap["pipemult5s"] = sNameMap["mul5s"];
    sNameMap["pipemult6s"] = sNameMap["mul6s"];
    sNameMap["hcmp"] = sNameMap["hcompare"];
    sNameMap["fcmp"] = sNameMap["fcompare"];
    sNameMap["dcmp"] = sNameMap["dcompare"];
    sNameMap["xilaxi4_bus_rw"] = sNameMap["axi4m"];
    sNameMap["axis"] = sNameMap["axi4stream"];
    sNameMap["axi_slave"] = sNameMap["axi4lites"];
    sNameMap["xilplb46_bus_rw"] = sNameMap["plb46m"];
    sNameMap["plb_slave"] = sNameMap["plb46s"];
    sNameMap["npi64"] = sNameMap["npi64m"];
    sNameMap["xpm_memory_distribute"] = sNameMap["xpm_memory_distributed"];
    sNameMap["xpm_memory_uram"] = sNameMap["xpm_memory_ultra"];

    return coreMap;
}

/// create a map of <op_code, op_str>
void PlatformBasic::createOpEnum2Str()
{
    sOpEnum2Str.clear();

    sOpEnum2Str[PlatformBasic::OP_FACC] = "facc";
    sOpEnum2Str[PlatformBasic::OP_DACC] = "dacc";
    sOpEnum2Str[PlatformBasic::OP_FMAC] = "fmac";
    sOpEnum2Str[PlatformBasic::OP_ADAPTER] = "adapter";
    sOpEnum2Str[PlatformBasic::OP_ADD] = "add";
    sOpEnum2Str[PlatformBasic::OP_ADDMUL] = "addmul";
    sOpEnum2Str[PlatformBasic::OP_ADDMUL_SUB] = "addmul_sub";
    sOpEnum2Str[PlatformBasic::OP_ADDMULADD] = "addmuladd";
    sOpEnum2Str[PlatformBasic::OP_ADDMULADDACC] = "addmuladdacc";
    sOpEnum2Str[PlatformBasic::OP_ADDMULADDSEL] = "addmuladdsel";
    sOpEnum2Str[PlatformBasic::OP_ADDMULSUB] = "addmulsub";
    sOpEnum2Str[PlatformBasic::OP_ADDMULSUBACC] = "addmulsubacc";
    sOpEnum2Str[PlatformBasic::OP_ALL] = "all";
    sOpEnum2Str[PlatformBasic::OP_ALLOCA] = "alloca";
    sOpEnum2Str[PlatformBasic::OP_AND] = "and";
    sOpEnum2Str[PlatformBasic::OP_ANDREDUCE] = "andreduce";
    sOpEnum2Str[PlatformBasic::OP_ASHR] = "ashr";
    sOpEnum2Str[PlatformBasic::OP_ATOMICCMPXCHG] = "atomiccmpxchg";
    sOpEnum2Str[PlatformBasic::OP_ATOMICRMW] = "atomicrmw";
    sOpEnum2Str[PlatformBasic::OP_BITCAST] = "bitcast";
    sOpEnum2Str[PlatformBasic::OP_BITCONCATENATE] = "bitconcatenate";
    sOpEnum2Str[PlatformBasic::OP_BITSELECT] = "bitselect";
    sOpEnum2Str[PlatformBasic::OP_BITSET] = "bitset";
    sOpEnum2Str[PlatformBasic::OP_BLACKBOX] = "blackbox";
    sOpEnum2Str[PlatformBasic::OP_BR] = "br";
    sOpEnum2Str[PlatformBasic::OP_CALL] = "call";
    sOpEnum2Str[PlatformBasic::OP_CMUL] = "cmul";
    sOpEnum2Str[PlatformBasic::OP_CTPOP] = "ctpop";
    sOpEnum2Str[PlatformBasic::OP_DADD] = "dadd";
    sOpEnum2Str[PlatformBasic::OP_DCMP] = "dcmp";
    sOpEnum2Str[PlatformBasic::OP_DDIV] = "ddiv";
    sOpEnum2Str[PlatformBasic::OP_DEXP] = "dexp";
    sOpEnum2Str[PlatformBasic::OP_DLOG] = "dlog";
    sOpEnum2Str[PlatformBasic::OP_DMUL] = "dmul";
    sOpEnum2Str[PlatformBasic::OP_DOTPRA3] = "dotpra3";
    sOpEnum2Str[PlatformBasic::OP_DOTPRA3ADD] = "dotpra3add";
    sOpEnum2Str[PlatformBasic::OP_DPTOHP] = "dptohp";
    sOpEnum2Str[PlatformBasic::OP_DPTOSI] = "dptosi";
    sOpEnum2Str[PlatformBasic::OP_DPTOUI] = "dptoui";
    sOpEnum2Str[PlatformBasic::OP_DRECIP] = "drecip";
    sOpEnum2Str[PlatformBasic::OP_DREM] = "drem";
    sOpEnum2Str[PlatformBasic::OP_DRSQRT] = "drsqrt";
    sOpEnum2Str[PlatformBasic::OP_DSP] = "dsp";
    sOpEnum2Str[PlatformBasic::OP_DSP_AB] = "dsp_ab";
    sOpEnum2Str[PlatformBasic::OP_DSQRT] = "dsqrt";
    sOpEnum2Str[PlatformBasic::OP_DSUB] = "dsub";
    sOpEnum2Str[PlatformBasic::OP_EXTRACTELEMENT] = "extractelement";
    sOpEnum2Str[PlatformBasic::OP_EXTRACTVALUE] = "extractvalue";
    sOpEnum2Str[PlatformBasic::OP_FADD] = "fadd";
    sOpEnum2Str[PlatformBasic::OP_FCMP] = "fcmp";
    sOpEnum2Str[PlatformBasic::OP_FDIV] = "fdiv";
    sOpEnum2Str[PlatformBasic::OP_FENCE] = "fence";
    sOpEnum2Str[PlatformBasic::OP_FEXP] = "fexp";
    sOpEnum2Str[PlatformBasic::OP_FLOG] = "flog";
    sOpEnum2Str[PlatformBasic::OP_FMUL] = "fmul";
    sOpEnum2Str[PlatformBasic::OP_FPEXT] = "fpext";
    sOpEnum2Str[PlatformBasic::OP_FPTOSI] = "fptosi";
    sOpEnum2Str[PlatformBasic::OP_FPTOUI] = "fptoui";
    sOpEnum2Str[PlatformBasic::OP_FPTRUNC] = "fptrunc";
    sOpEnum2Str[PlatformBasic::OP_FRECIP] = "frecip";
    sOpEnum2Str[PlatformBasic::OP_FREM] = "frem";
    sOpEnum2Str[PlatformBasic::OP_FRSQRT] = "frsqrt";
    sOpEnum2Str[PlatformBasic::OP_FSQRT] = "fsqrt";
    sOpEnum2Str[PlatformBasic::OP_FSUB] = "fsub";
    sOpEnum2Str[PlatformBasic::OP_GETELEMENTPTR] = "getelementptr";
    sOpEnum2Str[PlatformBasic::OP_HADD] = "hadd";
    sOpEnum2Str[PlatformBasic::OP_HCMP] = "hcmp";
    sOpEnum2Str[PlatformBasic::OP_HDIV] = "hdiv";
    sOpEnum2Str[PlatformBasic::OP_HMUL] = "hmul";
    sOpEnum2Str[PlatformBasic::OP_HPTODP] = "hptodp";
    sOpEnum2Str[PlatformBasic::OP_HPTOSP] = "hptosp";
    sOpEnum2Str[PlatformBasic::OP_HSQRT] = "hsqrt";
    sOpEnum2Str[PlatformBasic::OP_HSUB] = "hsub";
    sOpEnum2Str[PlatformBasic::OP_ICMP] = "icmp";
    sOpEnum2Str[PlatformBasic::OP_IFCANREAD] = "ifcanread";
    sOpEnum2Str[PlatformBasic::OP_IFCANWRITE] = "ifcanwrite";
    sOpEnum2Str[PlatformBasic::OP_IFNBREAD] = "ifnbread";
    sOpEnum2Str[PlatformBasic::OP_IFNBWRITE] = "ifnbwrite";
    sOpEnum2Str[PlatformBasic::OP_IFREAD] = "ifread";
    sOpEnum2Str[PlatformBasic::OP_IFWRITE] = "ifwrite";
    sOpEnum2Str[PlatformBasic::OP_INDIRECTBR] = "indirectbr";
    sOpEnum2Str[PlatformBasic::OP_INSERTELEMENT] = "insertelement";
    sOpEnum2Str[PlatformBasic::OP_INSERTVALUE] = "insertvalue";
    sOpEnum2Str[PlatformBasic::OP_INTTOPTR] = "inttoptr";
    sOpEnum2Str[PlatformBasic::OP_INVOKE] = "invoke";
    sOpEnum2Str[PlatformBasic::OP_LANDINGPAD] = "landingpad";
    sOpEnum2Str[PlatformBasic::OP_LOAD] = "load";
    sOpEnum2Str[PlatformBasic::OP_LSHR] = "lshr";
    sOpEnum2Str[PlatformBasic::OP_MAC16_CLOCKX2] = "mac16_clockx2";
    sOpEnum2Str[PlatformBasic::OP_MAC6_MAC8_CLOCKX2] = "mac6_mac8_clockx2";
    sOpEnum2Str[PlatformBasic::OP_MEMORY] = "memory";
    sOpEnum2Str[PlatformBasic::OP_MEMSHIFTREAD] = "memshiftread";
    sOpEnum2Str[PlatformBasic::OP_MUL] = "mul";
    sOpEnum2Str[PlatformBasic::OP_MUL_SELECT] = "mul_select";
    sOpEnum2Str[PlatformBasic::OP_MUL_SELECTIVT] = "mul_selectivt";
    sOpEnum2Str[PlatformBasic::OP_MUL_SUB] = "mul_sub";
    sOpEnum2Str[PlatformBasic::OP_MULADD] = "muladd";
    sOpEnum2Str[PlatformBasic::OP_MULADDACC] = "muladdacc";
    sOpEnum2Str[PlatformBasic::OP_MULSELECT] = "mulselect";
    sOpEnum2Str[PlatformBasic::OP_MULSELECTIVT] = "mulselectivt";
    sOpEnum2Str[PlatformBasic::OP_MULSUB] = "mulsub";
    sOpEnum2Str[PlatformBasic::OP_MULSUBACC] = "mulsubacc";
    sOpEnum2Str[PlatformBasic::OP_MUX] = "mux";
    sOpEnum2Str[PlatformBasic::OP_NANDREDUCE] = "nandreduce";
    sOpEnum2Str[PlatformBasic::OP_NBPEEK] = "nbpeek";
    sOpEnum2Str[PlatformBasic::OP_NBREAD] = "nbread";
    sOpEnum2Str[PlatformBasic::OP_NBREADREQ] = "nbreadreq";
    sOpEnum2Str[PlatformBasic::OP_NBWRITE] = "nbwrite";
    sOpEnum2Str[PlatformBasic::OP_NBWRITEREQ] = "nbwritereq";
    sOpEnum2Str[PlatformBasic::OP_NORREDUCE] = "norreduce";
    sOpEnum2Str[PlatformBasic::OP_OR] = "or";
    sOpEnum2Str[PlatformBasic::OP_ORREDUCE] = "orreduce";
    sOpEnum2Str[PlatformBasic::OP_PARTSELECT] = "partselect";
    sOpEnum2Str[PlatformBasic::OP_PARTSET] = "partset";
    sOpEnum2Str[PlatformBasic::OP_PEEK] = "peek";
    sOpEnum2Str[PlatformBasic::OP_PHI] = "phi";
    sOpEnum2Str[PlatformBasic::OP_POLL] = "poll";
    sOpEnum2Str[PlatformBasic::OP_PTRTOINT] = "ptrtoint";
    sOpEnum2Str[PlatformBasic::OP_QADDSUB] = "qaddsub";
    sOpEnum2Str[PlatformBasic::OP_READ] = "read";
    sOpEnum2Str[PlatformBasic::OP_READREQ] = "readreq";
    sOpEnum2Str[PlatformBasic::OP_RESUME] = "resume";
    sOpEnum2Str[PlatformBasic::OP_RET] = "ret";
    sOpEnum2Str[PlatformBasic::OP_RETURN] = "return";
    sOpEnum2Str[PlatformBasic::OP_SDIV] = "sdiv";
    sOpEnum2Str[PlatformBasic::OP_SELECT] = "select";
    sOpEnum2Str[PlatformBasic::OP_SETEQ] = "seteq";
    sOpEnum2Str[PlatformBasic::OP_SETGE] = "setge";
    sOpEnum2Str[PlatformBasic::OP_SETGT] = "setgt";
    sOpEnum2Str[PlatformBasic::OP_SETLE] = "setle";
    sOpEnum2Str[PlatformBasic::OP_SETLT] = "setlt";
    sOpEnum2Str[PlatformBasic::OP_SETNE] = "setne";
    sOpEnum2Str[PlatformBasic::OP_SEXT] = "sext";
    sOpEnum2Str[PlatformBasic::OP_SHL] = "shl";
    sOpEnum2Str[PlatformBasic::OP_SHUFFLEVECTOR] = "shufflevector";
    sOpEnum2Str[PlatformBasic::OP_SITODP] = "sitodp";
    sOpEnum2Str[PlatformBasic::OP_SITOFP] = "sitofp";
    sOpEnum2Str[PlatformBasic::OP_SPECBITSMAP] = "specbitsmap";
    sOpEnum2Str[PlatformBasic::OP_SPECBRAMWITHBYTEENABLE] = "specbramwithbyteenable";
    sOpEnum2Str[PlatformBasic::OP_SPECBURST] = "specburst";
    sOpEnum2Str[PlatformBasic::OP_SPECCHANNEL] = "specchannel";
    sOpEnum2Str[PlatformBasic::OP_SPECCHCORE] = "specchcore";
    sOpEnum2Str[PlatformBasic::OP_SPECCLOCKDOMAIN] = "specclockdomain";
    sOpEnum2Str[PlatformBasic::OP_SPECDATAFLOWPIPELINE] = "specdataflowpipeline";
    sOpEnum2Str[PlatformBasic::OP_SPECDT] = "specdt";
    sOpEnum2Str[PlatformBasic::OP_SPECEXT] = "specext";
    sOpEnum2Str[PlatformBasic::OP_SPECFUCORE] = "specfucore";
    sOpEnum2Str[PlatformBasic::OP_SPECIFCORE] = "specifcore";
    sOpEnum2Str[PlatformBasic::OP_SPECINTERFACE] = "specinterface";
    sOpEnum2Str[PlatformBasic::OP_SPECIPCORE] = "specipcore";
    sOpEnum2Str[PlatformBasic::OP_SPECKEEPVALUE] = "speckeepvalue";
    sOpEnum2Str[PlatformBasic::OP_SPECLATENCY] = "speclatency";
    sOpEnum2Str[PlatformBasic::OP_SPECLOOPBEGIN] = "specloopbegin";
    sOpEnum2Str[PlatformBasic::OP_SPECLOOPEND] = "specloopend";
    sOpEnum2Str[PlatformBasic::OP_SPECLOOPFLATTEN] = "specloopflatten";
    sOpEnum2Str[PlatformBasic::OP_SPECLOOPNAME] = "specloopname";
    sOpEnum2Str[PlatformBasic::OP_SPECLOOPTRIPCOUNT] = "speclooptripcount";
    sOpEnum2Str[PlatformBasic::OP_SPECMEMCORE] = "specmemcore";
    sOpEnum2Str[PlatformBasic::OP_SPECMODULE] = "specmodule";
    sOpEnum2Str[PlatformBasic::OP_SPECMODULEINST] = "specmoduleinst";
    sOpEnum2Str[PlatformBasic::OP_SPECOCCURRENCE] = "specoccurrence";
    sOpEnum2Str[PlatformBasic::OP_SPECPARALLEL] = "specparallel";
    sOpEnum2Str[PlatformBasic::OP_SPECPARALLELLOOP] = "specparallelloop";
    sOpEnum2Str[PlatformBasic::OP_SPECPIPELINE] = "specpipeline";
    sOpEnum2Str[PlatformBasic::OP_SPECPIPODEPTH] = "specpipodepth";
    sOpEnum2Str[PlatformBasic::OP_SPECPLATFORM] = "specplatform";
    sOpEnum2Str[PlatformBasic::OP_SPECPORT] = "specport";
    sOpEnum2Str[PlatformBasic::OP_SPECPORTMAP] = "specportmap";
    sOpEnum2Str[PlatformBasic::OP_SPECPOWERDOMAIN] = "specpowerdomain";
    sOpEnum2Str[PlatformBasic::OP_SPECPROCESSDECL] = "specprocessdecl";
    sOpEnum2Str[PlatformBasic::OP_SPECPROCESSDEF] = "specprocessdef";
    sOpEnum2Str[PlatformBasic::OP_SPECPROTOCOL] = "specprotocol";
    sOpEnum2Str[PlatformBasic::OP_SPECREGIONBEGIN] = "specregionbegin";
    sOpEnum2Str[PlatformBasic::OP_SPECREGIONEND] = "specregionend";
    sOpEnum2Str[PlatformBasic::OP_SPECRESET] = "specreset";
    sOpEnum2Str[PlatformBasic::OP_SPECRESOURCE] = "specresource";
    sOpEnum2Str[PlatformBasic::OP_SPECRESOURCELIMIT] = "specresourcelimit";
    sOpEnum2Str[PlatformBasic::OP_SPECSENSITIVE] = "specsensitive";
    sOpEnum2Str[PlatformBasic::OP_SPECSTABLE] = "specstable";
    sOpEnum2Str[PlatformBasic::OP_SPECSTABLECONTENT] = "specstablecontent";
    sOpEnum2Str[PlatformBasic::OP_SPECSTATEBEGIN] = "specstatebegin";
    sOpEnum2Str[PlatformBasic::OP_SPECSTATEEND] = "specstateend";
    sOpEnum2Str[PlatformBasic::OP_SPECSYNMODULE] = "specsynmodule";
    sOpEnum2Str[PlatformBasic::OP_SPECTOPMODULE] = "spectopmodule";
    sOpEnum2Str[PlatformBasic::OP_SPTOHP] = "sptohp";
    sOpEnum2Str[PlatformBasic::OP_SREM] = "srem";
    sOpEnum2Str[PlatformBasic::OP_STORE] = "store";
    sOpEnum2Str[PlatformBasic::OP_SUB] = "sub";
    sOpEnum2Str[PlatformBasic::OP_SUBMUL] = "submul";
    sOpEnum2Str[PlatformBasic::OP_SUBMUL_SUB] = "submul_sub";
    sOpEnum2Str[PlatformBasic::OP_SUBMULADD] = "submuladd";
    sOpEnum2Str[PlatformBasic::OP_SUBMULADDACC] = "submuladdacc";
    sOpEnum2Str[PlatformBasic::OP_SUBMULSUB] = "submulsub";
    sOpEnum2Str[PlatformBasic::OP_SUBMULSUBACC] = "submulsubacc";
    sOpEnum2Str[PlatformBasic::OP_SWITCH] = "switch";
    sOpEnum2Str[PlatformBasic::OP_TRUNC] = "trunc";
    sOpEnum2Str[PlatformBasic::OP_UDIV] = "udiv";
    sOpEnum2Str[PlatformBasic::OP_UITODP] = "uitodp";
    sOpEnum2Str[PlatformBasic::OP_UITOFP] = "uitofp";
    sOpEnum2Str[PlatformBasic::OP_UNREACHABLE] = "unreachable";
    sOpEnum2Str[PlatformBasic::OP_UREM] = "urem";
    sOpEnum2Str[PlatformBasic::OP_USEROP1] = "userop1";
    sOpEnum2Str[PlatformBasic::OP_USEROP2] = "userop2";
    sOpEnum2Str[PlatformBasic::OP_VAARG] = "vaarg";
    sOpEnum2Str[PlatformBasic::OP_VIVADO_IP] = "vivado_ip";
    sOpEnum2Str[PlatformBasic::OP_WAIT] = "wait";
    sOpEnum2Str[PlatformBasic::OP_WAITEVENT] = "waitevent";
    sOpEnum2Str[PlatformBasic::OP_WRITE] = "write";
    sOpEnum2Str[PlatformBasic::OP_WRITEREQ] = "writereq";
    sOpEnum2Str[PlatformBasic::OP_WRITERESP] = "writeresp";
    sOpEnum2Str[PlatformBasic::OP_XNORREDUCE] = "xnorreduce";
    sOpEnum2Str[PlatformBasic::OP_XOR] = "xor";
    sOpEnum2Str[PlatformBasic::OP_XORREDUCE] = "xorreduce";
    sOpEnum2Str[PlatformBasic::OP_ZEXT] = "zext";
}

/// create a map of <op_str, op_code>
void PlatformBasic::createOpStr2Enum()
{
    sOpStr2Enum.clear();

    sOpStr2Enum["facc"] = PlatformBasic::OP_FACC;
    sOpStr2Enum["dacc"] = PlatformBasic::OP_DACC;
    sOpStr2Enum["fmac"] = PlatformBasic::OP_FMAC;
    sOpStr2Enum["adapter"] = PlatformBasic::OP_ADAPTER;
    sOpStr2Enum["add"] = PlatformBasic::OP_ADD;
    sOpStr2Enum["addmul"] = PlatformBasic::OP_ADDMUL;
    sOpStr2Enum["addmul_sub"] = PlatformBasic::OP_ADDMUL_SUB;
    sOpStr2Enum["addmuladd"] = PlatformBasic::OP_ADDMULADD;
    sOpStr2Enum["addmuladdacc"] = PlatformBasic::OP_ADDMULADDACC;
    sOpStr2Enum["addmuladdsel"] = PlatformBasic::OP_ADDMULADDSEL;
    sOpStr2Enum["addmulsub"] = PlatformBasic::OP_ADDMULSUB;
    sOpStr2Enum["addmulsubacc"] = PlatformBasic::OP_ADDMULSUBACC;
    sOpStr2Enum["all"] = PlatformBasic::OP_ALL;
    sOpStr2Enum["alloca"] = PlatformBasic::OP_ALLOCA;
    sOpStr2Enum["and"] = PlatformBasic::OP_AND;
    sOpStr2Enum["andreduce"] = PlatformBasic::OP_ANDREDUCE;
    sOpStr2Enum["ashr"] = PlatformBasic::OP_ASHR;
    sOpStr2Enum["atomiccmpxchg"] = PlatformBasic::OP_ATOMICCMPXCHG;
    sOpStr2Enum["atomicrmw"] = PlatformBasic::OP_ATOMICRMW;
    sOpStr2Enum["bitcast"] = PlatformBasic::OP_BITCAST;
    sOpStr2Enum["bitconcatenate"] = PlatformBasic::OP_BITCONCATENATE;
    sOpStr2Enum["bitselect"] = PlatformBasic::OP_BITSELECT;
    sOpStr2Enum["bitset"] = PlatformBasic::OP_BITSET;
    sOpStr2Enum["blackbox"] = PlatformBasic::OP_BLACKBOX;
    sOpStr2Enum["br"] = PlatformBasic::OP_BR;
    sOpStr2Enum["call"] = PlatformBasic::OP_CALL;
    sOpStr2Enum["cmul"] = PlatformBasic::OP_CMUL;
    sOpStr2Enum["ctpop"] = PlatformBasic::OP_CTPOP;
    sOpStr2Enum["dadd"] = PlatformBasic::OP_DADD;
    sOpStr2Enum["dcmp"] = PlatformBasic::OP_DCMP;
    sOpStr2Enum["ddiv"] = PlatformBasic::OP_DDIV;
    sOpStr2Enum["dexp"] = PlatformBasic::OP_DEXP;
    sOpStr2Enum["dlog"] = PlatformBasic::OP_DLOG;
    sOpStr2Enum["dmul"] = PlatformBasic::OP_DMUL;
    sOpStr2Enum["dotpra3"] = PlatformBasic::OP_DOTPRA3;
    sOpStr2Enum["dotpra3add"] = PlatformBasic::OP_DOTPRA3ADD;
    sOpStr2Enum["dptohp"] = PlatformBasic::OP_DPTOHP;
    sOpStr2Enum["dptosi"] = PlatformBasic::OP_DPTOSI;
    sOpStr2Enum["dptoui"] = PlatformBasic::OP_DPTOUI;
    sOpStr2Enum["drecip"] = PlatformBasic::OP_DRECIP;
    sOpStr2Enum["drem"] = PlatformBasic::OP_DREM;
    sOpStr2Enum["drsqrt"] = PlatformBasic::OP_DRSQRT;
    sOpStr2Enum["dsp"] = PlatformBasic::OP_DSP;
    sOpStr2Enum["dsp_ab"] = PlatformBasic::OP_DSP_AB;
    sOpStr2Enum["dsqrt"] = PlatformBasic::OP_DSQRT;
    sOpStr2Enum["dsub"] = PlatformBasic::OP_DSUB;
    sOpStr2Enum["extractelement"] = PlatformBasic::OP_EXTRACTELEMENT;
    sOpStr2Enum["extractvalue"] = PlatformBasic::OP_EXTRACTVALUE;
    sOpStr2Enum["fadd"] = PlatformBasic::OP_FADD;
    sOpStr2Enum["fcmp"] = PlatformBasic::OP_FCMP;
    sOpStr2Enum["fdiv"] = PlatformBasic::OP_FDIV;
    sOpStr2Enum["fence"] = PlatformBasic::OP_FENCE;
    sOpStr2Enum["fexp"] = PlatformBasic::OP_FEXP;
    sOpStr2Enum["flog"] = PlatformBasic::OP_FLOG;
    sOpStr2Enum["fmul"] = PlatformBasic::OP_FMUL;
    sOpStr2Enum["fpext"] = PlatformBasic::OP_FPEXT;
    sOpStr2Enum["fptosi"] = PlatformBasic::OP_FPTOSI;
    sOpStr2Enum["fptoui"] = PlatformBasic::OP_FPTOUI;
    sOpStr2Enum["fptrunc"] = PlatformBasic::OP_FPTRUNC;
    sOpStr2Enum["frecip"] = PlatformBasic::OP_FRECIP;
    sOpStr2Enum["frem"] = PlatformBasic::OP_FREM;
    sOpStr2Enum["frsqrt"] = PlatformBasic::OP_FRSQRT;
    sOpStr2Enum["fsqrt"] = PlatformBasic::OP_FSQRT;
    sOpStr2Enum["fsub"] = PlatformBasic::OP_FSUB;
    sOpStr2Enum["getelementptr"] = PlatformBasic::OP_GETELEMENTPTR;
    sOpStr2Enum["hadd"] = PlatformBasic::OP_HADD;
    sOpStr2Enum["hcmp"] = PlatformBasic::OP_HCMP;
    sOpStr2Enum["hdiv"] = PlatformBasic::OP_HDIV;
    sOpStr2Enum["hmul"] = PlatformBasic::OP_HMUL;
    sOpStr2Enum["hptodp"] = PlatformBasic::OP_HPTODP;
    sOpStr2Enum["hptosp"] = PlatformBasic::OP_HPTOSP;
    sOpStr2Enum["hsqrt"] = PlatformBasic::OP_HSQRT;
    sOpStr2Enum["hsub"] = PlatformBasic::OP_HSUB;
    sOpStr2Enum["icmp"] = PlatformBasic::OP_ICMP;
    sOpStr2Enum["ifcanread"] = PlatformBasic::OP_IFCANREAD;
    sOpStr2Enum["ifcanwrite"] = PlatformBasic::OP_IFCANWRITE;
    sOpStr2Enum["ifnbread"] = PlatformBasic::OP_IFNBREAD;
    sOpStr2Enum["ifnbwrite"] = PlatformBasic::OP_IFNBWRITE;
    sOpStr2Enum["ifread"] = PlatformBasic::OP_IFREAD;
    sOpStr2Enum["ifwrite"] = PlatformBasic::OP_IFWRITE;
    sOpStr2Enum["indirectbr"] = PlatformBasic::OP_INDIRECTBR;
    sOpStr2Enum["insertelement"] = PlatformBasic::OP_INSERTELEMENT;
    sOpStr2Enum["insertvalue"] = PlatformBasic::OP_INSERTVALUE;
    sOpStr2Enum["inttoptr"] = PlatformBasic::OP_INTTOPTR;
    sOpStr2Enum["invoke"] = PlatformBasic::OP_INVOKE;
    sOpStr2Enum["landingpad"] = PlatformBasic::OP_LANDINGPAD;
    sOpStr2Enum["load"] = PlatformBasic::OP_LOAD;
    sOpStr2Enum["lshr"] = PlatformBasic::OP_LSHR;
    sOpStr2Enum["mac16_clockx2"] = PlatformBasic::OP_MAC16_CLOCKX2;
    sOpStr2Enum["mac6_mac8_clockx2"] = PlatformBasic::OP_MAC6_MAC8_CLOCKX2;
    sOpStr2Enum["memory"] = PlatformBasic::OP_MEMORY;
    sOpStr2Enum["memshiftread"] = PlatformBasic::OP_MEMSHIFTREAD;
    sOpStr2Enum["mul"] = PlatformBasic::OP_MUL;
    sOpStr2Enum["mul_select"] = PlatformBasic::OP_MUL_SELECT;
    sOpStr2Enum["mul_selectivt"] = PlatformBasic::OP_MUL_SELECTIVT;
    sOpStr2Enum["mul_sub"] = PlatformBasic::OP_MUL_SUB;
    sOpStr2Enum["muladd"] = PlatformBasic::OP_MULADD;
    sOpStr2Enum["muladdacc"] = PlatformBasic::OP_MULADDACC;
    sOpStr2Enum["mulselect"] = PlatformBasic::OP_MULSELECT;
    sOpStr2Enum["mulselectivt"] = PlatformBasic::OP_MULSELECTIVT;
    sOpStr2Enum["mulsub"] = PlatformBasic::OP_MULSUB;
    sOpStr2Enum["mulsubacc"] = PlatformBasic::OP_MULSUBACC;
    sOpStr2Enum["mux"] = PlatformBasic::OP_MUX;
    sOpStr2Enum["nandreduce"] = PlatformBasic::OP_NANDREDUCE;
    sOpStr2Enum["nbpeek"] = PlatformBasic::OP_NBPEEK;
    sOpStr2Enum["nbread"] = PlatformBasic::OP_NBREAD;
    sOpStr2Enum["nbreadreq"] = PlatformBasic::OP_NBREADREQ;
    sOpStr2Enum["nbwrite"] = PlatformBasic::OP_NBWRITE;
    sOpStr2Enum["nbwritereq"] = PlatformBasic::OP_NBWRITEREQ;
    sOpStr2Enum["norreduce"] = PlatformBasic::OP_NORREDUCE;
    sOpStr2Enum["or"] = PlatformBasic::OP_OR;
    sOpStr2Enum["orreduce"] = PlatformBasic::OP_ORREDUCE;
    sOpStr2Enum["partselect"] = PlatformBasic::OP_PARTSELECT;
    sOpStr2Enum["partset"] = PlatformBasic::OP_PARTSET;
    sOpStr2Enum["peek"] = PlatformBasic::OP_PEEK;
    sOpStr2Enum["phi"] = PlatformBasic::OP_PHI;
    sOpStr2Enum["poll"] = PlatformBasic::OP_POLL;
    sOpStr2Enum["ptrtoint"] = PlatformBasic::OP_PTRTOINT;
    sOpStr2Enum["qaddsub"] = PlatformBasic::OP_QADDSUB;
    sOpStr2Enum["read"] = PlatformBasic::OP_READ;
    sOpStr2Enum["readreq"] = PlatformBasic::OP_READREQ;
    sOpStr2Enum["resume"] = PlatformBasic::OP_RESUME;
    sOpStr2Enum["ret"] = PlatformBasic::OP_RET;
    sOpStr2Enum["return"] = PlatformBasic::OP_RETURN;
    sOpStr2Enum["sdiv"] = PlatformBasic::OP_SDIV;
    sOpStr2Enum["select"] = PlatformBasic::OP_SELECT;
    sOpStr2Enum["seteq"] = PlatformBasic::OP_SETEQ;
    sOpStr2Enum["setge"] = PlatformBasic::OP_SETGE;
    sOpStr2Enum["setgt"] = PlatformBasic::OP_SETGT;
    sOpStr2Enum["setle"] = PlatformBasic::OP_SETLE;
    sOpStr2Enum["setlt"] = PlatformBasic::OP_SETLT;
    sOpStr2Enum["setne"] = PlatformBasic::OP_SETNE;
    sOpStr2Enum["sext"] = PlatformBasic::OP_SEXT;
    sOpStr2Enum["shl"] = PlatformBasic::OP_SHL;
    sOpStr2Enum["shufflevector"] = PlatformBasic::OP_SHUFFLEVECTOR;
    sOpStr2Enum["sitodp"] = PlatformBasic::OP_SITODP;
    sOpStr2Enum["sitofp"] = PlatformBasic::OP_SITOFP;
    sOpStr2Enum["specbitsmap"] = PlatformBasic::OP_SPECBITSMAP;
    sOpStr2Enum["specbramwithbyteenable"] = PlatformBasic::OP_SPECBRAMWITHBYTEENABLE;
    sOpStr2Enum["specburst"] = PlatformBasic::OP_SPECBURST;
    sOpStr2Enum["specchannel"] = PlatformBasic::OP_SPECCHANNEL;
    sOpStr2Enum["specchcore"] = PlatformBasic::OP_SPECCHCORE;
    sOpStr2Enum["specclockdomain"] = PlatformBasic::OP_SPECCLOCKDOMAIN;
    sOpStr2Enum["specdataflowpipeline"] = PlatformBasic::OP_SPECDATAFLOWPIPELINE;
    sOpStr2Enum["specdt"] = PlatformBasic::OP_SPECDT;
    sOpStr2Enum["specext"] = PlatformBasic::OP_SPECEXT;
    sOpStr2Enum["specfucore"] = PlatformBasic::OP_SPECFUCORE;
    sOpStr2Enum["specifcore"] = PlatformBasic::OP_SPECIFCORE;
    sOpStr2Enum["specinterface"] = PlatformBasic::OP_SPECINTERFACE;
    sOpStr2Enum["specipcore"] = PlatformBasic::OP_SPECIPCORE;
    sOpStr2Enum["speckeepvalue"] = PlatformBasic::OP_SPECKEEPVALUE;
    sOpStr2Enum["speclatency"] = PlatformBasic::OP_SPECLATENCY;
    sOpStr2Enum["specloopbegin"] = PlatformBasic::OP_SPECLOOPBEGIN;
    sOpStr2Enum["specloopend"] = PlatformBasic::OP_SPECLOOPEND;
    sOpStr2Enum["specloopflatten"] = PlatformBasic::OP_SPECLOOPFLATTEN;
    sOpStr2Enum["specloopname"] = PlatformBasic::OP_SPECLOOPNAME;
    sOpStr2Enum["speclooptripcount"] = PlatformBasic::OP_SPECLOOPTRIPCOUNT;
    sOpStr2Enum["specmemcore"] = PlatformBasic::OP_SPECMEMCORE;
    sOpStr2Enum["specmodule"] = PlatformBasic::OP_SPECMODULE;
    sOpStr2Enum["specmoduleinst"] = PlatformBasic::OP_SPECMODULEINST;
    sOpStr2Enum["specoccurrence"] = PlatformBasic::OP_SPECOCCURRENCE;
    sOpStr2Enum["specparallel"] = PlatformBasic::OP_SPECPARALLEL;
    sOpStr2Enum["specparallelloop"] = PlatformBasic::OP_SPECPARALLELLOOP;
    sOpStr2Enum["specpipeline"] = PlatformBasic::OP_SPECPIPELINE;
    sOpStr2Enum["specpipodepth"] = PlatformBasic::OP_SPECPIPODEPTH;
    sOpStr2Enum["specplatform"] = PlatformBasic::OP_SPECPLATFORM;
    sOpStr2Enum["specport"] = PlatformBasic::OP_SPECPORT;
    sOpStr2Enum["specportmap"] = PlatformBasic::OP_SPECPORTMAP;
    sOpStr2Enum["specpowerdomain"] = PlatformBasic::OP_SPECPOWERDOMAIN;
    sOpStr2Enum["specprocessdecl"] = PlatformBasic::OP_SPECPROCESSDECL;
    sOpStr2Enum["specprocessdef"] = PlatformBasic::OP_SPECPROCESSDEF;
    sOpStr2Enum["specprotocol"] = PlatformBasic::OP_SPECPROTOCOL;
    sOpStr2Enum["specregionbegin"] = PlatformBasic::OP_SPECREGIONBEGIN;
    sOpStr2Enum["specregionend"] = PlatformBasic::OP_SPECREGIONEND;
    sOpStr2Enum["specreset"] = PlatformBasic::OP_SPECRESET;
    sOpStr2Enum["specresource"] = PlatformBasic::OP_SPECRESOURCE;
    sOpStr2Enum["specresourcelimit"] = PlatformBasic::OP_SPECRESOURCELIMIT;
    sOpStr2Enum["specsensitive"] = PlatformBasic::OP_SPECSENSITIVE;
    sOpStr2Enum["specstable"] = PlatformBasic::OP_SPECSTABLE;
    sOpStr2Enum["specstablecontent"] = PlatformBasic::OP_SPECSTABLECONTENT;
    sOpStr2Enum["specstatebegin"] = PlatformBasic::OP_SPECSTATEBEGIN;
    sOpStr2Enum["specstateend"] = PlatformBasic::OP_SPECSTATEEND;
    sOpStr2Enum["specsynmodule"] = PlatformBasic::OP_SPECSYNMODULE;
    sOpStr2Enum["spectopmodule"] = PlatformBasic::OP_SPECTOPMODULE;
    sOpStr2Enum["sptohp"] = PlatformBasic::OP_SPTOHP;
    sOpStr2Enum["srem"] = PlatformBasic::OP_SREM;
    sOpStr2Enum["store"] = PlatformBasic::OP_STORE;
    sOpStr2Enum["sub"] = PlatformBasic::OP_SUB;
    sOpStr2Enum["submul"] = PlatformBasic::OP_SUBMUL;
    sOpStr2Enum["submul_sub"] = PlatformBasic::OP_SUBMUL_SUB;
    sOpStr2Enum["submuladd"] = PlatformBasic::OP_SUBMULADD;
    sOpStr2Enum["submuladdacc"] = PlatformBasic::OP_SUBMULADDACC;
    sOpStr2Enum["submulsub"] = PlatformBasic::OP_SUBMULSUB;
    sOpStr2Enum["submulsubacc"] = PlatformBasic::OP_SUBMULSUBACC;
    sOpStr2Enum["switch"] = PlatformBasic::OP_SWITCH;
    sOpStr2Enum["trunc"] = PlatformBasic::OP_TRUNC;
    sOpStr2Enum["udiv"] = PlatformBasic::OP_UDIV;
    sOpStr2Enum["uitodp"] = PlatformBasic::OP_UITODP;
    sOpStr2Enum["uitofp"] = PlatformBasic::OP_UITOFP;
    sOpStr2Enum["unreachable"] = PlatformBasic::OP_UNREACHABLE;
    sOpStr2Enum["urem"] = PlatformBasic::OP_UREM;
    sOpStr2Enum["userop1"] = PlatformBasic::OP_USEROP1;
    sOpStr2Enum["userop2"] = PlatformBasic::OP_USEROP2;
    sOpStr2Enum["vaarg"] = PlatformBasic::OP_VAARG;
    sOpStr2Enum["vivado_ip"] = PlatformBasic::OP_VIVADO_IP;
    sOpStr2Enum["wait"] = PlatformBasic::OP_WAIT;
    sOpStr2Enum["waitevent"] = PlatformBasic::OP_WAITEVENT;
    sOpStr2Enum["write"] = PlatformBasic::OP_WRITE;
    sOpStr2Enum["writereq"] = PlatformBasic::OP_WRITEREQ;
    sOpStr2Enum["writeresp"] = PlatformBasic::OP_WRITERESP;
    sOpStr2Enum["xnorreduce"] = PlatformBasic::OP_XNORREDUCE;
    sOpStr2Enum["xor"] = PlatformBasic::OP_XOR;
    sOpStr2Enum["xorreduce"] = PlatformBasic::OP_XORREDUCE;
    sOpStr2Enum["zext"] = PlatformBasic::OP_ZEXT; 
}

/// create a map of <impl_str, impl_code>
void PlatformBasic::createImplEnum2Str()
{
    sImplEnum2Str.clear();

    sImplEnum2Str[PlatformBasic::NOIMPL] = "";
    sImplEnum2Str[PlatformBasic::AUTO] = "auto";
    sImplEnum2Str[PlatformBasic::AUTO_2STAGE] = "auto_2stage";
    sImplEnum2Str[PlatformBasic::AUTO_3STAGE] = "auto_3stage";
    sImplEnum2Str[PlatformBasic::AUTO_4STAGE] = "auto_4stage";
    sImplEnum2Str[PlatformBasic::AUTO_5STAGE] = "auto_5stage";
    sImplEnum2Str[PlatformBasic::AUTO_6STAGE] = "auto_6stage";
    sImplEnum2Str[PlatformBasic::AUTO_COMB] = "auto_comb";
    sImplEnum2Str[PlatformBasic::AUTO_MUX] = "auto_mux";
    sImplEnum2Str[PlatformBasic::AUTO_PIPE] = "auto_pipe";
    sImplEnum2Str[PlatformBasic::AUTO_SEL] = "auto_sel";
    sImplEnum2Str[PlatformBasic::AUTO_SEQ] = "auto_seq";
    sImplEnum2Str[PlatformBasic::AXI4LITES] = "axi4lites";
    sImplEnum2Str[PlatformBasic::AXI4M] = "axi4m";
    sImplEnum2Str[PlatformBasic::AXI4STREAM] = "axi4stream";
    sImplEnum2Str[PlatformBasic::DPWOM_AUTO] = "dpwom_auto";
    sImplEnum2Str[PlatformBasic::DSP] = "dsp";
    sImplEnum2Str[PlatformBasic::DSP48] = "dsp48";
    sImplEnum2Str[PlatformBasic::DSP58_DP] = "dsp58_dp";
    sImplEnum2Str[PlatformBasic::DSP_AM] = "dsp_am";
    sImplEnum2Str[PlatformBasic::DSP_AMA] = "dsp_ama";
    sImplEnum2Str[PlatformBasic::DSP_AMA_2STAGE] = "dsp_ama_2stage";
    sImplEnum2Str[PlatformBasic::DSP_AMA_3STAGE] = "dsp_ama_3stage";
    sImplEnum2Str[PlatformBasic::DSP_AMA_4STAGE] = "dsp_ama_4stage";
    sImplEnum2Str[PlatformBasic::DSP_AM_2STAGE] = "dsp_am_2stage";
    sImplEnum2Str[PlatformBasic::DSP_AM_3STAGE] = "dsp_am_3stage";
    sImplEnum2Str[PlatformBasic::DSP_AM_4STAGE] = "dsp_am_4stage";
    sImplEnum2Str[PlatformBasic::DSP_MAC] = "dsp_mac";
    sImplEnum2Str[PlatformBasic::DSP_MAC_2STAGE] = "dsp_mac_2stage";
    sImplEnum2Str[PlatformBasic::DSP_MAC_3STAGE] = "dsp_mac_3stage";
    sImplEnum2Str[PlatformBasic::DSP_MAC_4STAGE] = "dsp_mac_4stage";
    sImplEnum2Str[PlatformBasic::FABRIC] = "fabric";
    sImplEnum2Str[PlatformBasic::FABRIC_COMB] = "fabric_comb";
    sImplEnum2Str[PlatformBasic::FABRIC_SEQ] = "fabric_seq";
    sImplEnum2Str[PlatformBasic::FIFO_MEMORY] = "fifo_memory";
    sImplEnum2Str[PlatformBasic::FIFO_BRAM] = "fifo_bram";
    sImplEnum2Str[PlatformBasic::FIFO_URAM] = "fifo_uram";
    sImplEnum2Str[PlatformBasic::FIFO_LUTRAM] = "fifo_lutram";
    sImplEnum2Str[PlatformBasic::FIFO_SRL] = "fifo_srl";
    sImplEnum2Str[PlatformBasic::FSL] = "fsl";
    sImplEnum2Str[PlatformBasic::FULLDSP] = "fulldsp";
    sImplEnum2Str[PlatformBasic::MAXDSP] = "maxdsp";
    sImplEnum2Str[PlatformBasic::MEDDSP] = "meddsp";
    sImplEnum2Str[PlatformBasic::M_AXI] = "m_axi";
    sImplEnum2Str[PlatformBasic::NODSP] = "nodsp";
    sImplEnum2Str[PlatformBasic::PRIMITIVEDSP] = "primitivedsp";
    sImplEnum2Str[PlatformBasic::NPI64M] = "npi64m";
    sImplEnum2Str[PlatformBasic::PLB46M] = "plb46m";
    sImplEnum2Str[PlatformBasic::PLB46S] = "plb46s";
    sImplEnum2Str[PlatformBasic::QADDER] = "qadder";
    sImplEnum2Str[PlatformBasic::RAM3S_AUTO] = "ram3s_auto";
    sImplEnum2Str[PlatformBasic::RAM4S_AUTO] = "ram4s_auto";
    sImplEnum2Str[PlatformBasic::RAM5S_AUTO] = "ram5s_auto";
    sImplEnum2Str[PlatformBasic::RAM_1P_AUTO] = "ram_1p_auto";
    sImplEnum2Str[PlatformBasic::RAM_1P_BRAM] = "ram_1p_bram";
    sImplEnum2Str[PlatformBasic::RAM_1P_LUTRAM] = "ram_1p_lutram";
    sImplEnum2Str[PlatformBasic::RAM_1P_URAM] = "ram_1p_uram";
    sImplEnum2Str[PlatformBasic::RAM_2P_AUTO] = "ram_2p_auto";
    sImplEnum2Str[PlatformBasic::RAM_2P_BRAM] = "ram_2p_bram";
    sImplEnum2Str[PlatformBasic::RAM_2P_LUTRAM] = "ram_2p_lutram";
    sImplEnum2Str[PlatformBasic::RAM_2P_URAM] = "ram_2p_uram";
    sImplEnum2Str[PlatformBasic::RAM_AUTO] = "ram_auto";
    sImplEnum2Str[PlatformBasic::RAM_1WNR] = "ram_1wnr_auto";
    sImplEnum2Str[PlatformBasic::RAM_1WNR_LUTRAM] = "ram_1wnr_lutram";
    sImplEnum2Str[PlatformBasic::RAM_1WNR_BRAM] = "ram_1wnr_bram";
    sImplEnum2Str[PlatformBasic::RAM_1WNR_URAM] = "ram_1wnr_uram";
    sImplEnum2Str[PlatformBasic::RAM_COREGEN_AUTO] = "ram_coregen_auto";
    sImplEnum2Str[PlatformBasic::RAM_S2P_BRAM] = "ram_s2p_bram";
    sImplEnum2Str[PlatformBasic::RAM_S2P_BRAM_ECC] = "ram_s2p_bram_ecc";
    sImplEnum2Str[PlatformBasic::RAM_S2P_LUTRAM] = "ram_s2p_lutram";
    sImplEnum2Str[PlatformBasic::RAM_S2P_URAM] = "ram_s2p_uram";
    sImplEnum2Str[PlatformBasic::RAM_S2P_URAM_ECC] = "ram_s2p_uram_ecc";
    sImplEnum2Str[PlatformBasic::RAM_T2P_BRAM] = "ram_t2p_bram";
    sImplEnum2Str[PlatformBasic::RAM_T2P_URAM] = "ram_t2p_uram";
    sImplEnum2Str[PlatformBasic::REGISTER_AUTO] = "register_auto";
    sImplEnum2Str[PlatformBasic::ROM3S_AUTO] = "rom3s_auto";
    sImplEnum2Str[PlatformBasic::ROM4S_AUTO] = "rom4s_auto";
    sImplEnum2Str[PlatformBasic::ROM5S_AUTO] = "rom5s_auto";
    sImplEnum2Str[PlatformBasic::ROM_1P_1S_AUTO] = "rom_1p_1s_auto";
    sImplEnum2Str[PlatformBasic::ROM_1P_AUTO] = "rom_1p_auto";
    sImplEnum2Str[PlatformBasic::ROM_1P_BRAM] = "rom_1p_bram";
    sImplEnum2Str[PlatformBasic::ROM_1P_LUTRAM] = "rom_1p_lutram";
    sImplEnum2Str[PlatformBasic::ROM_2P_AUTO] = "rom_2p_auto";
    sImplEnum2Str[PlatformBasic::ROM_2P_BRAM] = "rom_2p_bram";
    sImplEnum2Str[PlatformBasic::ROM_2P_LUTRAM] = "rom_2p_lutram";
    sImplEnum2Str[PlatformBasic::ROM_AUTO] = "rom_auto";
    sImplEnum2Str[PlatformBasic::ROM_COREGEN_AUTO] = "rom_coregen_auto";
    sImplEnum2Str[PlatformBasic::ROM_NP_AUTO] = "rom_np_auto";
    sImplEnum2Str[PlatformBasic::ROM_NP_BRAM] = "rom_np_bram";
    sImplEnum2Str[PlatformBasic::ROM_NP_LUTRAM] = "rom_np_lutram";
    sImplEnum2Str[PlatformBasic::SHIFTREG_AUTO] = "shiftreg_auto";
    sImplEnum2Str[PlatformBasic::SPRAM_COREGEN_AUTO] = "spram_coregen_auto";
    sImplEnum2Str[PlatformBasic::SPROM_COREGEN_AUTO] = "sprom_coregen_auto";
    sImplEnum2Str[PlatformBasic::SPWOM_AUTO] = "spwom_auto";
    sImplEnum2Str[PlatformBasic::STREAM_BUNDLE] = "stream_bundle";
    sImplEnum2Str[PlatformBasic::S_AXILITE] = "s_axilite";
    sImplEnum2Str[PlatformBasic::TADDER] = "tadder";
    sImplEnum2Str[PlatformBasic::VIVADO_DDS] = "vivado_dds";
    sImplEnum2Str[PlatformBasic::VIVADO_FFT] = "vivado_fft";
    sImplEnum2Str[PlatformBasic::VIVADO_FIR] = "vivado_fir";
    sImplEnum2Str[PlatformBasic::VIVADO_DIVIDER] = "vivado_divider";
    sImplEnum2Str[PlatformBasic::XPM_MEMORY_AUTO] = "xpm_memory_auto";
    sImplEnum2Str[PlatformBasic::XPM_MEMORY_DISTRIBUTE] = "xpm_memory_distribute";
    sImplEnum2Str[PlatformBasic::XPM_MEMORY_BLOCK] = "xpm_memory_block";
    sImplEnum2Str[PlatformBasic::XPM_MEMORY_URAM] = "xpm_memory_uram";
}

/// create a map of <impl_code, impl_str
void PlatformBasic::createImplStr2Enum()
{
    sImplStr2Enum.clear();

    sImplStr2Enum[""] = PlatformBasic::NOIMPL;
    sImplStr2Enum["auto"] = PlatformBasic::AUTO;
    sImplStr2Enum["auto_2stage"] = PlatformBasic::AUTO_2STAGE;
    sImplStr2Enum["auto_3stage"] = PlatformBasic::AUTO_3STAGE;
    sImplStr2Enum["auto_4stage"] = PlatformBasic::AUTO_4STAGE;
    sImplStr2Enum["auto_5stage"] = PlatformBasic::AUTO_5STAGE;
    sImplStr2Enum["auto_6stage"] = PlatformBasic::AUTO_6STAGE;
    sImplStr2Enum["auto_comb"] = PlatformBasic::AUTO_COMB;
    sImplStr2Enum["auto_mux"] = PlatformBasic::AUTO_MUX;
    sImplStr2Enum["auto_pipe"] = PlatformBasic::AUTO_PIPE;
    sImplStr2Enum["auto_sel"] = PlatformBasic::AUTO_SEL;
    sImplStr2Enum["auto_seq"] = PlatformBasic::AUTO_SEQ;
    sImplStr2Enum["axi4lites"] = PlatformBasic::AXI4LITES;
    sImplStr2Enum["axi4m"] = PlatformBasic::AXI4M;
    sImplStr2Enum["axi4stream"] = PlatformBasic::AXI4STREAM;
    sImplStr2Enum["dpwom_auto"] = PlatformBasic::DPWOM_AUTO;
    sImplStr2Enum["dsp"] = PlatformBasic::DSP;
    sImplStr2Enum["dsp48"] = PlatformBasic::DSP48;
    sImplStr2Enum["dsp58_dp"] = PlatformBasic::DSP58_DP;
    sImplStr2Enum["dsp_am"] = PlatformBasic::DSP_AM;
    sImplStr2Enum["dsp_ama"] = PlatformBasic::DSP_AMA;
    sImplStr2Enum["dsp_ama_2stage"] = PlatformBasic::DSP_AMA_2STAGE;
    sImplStr2Enum["dsp_ama_3stage"] = PlatformBasic::DSP_AMA_3STAGE;
    sImplStr2Enum["dsp_ama_4stage"] = PlatformBasic::DSP_AMA_4STAGE;
    sImplStr2Enum["dsp_am_2stage"] = PlatformBasic::DSP_AM_2STAGE;
    sImplStr2Enum["dsp_am_3stage"] = PlatformBasic::DSP_AM_3STAGE;
    sImplStr2Enum["dsp_am_4stage"] = PlatformBasic::DSP_AM_4STAGE;
    sImplStr2Enum["dsp_mac"] = PlatformBasic::DSP_MAC;
    sImplStr2Enum["dsp_mac_2stage"] = PlatformBasic::DSP_MAC_2STAGE;
    sImplStr2Enum["dsp_mac_3stage"] = PlatformBasic::DSP_MAC_3STAGE;
    sImplStr2Enum["dsp_mac_4stage"] = PlatformBasic::DSP_MAC_4STAGE;
    sImplStr2Enum["fabric"] = PlatformBasic::FABRIC;
    sImplStr2Enum["fabric_comb"] = PlatformBasic::FABRIC_COMB;
    sImplStr2Enum["fabric_seq"] = PlatformBasic::FABRIC_SEQ;
    sImplStr2Enum["fifo_memory"] = PlatformBasic::FIFO_MEMORY;
    sImplStr2Enum["fifo_bram"] = PlatformBasic::FIFO_BRAM;
    sImplStr2Enum["fifo_uram"] = PlatformBasic::FIFO_URAM;
    sImplStr2Enum["fifo_lutram"] = PlatformBasic::FIFO_LUTRAM;
    sImplStr2Enum["fifo_srl"] = PlatformBasic::FIFO_SRL;
    sImplStr2Enum["fsl"] = PlatformBasic::FSL;
    sImplStr2Enum["fulldsp"] = PlatformBasic::FULLDSP;
    sImplStr2Enum["maxdsp"] = PlatformBasic::MAXDSP;
    sImplStr2Enum["meddsp"] = PlatformBasic::MEDDSP;
    sImplStr2Enum["m_axi"] = PlatformBasic::M_AXI;
    sImplStr2Enum["nodsp"] = PlatformBasic::NODSP;
    sImplStr2Enum["primitivedsp"] = PlatformBasic::PRIMITIVEDSP;
    sImplStr2Enum["npi64m"] = PlatformBasic::NPI64M;
    sImplStr2Enum["plb46m"] = PlatformBasic::PLB46M;
    sImplStr2Enum["plb46s"] = PlatformBasic::PLB46S;
    sImplStr2Enum["qadder"] = PlatformBasic::QADDER;
    sImplStr2Enum["ram3s_auto"] = PlatformBasic::RAM3S_AUTO;
    sImplStr2Enum["ram4s_auto"] = PlatformBasic::RAM4S_AUTO;
    sImplStr2Enum["ram5s_auto"] = PlatformBasic::RAM5S_AUTO;
    sImplStr2Enum["ram_1p_auto"] = PlatformBasic::RAM_1P_AUTO;
    sImplStr2Enum["ram_1p_bram"] = PlatformBasic::RAM_1P_BRAM;
    sImplStr2Enum["ram_1p_lutram"] = PlatformBasic::RAM_1P_LUTRAM;
    sImplStr2Enum["ram_1p_uram"] = PlatformBasic::RAM_1P_URAM;
    sImplStr2Enum["ram_2p_auto"] = PlatformBasic::RAM_2P_AUTO;
    sImplStr2Enum["ram_2p_bram"] = PlatformBasic::RAM_2P_BRAM;
    sImplStr2Enum["ram_2p_lutram"] = PlatformBasic::RAM_2P_LUTRAM;
    sImplStr2Enum["ram_2p_uram"] = PlatformBasic::RAM_2P_URAM;
    sImplStr2Enum["ram_auto"] = PlatformBasic::RAM_AUTO;
    sImplStr2Enum["ram_1wnr_auto"] = PlatformBasic::RAM_1WNR;
    sImplStr2Enum["ram_1wnr_lutram"] = PlatformBasic::RAM_1WNR_LUTRAM;
    sImplStr2Enum["ram_1wnr_bram"] = PlatformBasic::RAM_1WNR_BRAM;
    sImplStr2Enum["ram_1wnr_uram"] = PlatformBasic::RAM_1WNR_URAM;
    sImplStr2Enum["ram_coregen_auto"] = PlatformBasic::RAM_COREGEN_AUTO;
    sImplStr2Enum["ram_s2p_bram"] = PlatformBasic::RAM_S2P_BRAM;
    sImplStr2Enum["ram_s2p_bram_ecc"] = PlatformBasic::RAM_S2P_BRAM_ECC;
    sImplStr2Enum["ram_s2p_lutram"] = PlatformBasic::RAM_S2P_LUTRAM;
    sImplStr2Enum["ram_s2p_uram"] = PlatformBasic::RAM_S2P_URAM;
    sImplStr2Enum["ram_s2p_uram_ecc"] = PlatformBasic::RAM_S2P_URAM_ECC;
    sImplStr2Enum["ram_t2p_bram"] = PlatformBasic::RAM_T2P_BRAM;
    sImplStr2Enum["ram_t2p_uram"] = PlatformBasic::RAM_T2P_URAM;
    sImplStr2Enum["register_auto"] = PlatformBasic::REGISTER_AUTO;
    sImplStr2Enum["rom3s_auto"] = PlatformBasic::ROM3S_AUTO;
    sImplStr2Enum["rom4s_auto"] = PlatformBasic::ROM4S_AUTO;
    sImplStr2Enum["rom5s_auto"] = PlatformBasic::ROM5S_AUTO;
    sImplStr2Enum["rom_1p_1s_auto"] = PlatformBasic::ROM_1P_1S_AUTO;
    sImplStr2Enum["rom_1p_auto"] = PlatformBasic::ROM_1P_AUTO;
    sImplStr2Enum["rom_1p_bram"] = PlatformBasic::ROM_1P_BRAM;
    sImplStr2Enum["rom_1p_lutram"] = PlatformBasic::ROM_1P_LUTRAM;
    sImplStr2Enum["rom_2p_auto"] = PlatformBasic::ROM_2P_AUTO;
    sImplStr2Enum["rom_2p_bram"] = PlatformBasic::ROM_2P_BRAM;
    sImplStr2Enum["rom_2p_lutram"] = PlatformBasic::ROM_2P_LUTRAM;
    sImplStr2Enum["rom_auto"] = PlatformBasic::ROM_AUTO;
    sImplStr2Enum["rom_coregen_auto"] = PlatformBasic::ROM_COREGEN_AUTO;
    sImplStr2Enum["rom_np_auto"] = PlatformBasic::ROM_NP_AUTO;
    sImplStr2Enum["rom_np_bram"] = PlatformBasic::ROM_NP_BRAM;
    sImplStr2Enum["rom_np_lutram"] = PlatformBasic::ROM_NP_LUTRAM;
    sImplStr2Enum["shiftreg_auto"] = PlatformBasic::SHIFTREG_AUTO;
    sImplStr2Enum["spram_coregen_auto"] = PlatformBasic::SPRAM_COREGEN_AUTO;
    sImplStr2Enum["sprom_coregen_auto"] = PlatformBasic::SPROM_COREGEN_AUTO;
    sImplStr2Enum["spwom_auto"] = PlatformBasic::SPWOM_AUTO;
    sImplStr2Enum["stream_bundle"] = PlatformBasic::STREAM_BUNDLE;
    sImplStr2Enum["s_axilite"] = PlatformBasic::S_AXILITE;
    sImplStr2Enum["tadder"] = PlatformBasic::TADDER;
    sImplStr2Enum["vivado_dds"] = PlatformBasic::VIVADO_DDS;
    sImplStr2Enum["vivado_fft"] = PlatformBasic::VIVADO_FFT;
    sImplStr2Enum["vivado_fir"] = PlatformBasic::VIVADO_FIR;
    sImplStr2Enum["vivado_divider"] = PlatformBasic::VIVADO_DIVIDER;
    sImplStr2Enum["xpm_memory_auto"] = PlatformBasic::XPM_MEMORY_AUTO;
    sImplStr2Enum["xpm_memory_distribute"] = PlatformBasic::XPM_MEMORY_DISTRIBUTE;
    sImplStr2Enum["xpm_memory_block"] = PlatformBasic::XPM_MEMORY_BLOCK;
    sImplStr2Enum["xpm_memory_uram"] = PlatformBasic::XPM_MEMORY_URAM;
}

PlatformBasic::CoreBasic::CoreBasic(PlatformBasic::OP_TYPE op, PlatformBasic::IMPL_TYPE impl, int maxLat, int minLat, 
                     std::string name, bool isPublic):
        mIsPublic(isPublic), mOp(op), mImpl(impl), mMaxLatency(maxLat), mMinLatency(minLat), mName(name)
{}

std::pair<PlatformBasic::MEMORY_TYPE, PlatformBasic::MEMORY_IMPL> PlatformBasic::CoreBasic::getMemoryTypeImpl() const
{
  return getMemoryTypeImpl(mImpl);
}

std::pair<PlatformBasic::MEMORY_TYPE, PlatformBasic::MEMORY_IMPL> PlatformBasic::CoreBasic::getMemoryTypeImpl(PlatformBasic::IMPL_TYPE impl)
{
    std::pair<PlatformBasic::MEMORY_TYPE, PlatformBasic::MEMORY_IMPL> memoryPair(MEMORY_UNSUPPORTED, MEMORY_IMPL_UNSUPPORTED);
    switch(impl)
    {
        case PlatformBasic::SHIFTREG_AUTO:
            memoryPair = std::make_pair(MEMORY_SHIFTREG, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::REGISTER_AUTO:
            memoryPair = std::make_pair(MEMORY_REGISTER, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::XPM_MEMORY_AUTO:
            memoryPair = std::make_pair(MEMORY_XPM_MEMORY, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::XPM_MEMORY_DISTRIBUTE:
            memoryPair = std::make_pair(MEMORY_XPM_MEMORY, MEMORY_IMPL_DISTRIBUTE);
            break;
        case PlatformBasic::XPM_MEMORY_BLOCK:
            memoryPair = std::make_pair(MEMORY_XPM_MEMORY, MEMORY_IMPL_BLOCK);
            break;
        case PlatformBasic::XPM_MEMORY_URAM:
            memoryPair = std::make_pair(MEMORY_XPM_MEMORY, MEMORY_IMPL_URAM);
            break;
        case PlatformBasic::FIFO_MEMORY:
            memoryPair = std::make_pair(MEMORY_FIFO, MEMORY_IMPL_MEMORY);
            break;
        case PlatformBasic::FIFO_SRL:
            memoryPair = std::make_pair(MEMORY_FIFO, MEMORY_IMPL_SRL);
            break;
        case PlatformBasic::FIFO_BRAM:
            memoryPair = std::make_pair(MEMORY_FIFO, MEMORY_IMPL_BRAM);
            break;
        case PlatformBasic::FIFO_URAM:
            memoryPair = std::make_pair(MEMORY_FIFO, MEMORY_IMPL_URAM);
            break;
        case PlatformBasic::FIFO_LUTRAM:
            memoryPair = std::make_pair(MEMORY_FIFO, MEMORY_IMPL_LUTRAM);
            break;
        case PlatformBasic::RAM_AUTO:
            memoryPair = std::make_pair(MEMORY_RAM, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::RAM_1WNR:
            memoryPair = std::make_pair(MEMORY_RAM_1WNR, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::RAM_1WNR_LUTRAM:
            memoryPair = std::make_pair(MEMORY_RAM_1WNR, MEMORY_IMPL_LUTRAM);
            break;
        case PlatformBasic::RAM_1WNR_BRAM:
            memoryPair = std::make_pair(MEMORY_RAM_1WNR, MEMORY_IMPL_BRAM);
            break;
        case PlatformBasic::RAM_1P_AUTO:
            memoryPair = std::make_pair(MEMORY_RAM_1P, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::RAM_1P_LUTRAM:
            memoryPair = std::make_pair(MEMORY_RAM_1P, MEMORY_IMPL_LUTRAM);
            break;
        case PlatformBasic::RAM_1P_BRAM:
            memoryPair = std::make_pair(MEMORY_RAM_1P, MEMORY_IMPL_BRAM);
            break;
        case PlatformBasic::RAM_1P_URAM:
            memoryPair = std::make_pair(MEMORY_RAM_1P, MEMORY_IMPL_URAM);
            break;
        case PlatformBasic::RAM_2P_AUTO:
            memoryPair = std::make_pair(MEMORY_RAM_2P, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::RAM_2P_LUTRAM:
            memoryPair = std::make_pair(MEMORY_RAM_2P, MEMORY_IMPL_LUTRAM);
            break;
        case PlatformBasic::RAM_2P_BRAM:
            memoryPair = std::make_pair(MEMORY_RAM_2P, MEMORY_IMPL_BRAM);
            break;
        case PlatformBasic::RAM_2P_URAM:
            memoryPair = std::make_pair(MEMORY_RAM_2P, MEMORY_IMPL_URAM);
            break;
        case PlatformBasic::RAM_T2P_BRAM:
            memoryPair = std::make_pair(MEMORY_RAM_T2P, MEMORY_IMPL_BRAM);
            break;
        case PlatformBasic::RAM_T2P_URAM:
            memoryPair = std::make_pair(MEMORY_RAM_T2P, MEMORY_IMPL_URAM);
            break;
        case PlatformBasic::RAM_S2P_LUTRAM:
            memoryPair = std::make_pair(MEMORY_RAM_S2P, MEMORY_IMPL_LUTRAM);
            break;
        case PlatformBasic::RAM_S2P_BRAM:
            memoryPair = std::make_pair(MEMORY_RAM_S2P, MEMORY_IMPL_BRAM);
            break;
        case PlatformBasic::RAM_S2P_URAM:
            memoryPair = std::make_pair(MEMORY_RAM_S2P, MEMORY_IMPL_URAM);
            break;
        case PlatformBasic::RAM_S2P_BRAM_ECC:
            memoryPair = std::make_pair(MEMORY_RAM_S2P, MEMORY_IMPL_BRAM_ECC);
            break;
        case PlatformBasic::RAM_S2P_URAM_ECC:
            memoryPair = std::make_pair(MEMORY_RAM_S2P, MEMORY_IMPL_URAM_ECC);
            break;
        case PlatformBasic::ROM_AUTO:
            memoryPair = std::make_pair(MEMORY_ROM, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::ROM_1P_AUTO:
            memoryPair = std::make_pair(MEMORY_ROM_1P, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::ROM_1P_LUTRAM:
            memoryPair = std::make_pair(MEMORY_ROM_1P, MEMORY_IMPL_LUTRAM);
            break;
        case PlatformBasic::ROM_1P_BRAM:
            memoryPair = std::make_pair(MEMORY_ROM_1P, MEMORY_IMPL_BRAM);
            break;
        case PlatformBasic::ROM_1P_1S_AUTO:
            memoryPair = std::make_pair(MEMORY_ROM_1P_1S, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::ROM_NP_AUTO:
            memoryPair = std::make_pair(MEMORY_ROM_NP, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::ROM_NP_LUTRAM:
            memoryPair = std::make_pair(MEMORY_ROM_NP, MEMORY_IMPL_LUTRAM);
            break;
        case PlatformBasic::ROM_NP_BRAM:
            memoryPair = std::make_pair(MEMORY_ROM_NP, MEMORY_IMPL_BRAM);
            break;
        case PlatformBasic::ROM_2P_AUTO:
            memoryPair = std::make_pair(MEMORY_ROM_2P, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::ROM_2P_LUTRAM:
            memoryPair = std::make_pair(MEMORY_ROM_2P, MEMORY_IMPL_LUTRAM);
            break;
        case PlatformBasic::ROM_2P_BRAM:
            memoryPair = std::make_pair(MEMORY_ROM_2P, MEMORY_IMPL_BRAM);
            break;
        case PlatformBasic::RAM3S_AUTO:
            memoryPair = std::make_pair(MEMORY_RAM3S, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::RAM4S_AUTO:
            memoryPair = std::make_pair(MEMORY_RAM4S, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::RAM5S_AUTO:
            memoryPair = std::make_pair(MEMORY_RAM5S, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::RAM_COREGEN_AUTO:
            memoryPair = std::make_pair(MEMORY_RAM_COREGEN, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::SPRAM_COREGEN_AUTO:
            memoryPair = std::make_pair(MEMORY_SPRAM_COREGEN, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::ROM3S_AUTO:
            memoryPair = std::make_pair(MEMORY_ROM3S, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::ROM4S_AUTO:
            memoryPair = std::make_pair(MEMORY_ROM4S, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::ROM5S_AUTO:
            memoryPair = std::make_pair(MEMORY_ROM5S, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::ROM_COREGEN_AUTO:
            memoryPair = std::make_pair(MEMORY_ROM_COREGEN, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::SPROM_COREGEN_AUTO:
            memoryPair = std::make_pair(MEMORY_SPROM_COREGEN, MEMORY_IMPL_AUTO);
            break;
        case PlatformBasic::SPWOM_AUTO:
            memoryPair = std::make_pair(MEMORY_SPWOM, MEMORY_IMPL_AUTO);
            break;
		// for NOIMPL
		case PlatformBasic::NOIMPL_SHIFTREG:
			memoryPair = std::make_pair(MEMORY_SHIFTREG, MEMORY_NOIMPL);
			break;
        case PlatformBasic::NOIMPL_XPM_MEMORY:
            memoryPair = std::make_pair(MEMORY_XPM_MEMORY, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_FIFO:
            memoryPair = std::make_pair(MEMORY_RAM, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_RAM_1WNR:
            memoryPair = std::make_pair(MEMORY_RAM_1WNR, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_RAM_1P:
            memoryPair = std::make_pair(MEMORY_RAM_1P, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_RAM_2P:
            memoryPair = std::make_pair(MEMORY_RAM_2P, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_RAM_2P_1S:
            memoryPair = std::make_pair(MEMORY_RAM_2P_1S, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_RAM_T2P:
            memoryPair = std::make_pair(MEMORY_RAM_T2P, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_RAM_S2P:
            memoryPair = std::make_pair(MEMORY_RAM_S2P, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_ROM:
            memoryPair = std::make_pair(MEMORY_ROM, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_ROM_1P:
            memoryPair = std::make_pair(MEMORY_ROM_1P, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_ROM_1P_1S:
            memoryPair = std::make_pair(MEMORY_ROM_1P_1S, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_ROM_NP:
            memoryPair = std::make_pair(MEMORY_ROM_NP, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_ROM_2P:
            memoryPair = std::make_pair(MEMORY_ROM_2P, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_RAM3S:
            memoryPair = std::make_pair(MEMORY_RAM3S, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_RAM4S:
            memoryPair = std::make_pair(MEMORY_RAM4S, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_RAM5S:
            memoryPair = std::make_pair(MEMORY_RAM5S, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_RAM_COREGEN:
            memoryPair = std::make_pair(MEMORY_RAM_COREGEN, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_SPRAM_COREGEN:
            memoryPair = std::make_pair(MEMORY_SPRAM_COREGEN, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_ROM3S:
            memoryPair = std::make_pair(MEMORY_ROM3S, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_ROM4S:
            memoryPair = std::make_pair(MEMORY_ROM4S, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_ROM5S:
            memoryPair = std::make_pair(MEMORY_ROM5S, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_ROM_COREGEN:
            memoryPair = std::make_pair(MEMORY_ROM_COREGEN, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_SPROM_COREGEN:
            memoryPair = std::make_pair(MEMORY_SPROM_COREGEN, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_SPROMD_ASYNC:
            memoryPair = std::make_pair(MEMORY_SPROMD_ASYNC, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_DPWOM:
            memoryPair = std::make_pair(MEMORY_DPWOM, MEMORY_NOIMPL);
            break;
        case PlatformBasic::NOIMPL_SPWOM:
            memoryPair = std::make_pair(MEMORY_SPWOM, MEMORY_NOIMPL);
            break;
        default: break;
    };

    return memoryPair;
}

bool PlatformBasic::CoreBasic::isRAM() const
{
    return  getMemoryType() == PlatformBasic::MEMORY_RAM || 
            getMemoryType() == PlatformBasic::MEMORY_RAM_1WNR || 
            getMemoryType() == PlatformBasic::MEMORY_RAM_1P ||
            getMemoryType() == PlatformBasic::MEMORY_RAM_2P ||
            getMemoryType() == PlatformBasic::MEMORY_RAM_T2P ||
            getMemoryType() == PlatformBasic::MEMORY_RAM_S2P ||
            getMemoryType() == PlatformBasic::MEMORY_XPM_MEMORY;
}

bool PlatformBasic::CoreBasic::isROM() const
{
    return  getMemoryType() == PlatformBasic::MEMORY_ROM || 
            getMemoryType() == PlatformBasic::MEMORY_ROM_1P ||
            getMemoryType() == PlatformBasic::MEMORY_ROM_NP ||
            getMemoryType() == PlatformBasic::MEMORY_ROM_2P;
}

bool PlatformBasic::CoreBasic::isEccRAM() const
{
    return (getImpl() == PlatformBasic::RAM_S2P_BRAM_ECC ||
            getImpl() == PlatformBasic::RAM_S2P_URAM_ECC);
}

bool PlatformBasic::CoreBasic::supportByteEnable() const
{   
    // support ByteWriteEnable : NON-ECC BRAM, NON-ECC URAM, LUTRAM, AUTORAM.
    // Note : 1) byte write enable is not supported with ECC
    //        2) byte write enable is not supported in xpm memory currently.
    return  (isRAM() && getMemoryType() != PlatformBasic::MEMORY_XPM_MEMORY ) && (   
                getMemoryImpl() == PlatformBasic::MEMORY_IMPL_AUTO ||
                getMemoryImpl() == PlatformBasic::MEMORY_IMPL_BRAM || 
                getMemoryImpl() == PlatformBasic::MEMORY_IMPL_LUTRAM ||
                getMemoryImpl() == PlatformBasic::MEMORY_IMPL_URAM);
}

bool PlatformBasic::CoreBasic::isInitializable() const
{
    // support initialization : all RAMs/ROMs except URAM/ECC URAM
    return  isROM() || (  isRAM() && (   
                getMemoryImpl() == PlatformBasic::MEMORY_IMPL_AUTO || 
                getMemoryImpl() == PlatformBasic::MEMORY_IMPL_BRAM || 
                getMemoryImpl() == PlatformBasic::MEMORY_IMPL_BRAM_ECC ||
                getMemoryImpl() == PlatformBasic::MEMORY_IMPL_LUTRAM ||
                getMemoryImpl() == PlatformBasic::MEMORY_IMPL_BLOCK ||
                getMemoryImpl() == PlatformBasic::MEMORY_IMPL_DISTRIBUTE));
}

void PlatformBasic::createMemoryTypeConverter()
{
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_SHIFTREG, "shiftreg");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_REGISTER, "register");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_XPM_MEMORY, "xpm_memory");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_FIFO, "fifo");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_RAM, "ram");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_RAM_1WNR, "ram_1wnr");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_RAM_1P, "ram_1p");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_RAM_2P, "ram_2p");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_RAM_T2P, "ram_t2p");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_RAM_S2P, "ram_s2p");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_ROM, "rom");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_ROM_1P, "rom_1p");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_ROM_1P_1S, "rom_1p_1s");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_ROM_NP, "rom_np");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_ROM_2P, "rom_2p");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_RAM3S, "ram3s");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_RAM4S, "ram4s");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_RAM5S, "ram5s");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_RAM_COREGEN, "ram_coregen");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_SPRAM_COREGEN, "spram_coregen");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_ROM3S, "rom3s");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_ROM4S, "rom4s");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_ROM5S, "rom5s");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_ROM_COREGEN, "rom_coregen");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_SPROM_COREGEN, "sprom_coregen");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_DPWOM, "dpwom");
    createEnumStrConverter(mMemTypeEnum2Str, mMemTypeStr2Enum, MEMORY_SPWOM, "spwom");
}

void PlatformBasic::createMemoryImplConverter()
{
    createEnumStrConverter(mMemImplEnum2Str, mMemImplStr2Enum, MEMORY_IMPL_AUTO, "auto");
    createEnumStrConverter(mMemImplEnum2Str, mMemImplStr2Enum, MEMORY_IMPL_BRAM, "bram");
    createEnumStrConverter(mMemImplEnum2Str, mMemImplStr2Enum, MEMORY_IMPL_BRAM_ECC, "bram_ecc");
    createEnumStrConverter(mMemImplEnum2Str, mMemImplStr2Enum, MEMORY_IMPL_LUTRAM, "lutram");
    createEnumStrConverter(mMemImplEnum2Str, mMemImplStr2Enum, MEMORY_IMPL_URAM, "uram");
    createEnumStrConverter(mMemImplEnum2Str, mMemImplStr2Enum, MEMORY_IMPL_URAM_ECC, "uram_ecc");
    createEnumStrConverter(mMemImplEnum2Str, mMemImplStr2Enum, MEMORY_IMPL_SRL, "srl");
    createEnumStrConverter(mMemImplEnum2Str, mMemImplStr2Enum, MEMORY_IMPL_BLOCK, "block");
    createEnumStrConverter(mMemImplEnum2Str, mMemImplStr2Enum, MEMORY_IMPL_DISTRIBUTE, "distribute");
    createEnumStrConverter(mMemImplEnum2Str, mMemImplStr2Enum, MEMORY_NOIMPL, "");
    createEnumStrConverter(mMemImplEnum2Str, mMemImplStr2Enum, MEMORY_IMPL_MEMORY, "memory");
}

// class PlatformBasic
PlatformBasic::PlatformBasic()
{
    mCoreMap = createCoreMap();

    createOpEnum2Str();
    createOpStr2Enum();
    createImplEnum2Str();
    createImplStr2Enum();
    
    createMemoryTypeConverter();
    createMemoryImplConverter();
    //check match with .def, TODO
}

PlatformBasic::~PlatformBasic()
{
    for(auto it = mCoreMap.begin(); it != mCoreMap.end(); ++it)
    {
        delete it->second;
    }
}

const PlatformBasic* PlatformBasic::getInstance()
{
   static PlatformBasic *pf = NULL;
    if( pf ) { 
      return pf;
    }
    else { 
        pf = new PlatformBasic();
        return pf;
    }
}

std::vector<PlatformBasic::CoreBasic*> PlatformBasic::getCoreFromName(const std::string& coreName) const
{
    std::vector<PlatformBasic::CoreBasic*> cores;
    auto it = sNameMap.find(getLowerString(coreName));
    if(it != sNameMap.end())
    {
        cores = it->second;
    }
    return cores;
}

PlatformBasic::CoreBasic* PlatformBasic::getCoreFromOpImpl(const std::string& op, const std::string& impl) const
{
    auto opCode = getOpFromName(op);
    auto implCode = getImplFromName(impl);

    return getCoreFromOpImpl(opCode, implCode);
}

PlatformBasic::CoreBasic* PlatformBasic::getCoreFromOpImpl(PlatformBasic::OP_TYPE op, PlatformBasic::IMPL_TYPE impl) const
{
    if(op != OP_UNSUPPORTED && impl != UNSUPPORTED)
    {
        auto it = mCoreMap.find(op * GRPSIZE + impl);
        if(it != mCoreMap.end())
        {
            return it->second;
        }
    }
    return NULL;
}

PlatformBasic::CoreBasic* PlatformBasic::getMemoryFromTypeImpl(PlatformBasic::MEMORY_TYPE type, PlatformBasic::MEMORY_IMPL impl) const
{
    for(auto it = mCoreMap.begin(); it != mCoreMap.end(); ++it)
    {
        auto coreBasic = it->second;
        if(coreBasic->getMemoryTypeImpl() == std::make_pair(type, impl))
        {
            return coreBasic;
        }
    }
    return NULL;
}

std::vector<PlatformBasic::CoreBasic*> PlatformBasic::getPragmaCoreBasics(OP_TYPE op, IMPL_TYPE impl, int latency) const
{
    std::vector<PlatformBasic::CoreBasic*> cores;
    if(isNOIMPL(impl))
    {
        if(op == OP_MEMORY)
        {
            auto memType = static_cast<MEMORY_TYPE>(impl);
            cores = getCoresFromMemoryType(getMemTypeName(memType));
        }
        else
        {
            cores = getCoresFromOp(getOpName(op));
        }
    }
    else
    {
        cores.push_back(getCoreFromOpImpl(op, impl));
    }
    auto notMatchLatency = [latency](CoreBasic* core)
    {
      if (!core) return true;
      return !core->isValidLatency(latency);
    };
    cores.erase(std::remove_if(cores.begin(), cores.end(), notMatchLatency), cores.end());

    return cores;
}



std::vector<PlatformBasic::CoreBasic*> PlatformBasic::getCoresFromOp(const std::string& op) const
{
    std::vector<PlatformBasic::CoreBasic*> cores;
    auto opCode = getOpFromName(op);
    if(opCode != OP_UNSUPPORTED)
    {
        const int lbound = opCode * GRPSIZE;
        for(auto it = mCoreMap.lower_bound(lbound), ub = mCoreMap.upper_bound(lbound + GRPSIZE -1); it != ub; ++it)
        {
            if(it->second->isPublic())
            {
                cores.push_back(it->second);
            }
        }
    }
    return cores;
}

std::vector<PlatformBasic::CoreBasic*> PlatformBasic::getCoresFromMemoryType(const std::string& type) const
{
    std::vector<PlatformBasic::CoreBasic*> cores;
    auto memType = getMemTypeFromName(type);
    for(auto it = mCoreMap.begin(); it != mCoreMap.end(); ++it)
    {
        auto coreBasic = it->second;
        if(coreBasic->isPublic() && coreBasic->getMemoryType() == memType)
        {
            cores.push_back(coreBasic);
        }
    }
    return cores;
}

std::vector<PlatformBasic::CoreBasic*> PlatformBasic::getAllCores() const
{
    std::vector<PlatformBasic::CoreBasic*> cores;
    for(auto it = mCoreMap.begin(); it != mCoreMap.end(); ++it)
    {
        cores.push_back(it->second);
    }
    return cores;
}

std::vector<std::pair<PlatformBasic::OP_TYPE, PlatformBasic::IMPL_TYPE>> PlatformBasic::getOpImplFromCoreName(const std::string& coreName) const
{
    std::vector<std::pair<OP_TYPE, IMPL_TYPE>> opImpls;
    // special for DSP48
    if(iequals(coreName, "dsp48"))
    {
        opImpls.push_back(std::make_pair(OP_MUL, DSP));
        return opImpls;
    }
    std::vector<PlatformBasic::CoreBasic*> cores;
    auto it = sNameMap.find(getLowerString(coreName));
    if(it != sNameMap.end())
    {
        cores = it->second;
    }
    for(auto& core : cores)
    {
        opImpls.push_back(core->getOpAndImpl());
    }
    return opImpls;
}

bool PlatformBasic::isPublicOp(OP_TYPE op) const
{
    bool isPublic = false;
    for(auto it = mCoreMap.begin(); it != mCoreMap.end(); ++it)
    {
        auto coreBasic = it->second;
        if(coreBasic->getOp() == op && coreBasic->isPublic())
        {
            isPublic = true;
            break;
        }
    }
    return isPublic;
}

bool PlatformBasic::isPublicType(MEMORY_TYPE type) const
{
    bool isPublic = false;
    if(type == MEMORY_UNSUPPORTED) return isPublic;
    for(auto it = mCoreMap.begin(); it != mCoreMap.end(); ++it)
    {
        auto coreBasic = it->second;
        if(coreBasic->getMemoryType() == type && coreBasic->isPublic())
        {
            isPublic = true;
            break;
        }
    }
    return isPublic;
}

std::pair<PlatformBasic::OP_TYPE, PlatformBasic::IMPL_TYPE> PlatformBasic::verifyBindOp(const std::string& opStr, const std::string& implStr) const
{
    OP_TYPE op = getOpFromName(opStr);
    IMPL_TYPE impl = UNSUPPORTED;
    if(op != OP_UNSUPPORTED)
    {
        // to support fpdsp, and impl=all
        auto coreImpl = getUserControlData().getCoreImpl(opStr, implStr, false);
        if(coreImpl.empty())
        {
            impl = NOIMPL; 
        }
        else
        {
            impl = getImplFromName(coreImpl);
            // if op+impl can not match to a public core, still return (-1,-1)
            auto coreBasic = getCoreFromOpImpl(op, impl);
            if(coreBasic == NULL || !coreBasic->isPublic())
            {
                op = OP_UNSUPPORTED;
                impl = UNSUPPORTED;
            }
        }
    }
    if(!isPublicOp(op))
    {
        op = OP_UNSUPPORTED;
        impl = UNSUPPORTED;
    }
    return std::make_pair(op, impl);
}

std::pair<PlatformBasic::OP_TYPE, PlatformBasic::IMPL_TYPE> PlatformBasic::verifyBindStorage(const std::string& type, const std::string& implName) const
{
      OP_TYPE op = OP_MEMORY;
    IMPL_TYPE impl = UNSUPPORTED;
    // to support impl=all
    auto coreImpl = getUserControlData().getCoreImpl(type, implName, true);
    if(coreImpl.empty())
    {
        // Use NOIMPL 
        auto memType = getMemTypeFromName(type);
        if(isPublicType(memType))
        {
            impl = static_cast<IMPL_TYPE>(memType);
        }
    }
    else
    {
        auto it = sImplStr2Enum.find(getLowerString(type + "_" + coreImpl));
        if(it != sImplStr2Enum.end())
        {
            impl = it->second;
        }
        // if op+impl can not match to a public core, still return (-1,-1)
        auto coreBasic = getCoreFromOpImpl(op, impl);
        if(coreBasic == NULL || !coreBasic->isPublic())
        {
            impl = UNSUPPORTED;
        }
    }
    if(impl == UNSUPPORTED)
    {
        op = OP_UNSUPPORTED;
    }
    return std::make_pair(op, impl);
}

bool PlatformBasic::verifyInterfaceStorage(std::string storageType, std::pair<OP_TYPE, IMPL_TYPE> * coreEnums) const
{
  storageType = getLowerString(storageType);

  std::vector<std::string> alltypes;
  getAllInterfaceStorageTypes(alltypes);
  if (std::find(alltypes.begin(), alltypes.end(), storageType) == alltypes.end())
  {
    return false;
  }

  auto memType = getMemTypeFromName(storageType);
  if(!isPublicType(memType))
  {
    return false;
  }

  OP_TYPE op = OP_MEMORY;
  IMPL_TYPE impl = static_cast<IMPL_TYPE>(memType);
  if (coreEnums)
  {
    *coreEnums = std::make_pair(op, impl);
  }
  return true;
}


bool PlatformBasic::isValidLatency(OP_TYPE op, IMPL_TYPE impl, int latency) const
{
  std::pair<int, int> implRange;
  return isValidLatency(op, impl, latency, implRange);
}

bool PlatformBasic::isValidLatency(OP_TYPE op, IMPL_TYPE impl, int latency, std::pair<int, int> & implRange) const
{
    bool isValid = false;
    implRange.first = -1;
    implRange.second = -1;
    if(isNOIMPL(impl))
    {
        std::vector<CoreBasic*> cores;
        if(op == OP_MEMORY)
        {
            cores = getCoresFromMemoryType(getMemTypeName(getMemTypeEnum(impl)));
        }
        else
        {
            cores = getCoresFromOp(getOpName(op));
        }

        bool allRangesEqual = true;
        std::pair<int, int> prevRange = implRange;
        for(auto core : cores)
        {
            if (core && core->isValidLatency(latency))
            {
              isValid = true;
              // Return first range that allows latency
              implRange = core->getLatencyRange();
              break;
            }

            if (prevRange == implRange)
            {
              // Store prevRange if set to -1,-1
              prevRange = core->getLatencyRange();
            }
            else if (allRangesEqual && prevRange != core->getLatencyRange())
            {
              // Check if prevRange same as current core range
              allRangesEqual = false;
            }
        }
        if (!isValid && allRangesEqual)
        {
          // If did not find valid range but all ranges are the same return the range
          implRange = prevRange;
        }
    }
    else
    {
        auto it = mCoreMap.find(op * GRPSIZE + impl);
        if(it != mCoreMap.end())
        {
          isValid =it->second->isValidLatency(latency);
          implRange = it->second->getLatencyRange();
        }
    }
    return isValid;
}

std::pair<int, int> PlatformBasic::verifyLatency(OP_TYPE op, IMPL_TYPE impl) const
{
    int minLat = -1;
    int maxLat = -1;
    if(isNOIMPL(impl))
    {
        std::vector<CoreBasic*> cores;
        if(op == OP_MEMORY)
        {
            cores = getCoresFromMemoryType(getMemTypeName(getMemTypeEnum(impl)));
        }
        else
        {
            cores = getCoresFromOp(getOpName(op));
        }
        for(auto core : cores)
        {
            if(minLat == -1 || minLat > core->getMinLatency())
            {
                minLat = core->getMinLatency();
            }
            if(maxLat < core->getMaxLatency())
            {
                maxLat = core->getMaxLatency();
            }
        }
    }
    else
    {
        auto it = mCoreMap.find(op * GRPSIZE + impl);
        if(it != mCoreMap.end())
        {
            return it->second->getLatencyRange();
        }
    }
    return std::make_pair(minLat, maxLat);
}

std::string PlatformBasic::getOpName(OP_TYPE op) const
{
    std::string opStr;
    auto it = sOpEnum2Str.find(op);
    if(it != sOpEnum2Str.end())
    {
        opStr = it->second;
    }
    return opStr;
}

std::string PlatformBasic::getImplName(IMPL_TYPE impl) const
{
    std::string implStr("UNSUPPORTED");
    auto it = sImplEnum2Str.find(impl);
    if(it != sImplEnum2Str.end())
    {
        implStr = it->second;
    }
    if(isNOIMPL(impl))
    {
        implStr = "";
    }
    return implStr;
}

PlatformBasic::OP_TYPE PlatformBasic::getOpFromName(const std::string& opName) const
{
    OP_TYPE op = OP_UNSUPPORTED;
    auto it = sOpStr2Enum.find(getLowerString(opName));
    if(it != sOpStr2Enum.end())
    {
        op = it->second;
    }
    return op;
}

PlatformBasic::IMPL_TYPE PlatformBasic::getImplFromName(const std::string& implName) const
{
    IMPL_TYPE impl = UNSUPPORTED;
    auto it = sImplStr2Enum.find(getLowerString(implName));
    if(it != sImplStr2Enum.end())
    {
        impl = it->second;
    }
    // may be memory without impl
    if(impl == UNSUPPORTED)
    {
        MEMORY_TYPE memType = getMemTypeFromName(implName);
        if(memType != MEMORY_UNSUPPORTED)
        {
            impl = static_cast<IMPL_TYPE>(memType);
        }
    }
    return impl;
}

std::string PlatformBasic::getMemTypeName(MEMORY_TYPE type) const
{
    std::string typeStr;
    auto it = mMemTypeEnum2Str.find(type);
    if(it != mMemTypeEnum2Str.end())
    {
        typeStr = it->second;
    }
    return typeStr;
}

PlatformBasic::MEMORY_TYPE PlatformBasic::getMemTypeEnum(IMPL_TYPE impl)
{
  return CoreBasic::getMemoryTypeImpl(impl).first;
}

PlatformBasic::MEMORY_IMPL PlatformBasic::getMemImplEnum(IMPL_TYPE impl)
{
  return CoreBasic::getMemoryTypeImpl(impl).second;
}


std::string PlatformBasic::getMemImplName(MEMORY_IMPL impl) const
{
    std::string implStr("UNSUPPORTED");
    auto it = mMemImplEnum2Str.find(impl);
    if(it != mMemImplEnum2Str.end())
    {
        implStr = it->second;
    }
    return implStr;
}

PlatformBasic::MEMORY_TYPE PlatformBasic::getMemTypeFromName(const std::string& memTypeName) const
{
    MEMORY_TYPE memType = MEMORY_UNSUPPORTED;
    auto it = mMemTypeStr2Enum.find(getLowerString(memTypeName));
    if(it != mMemTypeStr2Enum.end())
    {
        memType = it->second;
    }
    return memType;
}

PlatformBasic::MEMORY_IMPL PlatformBasic::getMemImplFromName(const std::string& memImplName) const
{
    MEMORY_IMPL memImpl = MEMORY_IMPL_UNSUPPORTED;
    auto it = mMemImplStr2Enum.find(getLowerString(memImplName));
    if( it != mMemImplStr2Enum.end())
    {
        memImpl = it->second;
    }
    return memImpl;
}

bool PlatformBasic::isMemoryOp(OP_TYPE op)
{
    return op == OP_MEMORY || 
           op == OP_LOAD || 
           op == OP_STORE ||
           op == OP_READ || 
           op == OP_WRITE || 
           op == OP_MEMSHIFTREAD ||
           op == OP_NBREAD || 
           op == OP_NBWRITE;
}

} //< namespace platform
