// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The DIGIWAGE developers // Updated copyright year
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/server.h" // Should include protocol.h

// Base includes
#include "base58.h"
#include "init.h"
#include "main.h"
#include "random.h"
#include "sync.h"
#include "util.h"
#include "utilstrencodings.h" // Needed for ParseHashV/ParseHexV/strprintf
#include "utilmoneystr.h"     // Needed for FormatMoney/AmountFromValue etc. (declare external)
#include "chainparams.h"
#include "primitives/transaction.h" // <<< ADD THIS LINE (adjust path if needed)

// Optional includes based on enabled features
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif
#include "guiinterface.h"

// Boost includes
#include <boost/bind/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/signals2/signal.hpp>
#include <boost/thread.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/foreach.hpp>

// UniValue include
#include <univalue.h>

// Standard includes
#include <stdint.h>
#include <vector>
#include <map>
#include <list>
#include <set> // Needed for RPCTypeCheckObj
#include <memory>
#include <algorithm> // For std::sort
#include <stdexcept> // For std::runtime_error

// === Global variables ===
static bool fRPCRunning = false;
static bool fRPCInWarmup = true;
static std::string rpcWarmupStatus("RPC server started");
static RecursiveMutex cs_rpcWarmup;
static RPCTimerInterface* timerInterface = NULL;
static std::map<std::string, boost::shared_ptr<RPCTimerBase> > deadlineTimers;
static struct CRPCSignals g_rpcSignals; // Definition matching struct in header
// =======================================================================================


// === Function Definitions for functions DECLARED in rpc/server.h ===

// Helper: find_value
const UniValue& find_value(const UniValue& obj, const std::string& name)
{
    if (!obj.isObject())
        return NullUniValue; // Return static const NullUniValue reference

    // Previous attempts:
    // const UniValue* V = obj.find(name); // Failed
    // const UniValue* V = obj.locate(name); // Failed

    // New attempt: Use exists() and operator[]
    if (obj.exists(name)) {
        // obj[name] typically returns a const UniValue& when obj is const
        return obj[name];
    } else {
        return NullUniValue;
    }

    // Note: We can't easily return a pointer (*) like the original code
    // tried to do with find/locate using only exists/operator[].
    // But returning a const reference (&) works for the usage in RPCTypeCheckObj.
}

// Definition for IsRPCRunning
bool IsRPCRunning()
{
    return fRPCRunning;
}

// Definition for RPCIsInWarmup
bool RPCIsInWarmup(std::string *outStatus)
{
    LOCK(cs_rpcWarmup);
    if (outStatus)
        *outStatus = rpcWarmupStatus;
    return fRPCInWarmup;
}

// Definition for SetRPCWarmupStatus
void SetRPCWarmupStatus(const std::string& newStatus)
{
    LOCK(cs_rpcWarmup);
    rpcWarmupStatus = newStatus;
}

// Definition for SetRPCWarmupFinished
void SetRPCWarmupFinished()
{
    LOCK(cs_rpcWarmup);
    assert(fRPCInWarmup);
    fRPCInWarmup = false;
}

// --- RPCServer signal registration ---
void RPCServer::OnStarted(boost::function<void ()> slot) { g_rpcSignals.Started.connect(slot); }
void RPCServer::OnStopped(boost::function<void ()> slot) { g_rpcSignals.Stopped.connect(slot); }
void RPCServer::OnPreCommand(boost::function<void (const CRPCCommand&)> slot) { g_rpcSignals.PreCommand.connect(boost::bind(slot, boost::placeholders::_1)); }
void RPCServer::OnPostCommand(boost::function<void (const CRPCCommand&)> slot) { g_rpcSignals.PostCommand.connect(boost::bind(slot, boost::placeholders::_1)); }

// --- RPC Type-checking functions ---
// (Definitions likely already correct)
void RPCTypeCheck(const UniValue& params,
                  const std::list<UniValue::VType>& typesExpected,
                  bool fAllowNull)
{
    unsigned int i = 0;
    BOOST_FOREACH(UniValue::VType t, typesExpected)
    {
        if (params.size() <= i)
            break;

        const UniValue& v = params[i];
        if (!((v.type() == t) || (fAllowNull && (v.isNull()))))
        {
            std::string err = strprintf("Expected type %s, got %s", uvTypeName(t), uvTypeName(v.type()));
            throw JSONRPCError(RPC_TYPE_ERROR, err);
        }
        i++;
    }
}

void RPCTypeCheckObj(const UniValue& o,
                  const std::map<std::string, UniValue::VType>& typesExpected,
                  bool fAllowNull,
                  bool fAllowMissing)
{
    if (!o.isObject())
        throw JSONRPCError(RPC_TYPE_ERROR, "Expected object");

    // <<< Correctly initialize std::set from std::vector >>>
    const std::vector<std::string>& keyVec = o.getKeys();
    std::set<std::string> keys(keyVec.begin(), keyVec.end());

    typedef std::map<std::string, UniValue::VType> MT; // Use typedef for clarity
    BOOST_FOREACH(const MT::value_type& t, typesExpected)
    {
        const UniValue& v = find_value(o, t.first);
        if (!fAllowMissing && v.isNull())
            throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Missing %s", t.first));

        if (t.second != UniValue::VNULL) // VNULL means any type allowed
        {
            if (!v.isNull() && v.type() != t.second) // Check type if not null
                throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Expected type %s for %s, got %s", uvTypeName(t.second), t.first, uvTypeName(v.type())));
        }

        if (!fAllowNull && v.isNull()) // Check if null is allowed
            throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Missing non-null %s", t.first)); // Changed error message slightly

        keys.erase(t.first);
    }

    // Check for extraneous keys (currently ignored for compatibility)
    // BOOST_FOREACH(const std::string& key, keys)
    // {
    //     throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Found extraneous key %s", key));
    // }
}

// --- JSON Request Parsing ---
void JSONRequest::parse(const UniValue& valRequest)
{
    if (!valRequest.isObject()) throw JSONRPCError(RPC_INVALID_REQUEST, "Invalid Request object");
    const UniValue& request = valRequest.get_obj();
    id = find_value(request, "id");
    const UniValue& method_v = find_value(request, "method");
    if (method_v.isNull()) throw JSONRPCError(RPC_INVALID_REQUEST, "Missing method");
    if (!method_v.isStr()) throw JSONRPCError(RPC_INVALID_REQUEST, "Method must be a string");
    strMethod = method_v.get_str();
    const UniValue& params_v = find_value(request, "params");
    if (params_v.isArray()) params = params_v.get_array();
    else if (params_v.isNull()) params = UniValue(UniValue::VARR);
    else throw JSONRPCError(RPC_INVALID_REQUEST, "Params must be an array");
}

// --- RPC Command Table Definition ---
// (Includes *declarations* for all RPC function pointers used)
// These should point to the actual functions defined in other rpc_*.cpp files
extern UniValue getinfo(const UniValue& params, bool fHelp); extern UniValue addnode(const UniValue& params, bool fHelp); extern UniValue disconnectnode(const UniValue& params, bool fHelp);
extern UniValue getaddednodeinfo(const UniValue& params, bool fHelp); extern UniValue getconnectioncount(const UniValue& params, bool fHelp); extern UniValue getnettotals(const UniValue& params, bool fHelp);
extern UniValue getnetworkinfo(const UniValue& params, bool fHelp); extern UniValue getpeerinfo(const UniValue& params, bool fHelp); extern UniValue listbanned(const UniValue& params, bool fHelp);
extern UniValue ping(const UniValue& params, bool fHelp); extern UniValue setban(const UniValue& params, bool fHelp); extern UniValue clearbanned(const UniValue& params, bool fHelp);
extern UniValue getblock(const UniValue& params, bool fHelp); extern UniValue getblockchaininfo(const UniValue& params, bool fHelp); extern UniValue getblockcount(const UniValue& params, bool fHelp);
extern UniValue getblockhash(const UniValue& params, bool fHelp); extern UniValue getblockheader(const UniValue& params, bool fHelp); extern UniValue getchaintips(const UniValue& params, bool fHelp);
extern UniValue getdifficulty(const UniValue& params, bool fHelp); extern UniValue getmempoolinfo(const UniValue& params, bool fHelp); extern UniValue getrawmempool(const UniValue& params, bool fHelp);
extern UniValue gettxout(const UniValue& params, bool fHelp); extern UniValue gettxoutsetinfo(const UniValue& params, bool fHelp); extern UniValue invalidateblock(const UniValue& params, bool fHelp);
extern UniValue reconsiderblock(const UniValue& params, bool fHelp); extern UniValue verifychain(const UniValue& params, bool fHelp); extern UniValue getblocktemplate(const UniValue& params, bool fHelp);
extern UniValue getmininginfo(const UniValue& params, bool fHelp); extern UniValue getnetworkhashps(const UniValue& params, bool fHelp); extern UniValue prioritisetransaction(const UniValue& params, bool fHelp);
extern UniValue submitblock(const UniValue& params, bool fHelp);
#ifdef ENABLE_WALLET
extern UniValue getgenerate(const UniValue& params, bool fHelp); extern UniValue setgenerate(const UniValue& params, bool fHelp); extern UniValue generate(const UniValue& params, bool fHelp); extern UniValue gethashespersec(const UniValue& params, bool fHelp);
#endif
extern UniValue createrawtransaction(const UniValue& params, bool fHelp); extern UniValue decoderawtransaction(const UniValue& params, bool fHelp); extern UniValue decodescript(const UniValue& params, bool fHelp);
extern UniValue getrawtransaction(const UniValue& params, bool fHelp); extern UniValue sendrawtransaction(const UniValue& params, bool fHelp); extern UniValue signrawtransaction(const UniValue& params, bool fHelp);
extern UniValue createmultisig(const UniValue& params, bool fHelp); extern UniValue estimatefee(const UniValue& params, bool fHelp); extern UniValue estimatepriority(const UniValue& params, bool fHelp);
extern UniValue validateaddress(const UniValue& params, bool fHelp); extern UniValue verifymessage(const UniValue& params, bool fHelp); extern UniValue setmocktime(const UniValue& params, bool fHelp);
extern UniValue getbestblockhash(const UniValue& params, bool fHelp); extern UniValue waitfornewblock(const UniValue& params, bool fHelp); extern UniValue waitforblock(const UniValue& params, bool fHelp);
extern UniValue waitforblockheight(const UniValue& params, bool fHelp); extern UniValue listmasternodes(const UniValue& params, bool fHelp); extern UniValue getmasternodecount(const UniValue& params, bool fHelp);
extern UniValue masternodeconnect(const UniValue& params, bool fHelp); extern UniValue createmasternodebroadcast(const UniValue& params, bool fHelp); extern UniValue decodemasternodebroadcast(const UniValue& params, bool fHelp);
extern UniValue relaymasternodebroadcast(const UniValue& params, bool fHelp); extern UniValue masternodecurrent(const UniValue& params, bool fHelp); extern UniValue startmasternode(const UniValue& params, bool fHelp);
extern UniValue createmasternodekey(const UniValue& params, bool fHelp); extern UniValue getmasternodeoutputs(const UniValue& params, bool fHelp); extern UniValue listmasternodeconf(const UniValue& params, bool fHelp);
extern UniValue getmasternodestatus(const UniValue& params, bool fHelp); extern UniValue getmasternodewinners(const UniValue& params, bool fHelp); extern UniValue getmasternodescores(const UniValue& params, bool fHelp);
extern UniValue masternodedebug(const UniValue& params, bool fHelp); extern UniValue reloadmasternodeconfig(const UniValue& params, bool fHelp); extern UniValue preparebudget(const UniValue& params, bool fHelp);
extern UniValue submitbudget(const UniValue& params, bool fHelp); extern UniValue mnbudgetvote(const UniValue& params, bool fHelp); extern UniValue getbudgetvotes(const UniValue& params, bool fHelp);
extern UniValue getnextsuperblock(const UniValue& params, bool fHelp); extern UniValue getbudgetprojection(const UniValue& params, bool fHelp); extern UniValue getbudgetinfo(const UniValue& params, bool fHelp);
extern UniValue mnbudgetrawvote(const UniValue& params, bool fHelp); extern UniValue mnfinalbudget(const UniValue& params, bool fHelp); extern UniValue checkbudgets(const UniValue& params, bool fHelp);
extern UniValue mnsync(const UniValue& params, bool fHelp); extern UniValue spork(const UniValue& params, bool fHelp); extern UniValue getpoolinfo(const UniValue& params, bool fHelp);
extern UniValue getblockindexstats(const UniValue& params, bool fHelp); extern UniValue getfeeinfo(const UniValue& params, bool fHelp);
#ifdef ENABLE_WALLET
extern UniValue burn(const UniValue& params, bool fHelp); extern UniValue addmultisigaddress(const UniValue& params, bool fHelp); extern UniValue autocombinerewards(const UniValue& params, bool fHelp);
extern UniValue backupwallet(const UniValue& params, bool fHelp); extern UniValue delegatestake(const UniValue& params, bool fHelp); extern UniValue dumphdinfo(const UniValue& params, bool fHelp);
extern UniValue dumpprivkey(const UniValue& params, bool fHelp); extern UniValue dumpwallet(const UniValue& params, bool fHelp); extern UniValue bip38encrypt(const UniValue& params, bool fHelp);
extern UniValue bip38decrypt(const UniValue& params, bool fHelp); extern UniValue encryptwallet(const UniValue& params, bool fHelp); extern UniValue getaccount(const UniValue& params, bool fHelp);
extern UniValue getaccountaddress(const UniValue& params, bool fHelp); extern UniValue getaddressesbyaccount(const UniValue& params, bool fHelp); extern UniValue getbalance(const UniValue& params, bool fHelp);
extern UniValue getcoldstakingbalance(const UniValue& params, bool fHelp); extern UniValue getdelegatedbalance(const UniValue& params, bool fHelp); extern UniValue getnewaddress(const UniValue& params, bool fHelp);
extern UniValue getnewstakingaddress(const UniValue& params, bool fHelp); extern UniValue getrawchangeaddress(const UniValue& params, bool fHelp); extern UniValue getreceivedbyaccount(const UniValue& params, bool fHelp);
extern UniValue getreceivedbyaddress(const UniValue& params, bool fHelp); extern UniValue getstakingstatus(const UniValue& params, bool fHelp); extern UniValue getstakesplitthreshold(const UniValue& params, bool fHelp);
extern UniValue gettransaction(const UniValue& params, bool fHelp); extern UniValue abandontransaction(const UniValue& params, bool fHelp); extern UniValue getunconfirmedbalance(const UniValue& params, bool fHelp);
extern UniValue getwalletinfo(const UniValue& params, bool fHelp); extern UniValue importaddress(const UniValue& params, bool fHelp); extern UniValue importprivkey(const UniValue& params, bool fHelp);
extern UniValue importwallet(const UniValue& params, bool fHelp); extern UniValue keypoolrefill(const UniValue& params, bool fHelp); extern UniValue listaccounts(const UniValue& params, bool fHelp);
extern UniValue listaddressgroupings(const UniValue& params, bool fHelp); extern UniValue listcoldutxos(const UniValue& params, bool fHelp); extern UniValue listdelegators(const UniValue& params, bool fHelp);
extern UniValue listlockunspent(const UniValue& params, bool fHelp); extern UniValue listreceivedbyaccount(const UniValue& params, bool fHelp); extern UniValue listreceivedbyaddress(const UniValue& params, bool fHelp);
extern UniValue listsinceblock(const UniValue& params, bool fHelp); extern UniValue liststakingaddresses(const UniValue& params, bool fHelp); extern UniValue listtransactions(const UniValue& params, bool fHelp);
extern UniValue listunspent(const UniValue& params, bool fHelp); extern UniValue lockunspent(const UniValue& params, bool fHelp); extern UniValue movecmd(const UniValue& params, bool fHelp);
extern UniValue multisend(const UniValue& params, bool fHelp); extern UniValue rawdelegatestake(const UniValue& params, bool fHelp); extern UniValue sendfrom(const UniValue& params, bool fHelp);
extern UniValue sendmany(const UniValue& params, bool fHelp); extern UniValue sendtoaddress(const UniValue& params, bool fHelp); extern UniValue sendtoaddressix(const UniValue& params, bool fHelp);
extern UniValue setaccount(const UniValue& params, bool fHelp); extern UniValue setstakesplitthreshold(const UniValue& params, bool fHelp); extern UniValue settxfee(const UniValue& params, bool fHelp);
extern UniValue signmessage(const UniValue& params, bool fHelp); extern UniValue walletlock(const UniValue& params, bool fHelp); extern UniValue upgradetohd(const UniValue& params, bool fHelp);
extern UniValue walletpassphrase(const UniValue& params, bool fHelp); extern UniValue walletpassphrasechange(const UniValue& params, bool fHelp); extern UniValue delegatoradd(const UniValue& params, bool fHelp);
extern UniValue delegatorremove(const UniValue& params, bool fHelp); extern UniValue listforgeitems(const UniValue& params, bool fHelp); extern UniValue getaddressinfo(const UniValue& params, bool fHelp);
#endif // ENABLE_WALLET
extern UniValue callcontract(const UniValue& params, bool fHelp); extern UniValue waitforlogs(const UniValue& params, bool fHelp); extern UniValue searchlogs(const UniValue& params, bool fHelp);
extern UniValue gettransactionreceipt(const UniValue& params, bool fHelp); extern UniValue listcontracts(const UniValue& params, bool fHelp); extern UniValue qrc20name(const UniValue& params, bool fHelp);
extern UniValue qrc20symbol(const UniValue& params, bool fHelp); extern UniValue qrc20totalsupply(const UniValue& params, bool fHelp); extern UniValue qrc20decimals(const UniValue& params, bool fHelp);
extern UniValue qrc20balanceof(const UniValue& params, bool fHelp); extern UniValue qrc20allowance(const UniValue& params, bool fHelp); extern UniValue qrc20listtransactions(const UniValue& params, bool fHelp);
extern UniValue help(const UniValue& params, bool fHelp); // Declaration for help itself
extern UniValue stop(const UniValue& params, bool fHelp); // Declaration for stop


static const CRPCCommand vRPCCommands[] =
{
    // ... (All command entries as before) ...
    {"control",             "getinfo",                &getinfo,                 true,      false},
    {"control",             "help",                   &help,                    true,      false},
    {"control",             "stop",                   &stop,                    true,      false},
    // ...
    {"qrc20",               "qrc20listtransactions",  &qrc20listtransactions,   true,      false},
};

// Define tableRPC instance AFTER vRPCCommands array
CRPCTable tableRPC;

// --- CRPCTable Member Definitions ---
CRPCTable::CRPCTable()
{
    for (const auto& cmd : vRPCCommands) {
        mapCommands[cmd.name] = &cmd;
    }
}

const CRPCCommand* CRPCTable::operator[](const std::string& name) const
{
    auto it = mapCommands.find(name);
    if (it == mapCommands.end())
        return nullptr;
    return it->second;
}

// Definition for CRPCTable::execute
UniValue CRPCTable::execute(const std::string& method, const UniValue& params) const
{
    const CRPCCommand* pcmd = (*this)[method];
    if (!pcmd)
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found");

#ifdef ENABLE_WALLET
    if (pcmd->reqWallet && !pwalletMain)
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method requires wallet, but wallet is not loaded");
#endif

    // TODO: Add safe mode check specific to DigiWage if necessary

    try {
        g_rpcSignals.PreCommand(*pcmd);
        UniValue result = (*pcmd->actor)(params, false);
        g_rpcSignals.PostCommand(*pcmd);
        return result;
    } catch (const UniValue& objError) {
        g_rpcSignals.PostCommand(*pcmd);
        throw;
    } catch (const std::exception& e) {
        g_rpcSignals.PostCommand(*pcmd);
        throw JSONRPCError(RPC_MISC_ERROR, e.what());
    } catch (...) {
        g_rpcSignals.PostCommand(*pcmd);
        throw JSONRPCError(RPC_MISC_ERROR, "Unknown exception");
    }
}

std::vector<std::string> CRPCTable::listCommands() const
{
    std::vector<std::string> commandList;
    commandList.reserve(mapCommands.size());
    for(const auto& pair : mapCommands) {
        commandList.push_back(pair.first);
    }
    std::sort(commandList.begin(), commandList.end());
    return commandList;
}


// --- HTTP/Timer Function Definitions ---

// Definition for JSONRPCExecOne (needed by JSONRPCExecBatch)
static UniValue JSONRPCExecOne(const UniValue& req)
{
    UniValue rpc_result(UniValue::VOBJ);
    JSONRequest jreq;
    try {
        jreq.parse(req);
        UniValue result = tableRPC.execute(jreq.strMethod, jreq.params);
        rpc_result = JSONRPCReplyObj(result, NullUniValue, jreq.id); // JSONRPCReplyObj is defined here now
    } catch (const UniValue& objError) {
        rpc_result = JSONRPCReplyObj(NullUniValue, objError, jreq.id);
    } catch (const std::exception& e) {
        rpc_result = JSONRPCReplyObj(NullUniValue,
                                     JSONRPCError(RPC_PARSE_ERROR, e.what()), jreq.id); // JSONRPCError is defined here now
    } catch (...) {
        rpc_result = JSONRPCReplyObj(NullUniValue,
                                     JSONRPCError(RPC_PARSE_ERROR, "unknown error"), jreq.id);
    }
    return rpc_result;
}



// Define RPC Timer functions
void RPCSetTimerInterface(RPCTimerInterface *iface) { timerInterface = iface; }
void RPCSetTimerInterfaceIfUnset(RPCTimerInterface *iface) { if (!timerInterface) timerInterface = iface; }
void RPCUnsetTimerInterface(RPCTimerInterface *iface) { if (timerInterface == iface) timerInterface = nullptr; }

void RPCRunLater(const std::string& name, boost::function<void(void)> func, int64_t nSeconds)
{
    if (!timerInterface)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "No timer handler registered for RPC");
    deadlineTimers.erase(name);
    LogPrint("rpc", "queue run of %s in %i seconds (using %s)\n", name, nSeconds, timerInterface->Name());
    deadlineTimers.insert(std::make_pair(name, boost::shared_ptr<RPCTimerBase>(timerInterface->NewTimer(func, nSeconds*1000))));
}

// --- RPC run control functions ---
bool StartRPC()
{
    LogPrint("rpc", "Starting RPC\n");
    fRPCRunning = true;
    g_rpcSignals.Started();
    return true;
}

void InterruptRPC()
{
    LogPrint("rpc", "Interrupting RPC\n");
    fRPCRunning = false;
    // Add specific interruption logic if needed (e.g., notify condition vars)
}

void StopRPC()
{
    LogPrint("rpc", "Stopping RPC\n");
    deadlineTimers.clear();
    fRPCRunning = false;
    g_rpcSignals.Stopped();
    LogPrint("rpc", "RPC stopped\n");
}

// --- Other definitions originally in rpc/server.cpp ---
std::string CRPCTable::help(std::string strCommand) const
{
     std::string strRet;
    std::string category;
    std::set<rpcfn_type> setDone;
    std::vector<std::pair<std::string, const CRPCCommand*> > vCommands;

    for (auto const& [name, pcmd] : mapCommands)
        vCommands.push_back(std::make_pair(pcmd->category + name, pcmd));
    std::sort(vCommands.begin(), vCommands.end());

    for (const auto& command : vCommands) {
        const CRPCCommand* pcmd = command.second;
        std::string strMethod = pcmd->name;
        if ((!strCommand.empty() || pcmd->category == "hidden") && strMethod != strCommand)
            continue;
#ifdef ENABLE_WALLET
        if (pcmd->reqWallet && !pwalletMain)
            continue;
#endif

        try {
            UniValue params_help(UniValue::VARR);
            rpcfn_type pfn = pcmd->actor;
            if (setDone.insert(pfn).second)
                (*pfn)(params_help, true);
        } catch (const std::exception& e) {
            std::string strHelp = std::string(e.what());
            if (strCommand.empty()) {
                if (strHelp.find('\n') != std::string::npos)
                    strHelp = strHelp.substr(0, strHelp.find('\n'));
                if (category != pcmd->category) {
                    if (!category.empty()) strRet += "\n";
                    category = pcmd->category;
                    std::string firstLetter = category.substr(0, 1);
                    boost::to_upper(firstLetter);
                    strRet += "== " + firstLetter + category.substr(1) + " ==\n";
                }
                strRet += strprintf("  %-26s %s\n", strMethod, strHelp);
            } else {
                 strRet += strHelp + "\n";
            }
        } catch (...) {
             LogPrintf("ERROR: Exception generating help for RPC command %s\n", strMethod);
             strRet += strprintf("  %-26s %s\n", strMethod, "*** Error generating help ***");
        }
    }

    if (strRet.empty() && !strCommand.empty())
        strRet = strprintf("help: unknown command: %s\n", strCommand);

    if (!strRet.empty() && strRet.back() == '\n')
         strRet.pop_back();

    return strRet;
}