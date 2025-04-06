// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2015-2019 The DIGIWAGE developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "db.h"
#include "init.h"
#include "main.h"
#include "masternode-budget.h"
#include "masternode-payments.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "rpc/server.h"
#include "utilmoneystr.h"
#include "obfuscation.h" // Include for obfuScationPool

#include <univalue.h>

#include <boost/tokenizer.hpp>
#include <fstream>
#include <univalue.h> // Ensure UniValue is included

// Make sure obfuScationPool is declared or included properly
extern CObfuscationPool obfuScationPool;

UniValue getpoolinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "getpoolinfo\n"
            "\nReturns anonymous pool-related information\n"

            "\nResult:\n"
            "{\n"
            "  \"current_masternode\": \"addr\",    (string) DIGIWAGE address of current masternode\n"
            "  \"state\": xxxx,                 (string) Current pool state\n"
            "  \"entries\": xxxx,               (numeric) Number of entries\n"
            "  \"entries_accepted\": xxxx       (numeric) Number of entries accepted\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getpoolinfo", "") + HelpExampleRpc("getpoolinfo", ""));

    CMasternode* pCurrentMN = mnodeman.GetCurrentMasterNode();
    std::string currentMNStr = pCurrentMN ? pCurrentMN->addr.ToString() : "Not found";


    UniValue obj(UniValue::VOBJ);
    obj.pushKV("current_masternode", currentMNStr);
    obj.pushKV("state", obfuScationPool.GetState());
    obj.pushKV("entries", obfuScationPool.GetEntriesCount());
    obj.pushKV("entries_accepted", obfuScationPool.GetCountEntriesAccepted());
    return obj;
}

UniValue listmasternodes(const UniValue& params, bool fHelp)
{
    std::string strFilter = "";

    if (params.size() == 1) strFilter = params[0].get_str();

    if (fHelp || (params.size() > 1))
        throw std::runtime_error(
            "listmasternodes ( \"filter\" )\n"
            "\nGet a ranked list of masternodes\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match by txhash, status, or addr.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"rank\": n,           (numeric) Masternode Rank (or 0 if not enabled)\n"
            "    \"network\": \"xxxx\",   (string) Network (ipv4, ipv6, onion)\n"
            "    \"txhash\": \"hash\",    (string) Collateral transaction hash\n"
            "    \"outidx\": n,         (numeric) Collateral transaction output index\n"
            "    \"pubkey\": \"key\",     (string) Masternode public key used for message broadcasting (hex)\n"
            "    \"status\": s,         (string) Status (ENABLED/EXPIRED/REMOVE/etc)\n"
            "    \"addr\": \"addr\",      (string) Masternode Collateral DIGIWAGE address\n"
            "    \"version\": v,        (numeric) Masternode protocol version\n"
            "    \"lastseen\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the last seen\n"
            "    \"activetime\": ttt,   (numeric) The time in seconds since epoch (Jan 1 1970 GMT) masternode has been active\n"
            "    \"lastpaid\": ttt      (numeric) The time in seconds since epoch (Jan 1 1970 GMT) masternode was last paid\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listmasternodes", "") + HelpExampleRpc("listmasternodes", ""));

    UniValue ret(UniValue::VARR);
    int nHeight;
    {
        LOCK(cs_main);
        CBlockIndex* pindex = chainActive.Tip();
        if(!pindex) return UniValue(UniValue::VARR);
        nHeight = pindex->nHeight;
    }
    std::vector<std::pair<int, CMasternode> > vMasternodeRanks = mnodeman.GetMasternodeRanks(nHeight);
    // *** FIX: Removed const from loop variable to allow calling non-const methods ***
    for (auto& s : vMasternodeRanks) {
        UniValue obj(UniValue::VOBJ);
        std::string strTxHash = s.second.vin.prevout.hash.ToString();
        uint32_t oIdx = s.second.vin.prevout.n;

        CMasternode& mn = s.second; // Use reference to the MN object

        // *** FIX: Call Status() on non-const mn ***
        if (strFilter != "" && strTxHash.find(strFilter) == std::string::npos &&
            mn.Status().find(strFilter) == std::string::npos &&
            CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString().find(strFilter) == std::string::npos) continue;

        // *** FIX: Call Status() on non-const mn ***
        std::string strStatus = mn.Status();
        std::string strHost;
        int port;
        SplitHostPort(mn.addr.ToString(), port, strHost);
        CNetAddr node = CNetAddr(strHost);
        std::string strNetwork = GetNetworkName(node.GetNetwork());

        obj.pushKV("rank", (strStatus == "ENABLED" ? s.first : 0));
        obj.pushKV("network", strNetwork);
        obj.pushKV("txhash", strTxHash);
        obj.pushKV("outidx", (uint64_t)oIdx);
        obj.pushKV("pubkey", HexStr(mn.pubKeyMasternode));
        obj.pushKV("status", strStatus);
        obj.pushKV("addr", CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
        obj.pushKV("version", mn.protocolVersion);
        obj.pushKV("lastseen", (int64_t)mn.lastPing.sigTime);
        obj.pushKV("activetime", (int64_t)(mn.lastPing.sigTime - mn.sigTime));
        // *** FIX: Call GetLastPaid() on non-const mn ***
        obj.pushKV("lastpaid", (int64_t)mn.GetLastPaid());

        ret.push_back(obj);
    }

    return ret;
}

UniValue masternodeconnect(const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 1))
        throw std::runtime_error(
            "masternodeconnect \"address\"\n"
            "\nAttempts to connect to specified masternode address\n"

            "\nArguments:\n"
            "1. \"address\"     (string, required) IP or net address to connect to\n"

            "\nResult:\n"
            "null on success, throws error on failure.\n"

            "\nExamples:\n" +
            HelpExampleCli("masternodeconnect", "\"192.168.0.6:46003\"") + HelpExampleRpc("masternodeconnect", "\"192.168.0.6:46003\""));

    std::string strAddress = params[0].get_str();

    CService addr = CService(strAddress);

    CNode* pnode = ConnectNode((CAddress)addr, NULL, false, true);
    if (pnode) {
        pnode->Release();
        return NullUniValue;
    } else {
        throw std::runtime_error("error connecting to masternode " + strAddress);
    }
}

UniValue getmasternodecount (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() > 0))
        throw std::runtime_error(
            "getmasternodecount\n"
            "\nGet masternode count values\n"

            "\nResult:\n"
            "{\n"
            "  \"total\": n,        (numeric) Total masternodes\n"
            "  \"stable\": n,       (numeric) Stable count\n"
            "  \"obfcompat\": n,    (numeric) Obfuscation Compatible (DEPRECATED)\n"
            "  \"enabled\": n,      (numeric) Enabled masternodes\n"
            "  \"inqueue\": n,      (numeric) Masternodes in queue for payment\n"
            "  \"ipv4\": n,         (numeric) IPv4 Mastnodes\n"
            "  \"ipv6\": n,         (numeric) IPv6 Mastnodes\n"
            "  \"onion\": n         (numeric) Onion Mastnodes\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getmasternodecount", "") + HelpExampleRpc("getmasternodecount", ""));

    UniValue obj(UniValue::VOBJ);
    int nCount = 0;
    int ipv4 = 0, ipv6 = 0, onion = 0;

    CBlockIndex* ptip = nullptr;
    {
        LOCK(cs_main);
        ptip = chainActive.Tip();
    }
    if (ptip)
        mnodeman.GetNextMasternodeInQueueForPayment(ptip->nHeight, true, nCount);

    mnodeman.CountNetworks(ActiveProtocol(), ipv4, ipv6, onion);

    obj.pushKV("total", mnodeman.size());
    obj.pushKV("stable", mnodeman.stable_size());
    obj.pushKV("obfcompat", mnodeman.CountEnabled(ActiveProtocol()));
    obj.pushKV("enabled", mnodeman.CountEnabled());
    obj.pushKV("inqueue", nCount);
    obj.pushKV("ipv4", ipv4);
    obj.pushKV("ipv6", ipv6);
    obj.pushKV("onion", onion);

    return obj;
}

UniValue masternodecurrent (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw std::runtime_error(
            "masternodecurrent\n"
            "\nGet current masternode winner (scheduled to be paid next).\n"

            "\nResult:\n"
            "{\n"
            "  \"protocol\": xxxx,        (numeric) Protocol version\n"
            "  \"txhash\": \"xxxx\",      (string) Collateral transaction hash\n"
            "  \"pubkey\": \"xxxx\",      (string) MN Collateral Address\n"
            "  \"lastseen\": xxx,         (numeric) Time since epoch of last seen\n"
            "  \"activeseconds\": xxx     (numeric) Seconds MN has been active\n"
            "}\n"
            "or \"unknown\" if no winner found."

            "\nExamples:\n" +
            HelpExampleCli("masternodecurrent", "") + HelpExampleRpc("masternodecurrent", ""));

    int nHeight;
    {
        LOCK(cs_main);
        if (chainActive.Tip() == nullptr) {
             throw std::runtime_error("Chain not active");
        }
        nHeight = chainActive.Height() + 1;
    }

    int nCount = 0;
    CMasternode* winner = mnodeman.GetNextMasternodeInQueueForPayment(nHeight, true, nCount);

    if (winner) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("protocol", (int64_t)winner->protocolVersion);
        obj.pushKV("txhash", winner->vin.prevout.hash.ToString());
        obj.pushKV("pubkey", CBitcoinAddress(winner->pubKeyCollateralAddress.GetID()).ToString());
        // *** FIX: Reverted check from IsNull() to == CMasternodePing() ***
        obj.pushKV("lastseen", (winner->lastPing == CMasternodePing()) ? winner->sigTime : (int64_t)winner->lastPing.sigTime);
        obj.pushKV("activeseconds", (winner->lastPing == CMasternodePing()) ? 0 : (int64_t)(winner->lastPing.sigTime - winner->sigTime));
        return obj;
    }

    return "unknown";
}

UniValue masternodedebug (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw std::runtime_error(
            "masternodedebug\n"
            "\nPrint masternode status\n"

            "\nResult:\n"
            "\"status\"     (string) Masternode status message\n"

            "\nExamples:\n" +
            HelpExampleCli("masternodedebug", "") + HelpExampleRpc("masternodedebug", ""));

    if (!fMasterNode) {
        return "This node is not configured as a masternode.";
    }

    if (activeMasternode.status != ACTIVE_MASTERNODE_INITIAL || !masternodeSync.IsSynced())
        return activeMasternode.GetStatus();

    CTxIn vin = CTxIn();
    CPubKey pubkey;
    CKey key;
    if (!activeMasternode.GetMasterNodeVin(vin, pubkey, key))
        return "Missing masternode input, likely an issue with configuration or wallet (check privkey/txhash/outidx)";
    else
        return activeMasternode.GetStatus();
}

bool StartMasternodeEntry(UniValue& statusObjRet, CMasternodeBroadcast& mnbRet, bool& fSuccessRet, const CMasternodeConfig::CMasternodeEntry& mne, std::string& errorMessage, std::string strCommand = "")
{
    int nIndex;
    if(!mne.castOutputIndex(nIndex)) {
         errorMessage = "Could not cast output index";
         fSuccessRet = false;
         statusObjRet.pushKV("alias", mne.getAlias()); // Add status even on early failure
         statusObjRet.pushKV("result", "failed");
         statusObjRet.pushKV("error", errorMessage);
         return false; // Indicate failure to process this entry
    }

    CTxIn vin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
    CMasternode* pmn = mnodeman.Find(vin);

    statusObjRet.pushKV("alias", mne.getAlias()); // Set alias early

    if (pmn != NULL) {
        if (strCommand == "missing") {
             errorMessage = "Masternode already present";
             fSuccessRet = false;
             statusObjRet.pushKV("result", "skipped"); // Indicate skipped
             statusObjRet.pushKV("error", errorMessage);
             return false; // Don't process this entry
        }
        if (strCommand == "disabled" && pmn->IsEnabled()) {
            errorMessage = "Masternode already enabled";
            fSuccessRet = false;
            statusObjRet.pushKV("result", "skipped"); // Indicate skipped
            statusObjRet.pushKV("error", errorMessage);
            return false; // Don't process this entry
        }
    }

    fSuccessRet = activeMasternode.CreateBroadcast(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage, mnbRet);

    statusObjRet.pushKV("result", fSuccessRet ? "success" : "failed");
    statusObjRet.pushKV("error", fSuccessRet ? "" : errorMessage);

    return true; // Indicate processed (successfully or not)
}

void RelayMNB(CMasternodeBroadcast& mnb, const bool fSuccess, int& successful, int& failed)
{
    if (fSuccess) {
        successful++;
        mnodeman.UpdateMasternodeList(mnb);
        mnb.Relay();
    } else {
        failed++;
    }
}

void SerializeMNB(UniValue& statusObjRet, const CMasternodeBroadcast& mnb, const bool fSuccess, int& successful, int& failed)
{
    if(fSuccess) {
        successful++;
        CDataStream ssMnb(SER_NETWORK, PROTOCOL_VERSION);
        ssMnb << mnb;
        statusObjRet.pushKV("hex", HexStr(ssMnb.begin(), ssMnb.end()));
    } else {
        failed++;
        // Error message should already be in statusObjRet from StartMasternodeEntry
    }
}

UniValue reloadmasternodeconfig (const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 0)
        throw std::runtime_error(
            "reloadmasternodeconfig\n"
            "\nHot-reloads the masternode.conf file located in the data directory.\n"
            "Updates the list of known masternodes based on the file content.\n"

            "\nResult:\n"
            "{\n"
            "  \"success\": true|false, (boolean) Whether the reload succeeded.\n"
            "  \"message\": \"xxxx\"      (string) Status message indicating result and node counts.\n"
            "}\n"

            "\nExamples:\n" +
             HelpExampleCli("reloadmasternodeconfig", "") + HelpExampleRpc("reloadmasternodeconfig", "")
            );


    UniValue retObj(UniValue::VOBJ);

    int prevCount = masternodeConfig.getCount();
    masternodeConfig.clear();
    std::string error;
    if (!masternodeConfig.read(error)) {
        retObj.pushKV("success", false);
        retObj.pushKV("message", "Error reloading masternode.conf: " + error);
    } else {
        retObj.pushKV("success", true);
        retObj.pushKV("message", "Successfully reloaded masternode.conf (Prev nodes: " + std::to_string(prevCount) + ", New nodes: " + std::to_string(masternodeConfig.getCount()) + ")");
    }

    return retObj;
}

UniValue startmasternode (const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 1) {
        strCommand = params[0].get_str();

        // Backwards compatibility
        if (strCommand == "start") strCommand = "local";
        if (strCommand == "start-alias") strCommand = "alias";
        if (strCommand == "start-all") strCommand = "all";
        if (strCommand == "start-many") strCommand = "many";
        if (strCommand == "start-missing") strCommand = "missing";
        if (strCommand == "start-disabled") strCommand = "disabled";
    }

    if (fHelp || params.size() < 2 || params.size() > 3 ||
        (params.size() == 2 && (strCommand != "local" && strCommand != "all" && strCommand != "many" && strCommand != "missing" && strCommand != "disabled")) ||
        (params.size() == 3 && strCommand != "alias"))
        throw std::runtime_error(
            "startmasternode \"local|all|many|missing|disabled|alias\" lockwallet ( \"alias\" )\n"
            "\nAttempts to start one or more masternode(s)\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. command     (string, required) Specify which set of masternode(s) to start: \n"
            "                  \"local\"      - Start the masternode configured on this node.\n"
            "                  \"all\"        - Start all masternodes configured in masternode.conf.\n"
            "                  \"many\"       - Same as \"all\".\n"
            "                  \"missing\"    - Start all masternodes not currently in the network list.\n"
            "                  \"disabled\"   - Start all masternodes that are enabled but offline.\n"
            "                  \"alias\"      - Start the single masternode identified by alias.\n"
            "2. lockwallet  (boolean, required) Lock wallet after completion.\n"
            "3. alias       (string, optional) Masternode alias. Required if using 'alias' command.\n"

            "\nResult: (for 'local' command):\n"
            "\"status\"     (string) Masternode status message\n"

            "\nResult: (for other commands):\n"
            "{\n"
            "  \"overall\": \"xxxx\",     (string) Overall status message\n"
            "  \"detail\": [\n"
            "    {\n"
            "      \"alias\": \"xxxx\",   (string) Node name or alias\n"
            "      \"result\": \"xxxx\",  (string) 'success', 'failed', or 'skipped'\n" // Added skipped
            "      \"error\": \"xxxx\"    (string) Error message, if any\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("startmasternode", "\"alias\" false \"my_mn\"") +
            HelpExampleRpc("startmasternode", "\"alias\", false, \"my_mn\""));

    bool fLock = (params[1].get_str() == "true");

    EnsureWalletIsUnlocked();

    if (strCommand == "local") {
        if (!fMasterNode) throw std::runtime_error("You must set masternode=1 in the configuration file masternode=1");

        if (activeMasternode.status == ACTIVE_MASTERNODE_STARTED) {
             return "Masternode already started.";
        }

        activeMasternode.status = ACTIVE_MASTERNODE_INITIAL;
        activeMasternode.ManageStatus();

        if (fLock)
            pwalletMain->Lock();

        return activeMasternode.GetStatus();
    }

    if (strCommand == "all" || strCommand == "many" || strCommand == "missing" || strCommand == "disabled") {
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        if ((strCommand == "missing" || strCommand == "disabled") &&
            !masternodeSync.IsMasternodeListSynced()) {
            throw std::runtime_error("You can't use this command until masternode list is synced\n");
        }

        std::vector<CMasternodeConfig::CMasternodeEntry> mnEntries;
        mnEntries = masternodeConfig.getEntries();

        int successful = 0;
        int failed = 0;

        UniValue resultsObj(UniValue::VARR);

        for (const auto& mne : mnEntries) {
            UniValue statusObj(UniValue::VOBJ);
            CMasternodeBroadcast mnb;
            std::string errorMessage;
            bool fSuccess = false;

            if (!StartMasternodeEntry(statusObj, mnb, fSuccess, mne, errorMessage, strCommand)) {
                 // Entry was skipped, statusObj already populated by StartMasternodeEntry
                 resultsObj.push_back(statusObj);
                 continue;
             }

            RelayMNB(mnb, fSuccess, successful, failed);
            resultsObj.push_back(statusObj);
        }

        if (fLock)
            pwalletMain->Lock();

        UniValue returnObj(UniValue::VOBJ);
        returnObj.pushKV("overall", strprintf("Successfully started %d masternodes, failed to start %d, total %d", successful, failed, successful + failed));
        returnObj.pushKV("detail", resultsObj);

        return returnObj;
    }

    if (strCommand == "alias") {
        std::string alias = params[2].get_str();
        bool found = false;

        UniValue statusObj(UniValue::VOBJ);

        for (const auto& mne : masternodeConfig.getEntries()) {
            if (mne.getAlias() == alias) {
                CMasternodeBroadcast mnb;
                found = true;
                std::string errorMessage;
                bool fSuccess = false;

                StartMasternodeEntry(statusObj, mnb, fSuccess, mne, errorMessage, strCommand);

                 if (statusObj.exists("result")) {
                     int successful = 0, failed = 0;
                     RelayMNB(mnb, fSuccess, successful, failed);
                 }
                break;
            }
        }

        if (fLock)
            pwalletMain->Lock();

        if(!found) {
             statusObj.pushKV("alias", alias);
             statusObj.pushKV("result", "failed");
             statusObj.pushKV("error", "Could not find alias in config. Verify with listmasternodeconf.");
        } else if (!statusObj.exists("result")) {
             statusObj.pushKV("alias", alias);
             statusObj.pushKV("result", "failed");
             statusObj.pushKV("error", "Internal error processing alias.");
        }


        return statusObj;
    }
    return NullUniValue;
}

UniValue createmasternodekey (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw std::runtime_error(
            "createmasternodekey\n"
            "\nCreate a new masternode private key\n"

            "\nResult:\n"
            "\"key\"    (string) Masternode private key\n"

            "\nExamples:\n" +
            HelpExampleCli("createmasternodekey", "") + HelpExampleRpc("createmasternodekey", ""));

    CKey secret;
    secret.MakeNewKey(false);

    return CBitcoinSecret(secret).ToString();
}

UniValue getmasternodeoutputs (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw std::runtime_error(
            "getmasternodeoutputs\n"
            "\nPrint all masternode transaction outputs controlled by the wallet\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"txhash\": \"xxxx\",    (string) output transaction hash\n"
            "    \"outputidx\": n       (numeric) output index number\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getmasternodeoutputs", "") + HelpExampleRpc("getmasternodeoutputs", ""));

    if (!pwalletMain) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet instance not found.");
    }

    // *** FIX: Reverted back to using activeMasternode.SelectCoinsMasternode() ***
    std::vector<COutput> possibleCoins = activeMasternode.SelectCoinsMasternode();

    UniValue ret(UniValue::VARR);
    for (const auto& out : possibleCoins) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("txhash", out.tx->GetHash().ToString());
        obj.pushKV("outputidx", out.i);
        ret.push_back(obj);
    }

    return ret;
}

UniValue listmasternodeconf (const UniValue& params, bool fHelp)
{
    std::string strFilter = "";

    if (params.size() == 1) strFilter = params[0].get_str();

    if (fHelp || (params.size() > 1))
        throw std::runtime_error(
            "listmasternodeconf ( \"filter\" )\n"
            "\nPrint masternode.conf in JSON format\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match on alias, address, txHash, or status.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"alias\": \"xxxx\",        (string) masternode alias\n"
            "    \"address\": \"xxxx\",      (string) masternode IP address and port\n"
            "    \"privateKey\": \"xxxx\",   (string) masternode private key\n"
            "    \"txHash\": \"xxxx\",       (string) transaction hash\n"
            "    \"outputIndex\": n,       (numeric) transaction output index\n"
            "    \"status\": \"xxxx\"        (string) masternode status (ENABLED, PRE_ENABLED, MISSING, etc.)\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listmasternodeconf", "") + HelpExampleRpc("listmasternodeconf", ""));

    std::vector<CMasternodeConfig::CMasternodeEntry> mnEntries;
    mnEntries = masternodeConfig.getEntries();

    UniValue ret(UniValue::VARR);

    for (const auto& mne : mnEntries) {
        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;
        CTxIn vin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
        CMasternode* pmn = mnodeman.Find(vin);

        std::string strStatus = pmn ? pmn->Status() : "MISSING";

        if (strFilter != "" && mne.getAlias().find(strFilter) == std::string::npos &&
            mne.getIp().find(strFilter) == std::string::npos &&
            mne.getTxHash().find(strFilter) == std::string::npos &&
            strStatus.find(strFilter) == std::string::npos) continue;

        UniValue mnObj(UniValue::VOBJ);
        mnObj.pushKV("alias", mne.getAlias());
        mnObj.pushKV("address", mne.getIp());
        mnObj.pushKV("privateKey", mne.getPrivKey());
        mnObj.pushKV("txHash", mne.getTxHash());
        mnObj.pushKV("outputIndex", mne.getOutputIndex());
        mnObj.pushKV("status", strStatus);
        ret.push_back(mnObj);
    }

    return ret;
}

UniValue getmasternodestatus (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw std::runtime_error(
            "getmasternodestatus\n"
            "\nPrint masternode status for the node running this command\n"

            "\nResult:\n"
            "{\n"
            "  \"txhash\": \"xxxx\",      (string) Collateral transaction hash\n"
            "  \"outputidx\": n,        (numeric) Collateral transaction output index number\n"
            "  \"netaddr\": \"xxxx\",     (string) Masternode network address (IP:port)\n"
            "  \"addr\": \"xxxx\",        (string) DIGIWAGE collateral address for masternode payments\n"
            "  \"status\": n,           (numeric) Masternode status code (e.g., 4 for STARTED)\n" // Corrected type
            "  \"message\": \"xxxx\"      (string) Masternode status message\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getmasternodestatus", "") + HelpExampleRpc("getmasternodestatus", ""));

    if (!fMasterNode) throw std::runtime_error("This is not configured as a masternode (masternode=0).");

    if (activeMasternode.vin == CTxIn()) {
         throw std::runtime_error("Masternode input transaction not set.");
    }

    CMasternode* pmn = mnodeman.Find(activeMasternode.vin);

    if (pmn) {
        UniValue mnObj(UniValue::VOBJ);
        mnObj.pushKV("txhash", activeMasternode.vin.prevout.hash.ToString());
        mnObj.pushKV("outputidx", (uint64_t)activeMasternode.vin.prevout.n);
        mnObj.pushKV("netaddr", activeMasternode.service.ToString());
        mnObj.pushKV("addr", CBitcoinAddress(pmn->pubKeyCollateralAddress.GetID()).ToString());
        mnObj.pushKV("status", activeMasternode.status); // Status code
        mnObj.pushKV("message", activeMasternode.GetStatus()); // Status message
        return mnObj;
    }
    throw std::runtime_error("Masternode detected locally but not found in the global list. Masternode broadcast may be pending or malfunctioning. Current local status: " + activeMasternode.GetStatus());
}

UniValue getmasternodewinners (const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw std::runtime_error(
            "getmasternodewinners ( blocks \"filter\" )\n"
            "\nPrint the masternode winners for the last n blocks\n"

            "\nArguments:\n"
            "1. blocks      (numeric, optional, default=10) Number of previous blocks to show.\n"
            "2. filter      (string, optional) Search filter matching MN address.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"nHeight\": n,                (numeric) block height\n"
            "    \"winner\": \"address:votes\"    (string) Winner address and votes, or \"Unknown\"\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getmasternodewinners", "5") + HelpExampleRpc("getmasternodewinners", "5, \"X...\""));

    int nHeight;
    {
        LOCK(cs_main);
        CBlockIndex* pindex = chainActive.Tip();
        if(!pindex) return UniValue(UniValue::VARR);
        nHeight = pindex->nHeight;
    }

    int nLast = 10;
    std::string strFilter = "";

    if (params.size() >= 1) {
         try {
             nLast = std::stoi(params[0].get_str());
         } catch (const std::invalid_argument&) {
             throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid blocks count");
         } catch (const std::out_of_range&) {
              throw JSONRPCError(RPC_INVALID_PARAMETER, "Blocks count out of range");
         }
    }


    if (params.size() >= 2)
        strFilter = params[1].get_str();

    UniValue ret(UniValue::VARR);

    for (int i = nHeight - nLast + 1; i <= nHeight; i++) {
        if (i < 0) continue;

        UniValue obj(UniValue::VOBJ);
        obj.pushKV("nHeight", i);

        std::string strPayment = GetRequiredPaymentsString(i);

        if (strFilter != "" && strPayment.find(strFilter) == std::string::npos) continue;

        obj.pushKV("winner", strPayment);

        ret.push_back(obj);
    }

    return ret;
}

UniValue getmasternodescores (const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "getmasternodescores ( blocks )\n"
            "\nPrint list of winning masternode by score calculation for recent blocks\n"

            "\nArguments:\n"
            "1. blocks      (numeric, optional, default=10) Show the computed winner for the last n blocks.\n"

            "\nResult:\n"
            "{\n"
            "  \"<block height>\": \"<winning node hash>\",   (string : string) Computed winning node's collateral hash for specified block height.\n"
            "  ,...\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getmasternodescores", "5") + HelpExampleRpc("getmasternodescores", "5"));

    int nLast = 10;

    if (params.size() == 1) {
        try {
            nLast = std::stoi(params[0].get_str());
             if (nLast < 0) throw std::invalid_argument("Negative block count");
        } catch (const std::invalid_argument&) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid blocks count");
        } catch (const std::out_of_range&) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Blocks count out of range");
        }
    }

    UniValue obj(UniValue::VOBJ);

    std::vector<CMasternode> vMasternodes = mnodeman.GetFullMasternodeVector();
    if (vMasternodes.empty()) {
        return obj;
    }

    int currentHeight = WITH_LOCK(cs_main, return chainActive.Height());

    for (int nHeight = currentHeight - nLast + 1; nHeight <= currentHeight; nHeight++) {
        if (nHeight < 0) continue;

        uint256 nHigh = 0;
        CMasternode* pBestMasternode = nullptr;

        for (CMasternode& mn : vMasternodes) {
             if (!mn.IsEnabled()) continue;

             int scoreHeight = std::max(0, nHeight - 100);
             uint256 n = mn.CalculateScore(1, scoreHeight);

            if (n > nHigh) {
                nHigh = n;
                pBestMasternode = &mn;
            }
        }
        if (pBestMasternode)
            obj.pushKV(strprintf("%d", nHeight), pBestMasternode->vin.prevout.hash.ToString());
        else
            obj.pushKV(strprintf("%d", nHeight), "No eligible masternode found");
    }

    return obj;
}

bool DecodeHexMnb(CMasternodeBroadcast& mnb, const std::string& strHexMnb) {

    if (!IsHex(strHexMnb))
        return false;

    std::vector<unsigned char> mnbData(ParseHex(strHexMnb));
    CDataStream ssData(mnbData, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssData >> mnb;
    }
    catch (const std::exception& e) {
         LogPrintf("%s: Masternode broadcast deserialization failed: %s\n", __func__, e.what());
        return false;
    }

    return true;
}
UniValue createmasternodebroadcast(const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    if (fHelp || (strCommand != "alias" && strCommand != "all") || (strCommand == "alias" && params.size() != 2))
        throw std::runtime_error(
            "createmasternodebroadcast \"command\" ( \"alias\" )\n"
            "\nCreates a masternode broadcast message for one or all masternodes configured in masternode.conf\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. \"command\"      (string, required) \"alias\" for single masternode, \"all\" for all masternodes\n"
            "2. \"alias\"        (string, required if command is \"alias\") Alias of the masternode\n"

            "\nResult (all):\n"
            "{\n"
            "  \"overall\": \"xxx\",        (string) Overall status message indicating number of successes.\n"
            "  \"detail\": [                (array) JSON array of broadcast objects.\n"
            "    {\n"
            "      \"alias\": \"xxx\",      (string) Alias of the masternode.\n"
            "      \"result\": \"xxx\",     (string) \"success\" or \"failed\"\n"
            "      \"hex\": \"xxx\",        (string, if success) Hex encoded broadcast message.\n"
            "      \"error\": \"xxx\"       (string, if failed) Error message, if any.\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nResult (alias):\n"
            "{\n"
            "  \"alias\": \"xxx\",      (string) Alias of the masternode.\n"
            "  \"result\": \"xxx\",     (string) \"success\" or \"failed\"\n"
            "  \"hex\": \"xxx\",        (string, if success) Hex encoded broadcast message.\n"
            "  \"error\": \"xxx\"       (string, if failed) Error message, if any.\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("createmasternodebroadcast", "alias mymn1") + HelpExampleRpc("createmasternodebroadcast", "\"alias\", \"mymn1\""));

    EnsureWalletIsUnlocked();

    if (strCommand == "alias")
    {
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        std::string alias = params[1].get_str();
        bool found = false;

        UniValue statusObj(UniValue::VOBJ);

        for (const auto& mne : masternodeConfig.getEntries()) {
            if(mne.getAlias() == alias) {
                CMasternodeBroadcast mnb;
                found = true;
                std::string errorMessage;
                bool fSuccess = false;

                StartMasternodeEntry(statusObj, mnb, fSuccess, mne, errorMessage, strCommand);

                 if (statusObj.exists("result")) {
                     int successful = 0, failed = 0;
                     SerializeMNB(statusObj, mnb, fSuccess, successful, failed);
                 }

                break;
            }
        }

        if(!found) {
             statusObj.pushKV("alias", alias);
             statusObj.pushKV("result", "failed");
             statusObj.pushKV("error", "Could not find alias in config. Verify with listmasternodeconf.");
        } else if (!statusObj.exists("result")) {
             statusObj.pushKV("alias", alias);
             statusObj.pushKV("result", "failed");
             statusObj.pushKV("error", "Internal error processing alias.");
        }


        return statusObj;
    }

    if (strCommand == "all")
    {
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        std::vector<CMasternodeConfig::CMasternodeEntry> mnEntries;
        mnEntries = masternodeConfig.getEntries();

        int successful = 0;
        int failed = 0;

        UniValue resultsObj(UniValue::VARR);

        for (const auto& mne : mnEntries) {
            UniValue statusObj(UniValue::VOBJ);
            CMasternodeBroadcast mnb;
            std::string errorMessage;
            bool fSuccess = false;

            if (!StartMasternodeEntry(statusObj, mnb, fSuccess, mne, errorMessage, strCommand)){
                 resultsObj.push_back(statusObj);
                 continue;
             }

            SerializeMNB(statusObj, mnb, fSuccess, successful, failed);
            resultsObj.push_back(statusObj);
        }

        UniValue returnObj(UniValue::VOBJ);
        returnObj.pushKV("overall", strprintf("Successfully created broadcast messages for %d masternodes, failed to create %d, total %d", successful, failed, successful + failed));
        returnObj.pushKV("detail", resultsObj);

        return returnObj;
    }
    return NullUniValue;
}

UniValue decodemasternodebroadcast(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "decodemasternodebroadcast \"hexstring\"\n"
            "\nCommand to decode masternode broadcast messages\n"

            "\nArgument:\n"
            "1. \"hexstring\"        (string) The hex encoded masternode broadcast message\n"

            "\nResult:\n"
            "{\n"
            "  \"vin\": \"xxxx\"                (string) The unspent output which is holding the masternode collateral\n"
            "  \"addr\": \"xxxx\"               (string) IP address:port of the masternode\n"
            "  \"pubkeycollateral\": \"xxxx\"   (string) Collateral address\n"
            "  \"pubkeymasternode\": \"xxxx\"   (string) Masternode service public key (hex)\n"
            "  \"vchsig\": \"xxxx\"             (string) Base64-encoded signature of this message (verifiable via pubkeycollateral)\n"
            "  \"sigtime\": nnn,              (numeric) Signature timestamp (Unix epoch)\n"
            "  \"sigvalid\": true|false,      (boolean) Whether or not the mnb signature checks out.\n"
            "  \"protocolversion\": nnn,      (numeric) Masternode's protocol version\n"
            "  \"nlastdsq\": nnn,             (numeric) The last time the masternode sent a DSQ message (for mixing) (DEPRECATED)\n"
            "  \"nMessVersion\": nnn,         (numeric) MNB Message version number\n"
            "  \"lastping\" : {               (object) JSON object with information about the masternode's last ping\n"
            "      \"vin\": \"xxxx\"          (string) The unspent output of the masternode which is signing the message\n"
            "      \"blockhash\": \"xxxx\"    (string) Current chaintip blockhash minus 12\n"
            "      \"sigtime\": nnn,           (numeric) Signature time for this ping (Unix epoch)\n"
            "      \"sigvalid\": true|false,   (boolean) Whether or not the mnp signature checks out.\n"
            "      \"vchsig\": \"xxxx\",       (string) Base64-encoded signature of this ping (verifiable via pubkeymasternode)\n"
            "      \"nMessVersion\": nnn      (numeric) MNP Message version number\n"
            "  }\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("decodemasternodebroadcast", "hexstring") + HelpExampleRpc("decodemasternodebroadcast", "hexstring"));

    CMasternodeBroadcast mnb;

    if (!DecodeHexMnb(mnb, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Masternode broadcast message decode failed");

    // *** FIX: Removed argument from CheckSignature() ***
    bool sigValid = mnb.CheckSignature();


    UniValue resultObj(UniValue::VOBJ);

    resultObj.pushKV("vin", mnb.vin.prevout.ToString());
    resultObj.pushKV("addr", mnb.addr.ToString());
    resultObj.pushKV("pubkeycollateral", CBitcoinAddress(mnb.pubKeyCollateralAddress.GetID()).ToString());
    resultObj.pushKV("pubkeymasternode", HexStr(mnb.pubKeyMasternode));
    resultObj.pushKV("vchsig", mnb.GetSignatureBase64());
    resultObj.pushKV("sigtime", mnb.sigTime);
    resultObj.pushKV("sigvalid", sigValid);
    resultObj.pushKV("protocolversion", mnb.protocolVersion);
    resultObj.pushKV("nlastdsq", mnb.nLastDsq);
    resultObj.pushKV("nMessVersion", mnb.nMessVersion);

    UniValue lastPingObj(UniValue::VOBJ);
    // Pass the correct pubkey (mnb.pubKeyMasternode) to verify the ping signature
    bool pingSigValid = mnb.lastPing.CheckSignature(mnb.pubKeyMasternode);

    lastPingObj.pushKV("vin", mnb.lastPing.vin.prevout.ToString());
    lastPingObj.pushKV("blockhash", mnb.lastPing.blockHash.ToString());
    lastPingObj.pushKV("sigtime", mnb.lastPing.sigTime);
    lastPingObj.pushKV("sigvalid", pingSigValid);
    lastPingObj.pushKV("vchsig", mnb.lastPing.GetSignatureBase64());
    lastPingObj.pushKV("nMessVersion", mnb.lastPing.nMessVersion);

    resultObj.pushKV("lastping", lastPingObj);

    return resultObj;
}

UniValue relaymasternodebroadcast(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "relaymasternodebroadcast \"hexstring\"\n"
            "\nCommand to relay masternode broadcast messages\n"

            "\nArguments:\n"
            "1. \"hexstring\"        (string) The hex encoded masternode broadcast message\n"

            "\nResult:\n"
            "\"status\"     (string) Status message\n"

            "\nExamples:\n" +
            HelpExampleCli("relaymasternodebroadcast", "hexstring") + HelpExampleRpc("relaymasternodebroadcast", "hexstring"));


    CMasternodeBroadcast mnb;

    if (!DecodeHexMnb(mnb, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Masternode broadcast message decode failed");

    // *** FIX: Removed argument from CheckSignature() ***
    if(!mnb.CheckSignature())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Masternode broadcast signature verification failed");

    mnodeman.UpdateMasternodeList(mnb);
    mnb.Relay();

    return strprintf("Masternode broadcast sent (service %s, vin %s)", mnb.addr.ToString(), mnb.vin.ToString());
}