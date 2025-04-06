// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2015-2020 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPCSERVER_H
#define BITCOIN_RPCSERVER_H

#include "amount.h"
#include "rpc/protocol.h" // Includes univalue indirectly, declares JSONRPCError, JSONRPCReplyObj
#include "uint256.h"

#include <list>
#include <map>
#include <set> // <<< Need to include set for std::set used below >>>
#include <stdint.h>
#include <string>
#include <vector>
#include <stdexcept>

#include <boost/function.hpp>
#include <boost/signals2/signal.hpp> // <<< Need to include signals2 for struct definition >>>

#include <univalue.h>

class CRPCCommand;
class CBlockIndex;
class CNetAddr;
class CScript;

// <<< Moved struct CRPCSignals definition HERE, BEFORE its use >>>
struct CRPCSignals
{
    boost::signals2::signal<void ()> Started;
    boost::signals2::signal<void ()> Stopped;
    boost::signals2::signal<void (const CRPCCommand&)> PreCommand;
    boost::signals2::signal<void (const CRPCCommand&)> PostCommand;
};

namespace RPCServer
{
    // Declarations of signal connection functions
    void OnStarted(boost::function<void ()> slot);
    void OnStopped(boost::function<void ()> slot);
    void OnPreCommand(boost::function<void (const CRPCCommand&)> slot);
    void OnPostCommand(boost::function<void (const CRPCCommand&)> slot);
}


class JSONRequest
{
public:
    UniValue id;
    std::string strMethod;
    UniValue params;

    JSONRequest() { id = NullUniValue; }
    void parse(const UniValue& valRequest);
};

/** Query whether RPC is running */
bool IsRPCRunning();

// Declare helper functions specific to rpc/server.cpp
std::string HelpExampleCli(const std::string& methodname, const std::string& args);
std::string HelpExampleRpc(const std::string& methodname, const std::string& args);
const UniValue& find_value(const UniValue& obj, const std::string& name);

// Declare helper functions defined elsewhere (ensure includes are present where needed)
extern CAmount AmountFromValue(const UniValue& value); // utilmoneystr.h
extern UniValue ValueFromAmount(const CAmount& amount); // utilmoneystr.h
extern uint256 ParseHashV(const UniValue& v, std::string strName); // utilstrencodings.h or util.h
extern std::vector<unsigned char> ParseHexV(const UniValue& v, std::string strName); // utilstrencodings.h or util.h
extern void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex); // core_io.h or rpc/server.h (declaration moved here in previous step)


void SetRPCWarmupStatus(const std::string& newStatus);
void SetRPCWarmupFinished();
bool RPCIsInWarmup(std::string* statusOut);

void RPCTypeCheck(const UniValue& params,
                  const std::list<UniValue::VType>& typesExpected, bool fAllowNull=false);
void RPCTypeCheckObj(const UniValue& o,
                  const std::map<std::string, UniValue::VType>& typesExpected, bool fAllowNull=false, bool fAllowMissing=false);

class RPCTimerBase { public: virtual ~RPCTimerBase() {} };

class RPCTimerInterface {
public:
    virtual ~RPCTimerInterface() {}
    virtual const char *Name() = 0;
    virtual RPCTimerBase* NewTimer(boost::function<void(void)>& func, int64_t millis) = 0;
};

void RPCSetTimerInterface(RPCTimerInterface *iface);
void RPCSetTimerInterfaceIfUnset(RPCTimerInterface *iface);
void RPCUnsetTimerInterface(RPCTimerInterface *iface);

void RPCRunLater(const std::string& name, boost::function<void(void)> func, int64_t nSeconds);

typedef UniValue(*rpcfn_type)(const UniValue& params, bool fHelp);

class CRPCCommand {
public:
    std::string category; std::string name; rpcfn_type actor; bool okSafeMode; bool reqWallet;
    std::vector<std::string> vecArgNames;
    CRPCCommand(std::string _category, std::string _name, rpcfn_type _actor, bool _okSafeMode, bool _reqWallet) :
      category(std::move(_category)), name(std::move(_name)), actor(_actor), okSafeMode(_okSafeMode), reqWallet(_reqWallet)
    { if (!_actor) throw std::runtime_error("CRPCCommand actor cannot be null"); }
};

class CRPCTable {
private: std::map<std::string, const CRPCCommand*> mapCommands;
public:
    CRPCTable(); const CRPCCommand* operator[](const std::string& name) const;
    std::string help(std::string name) const;
    UniValue execute(const std::string &method, const UniValue &params) const;
    std::vector<std::string> listCommands() const;
    //bool Register(...);
};

extern CRPCTable tableRPC; // Declaration without const

// Utilities defined elsewhere
extern uint256 ParseHashO(const UniValue& o, std::string strKey);
extern std::vector<unsigned char> ParseHexO(const UniValue& o, std::string strKey);

extern int64_t nWalletUnlockTime;
extern double GetDifficulty(const CBlockIndex* blockindex = NULL);
extern std::string HelpRequiringPassphrase();

extern void EnsureWalletIsUnlocked(bool fAllowAnonOnly = false);
extern void EnsureWallet();

// === Existing DigiWage RPC Function Declarations ===
extern UniValue getinfo(const UniValue& params, bool fHelp);
// ... (All other extern UniValue declarations for RPC commands) ...
extern UniValue qrc20listtransactions(const UniValue& params, bool fHelp);

// === Control Functions ===
bool StartRPC();
void InterruptRPC();
void StopRPC();
std::string JSONRPCExecBatch(const UniValue& vReq);
void RPCNotifyBlockChange(bool fInitialDownload, const CBlockIndex* pindex);

#endif // BITCOIN_RPCSERVER_H