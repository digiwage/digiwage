// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "clientversion.h"
#include "init.h"
#include "main.h"
#include "masternode-sync.h"
#include "net.h"
#include "netbase.h"
#include "rpc/server.h"
#include "spork.h"
#include "timedata.h"
#include "util.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "forgeman.h"
#endif

#include <stdint.h>

#include <boost/assign/list_of.hpp>

#include <univalue.h>

extern std::vector<CSporkDef> sporkDefs;


/**
 * @note Do not add or change anything in the information returned by this
 * method. `getinfo` exists for backwards-compatibility only. It combines
 * information from wildly different sources in the program, which is a mess,
 * and is thus planned to be deprecated eventually.
 *
 * Based on the source of the information, new information should be added to:
 * - `getblockchaininfo`,
 * - `getnetworkinfo` or
 * - `getwalletinfo`
 *
 * Or alternatively, create a specific query method for the information.
 **/
UniValue getinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "getinfo\n"
            "\nReturns an object containing various state info.\n"

            "\nResult:\n"
            "{\n"
            "  \"version\": xxxxx,             (numeric) the server version\n"
            "  \"protocolversion\": xxxxx,     (numeric) the protocol version\n"
            "  \"walletversion\": xxxxx,       (numeric) the wallet version\n"
            "  \"balance\": xxxxxxx,           (numeric) the total DIGIWAGE balance of the wallet\n"
            "  \"staking status\": true|false, (boolean) if the wallet is staking or not\n"
            "  \"blocks\": xxxxxx,             (numeric) the current number of blocks processed in the server\n"
            "  \"timeoffset\": xxxxx,          (numeric) the time offset\n"
            "  \"connections\": xxxxx,         (numeric) the number of connections\n"
            "  \"proxy\": \"host:port\",       (string, optional) the proxy used by the server\n"
            "  \"difficulty\": xxxxxx,         (numeric) the current difficulty\n"
            "  \"testnet\": true|false,        (boolean) if the server is using testnet or not\n"
            "  \"moneysupply\" : \"supply\"    (numeric) The money supply when this block was added to the blockchain\n"
            "  \"zerocoinsupply\" :\n"
            "  {\n"
            "     \"1\" : n,            (numeric) supply of 1 zWAGE denomination\n"
            "     \"5\" : n,            (numeric) supply of 5 zWAGE denomination\n"
            "     \"10\" : n,           (numeric) supply of 10 zWAGE denomination\n"
            "     \"50\" : n,           (numeric) supply of 50 zWAGE denomination\n"
            "     \"100\" : n,          (numeric) supply of 100 zWAGE denomination\n"
            "     \"500\" : n,          (numeric) supply of 500 zWAGE denomination\n"
            "     \"1000\" : n,         (numeric) supply of 1000 zWAGE denomination\n"
            "     \"5000\" : n,         (numeric) supply of 5000 zWAGE denomination\n"
            "     \"total\" : n,        (numeric) The total supply of all zWAGE denominations\n"
            "  }\n"
            "  \"burned\" : \"supply\"         (numeric) The total amount of WAGE burned (In fees and unspendable outputs)\n"
            "  \"keypoololdest\": xxxxxx,      (numeric) the timestamp (seconds since GMT epoch) of the oldest pre-generated key in the key pool\n"
            "  \"keypoolsize\": xxxx,          (numeric) how many new keys are pre-generated\n"
            "  \"unlocked_until\": ttt,        (numeric) the timestamp in seconds since epoch (midnight Jan 1 1970 GMT) that the wallet is unlocked for transfers, or 0 if the wallet is locked\n"
            "  \"paytxfee\": x.xxxx,           (numeric) the transaction fee set in WAGE/kb\n"
            "  \"relayfee\": x.xxxx,           (numeric) minimum relay fee for non-free transactions in WAGE/kb\n"
            "  \"errors\": \"...\"             (string) any error messages\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getinfo", "") + HelpExampleRpc("getinfo", ""));

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    std::string services;
    for (int i = 0; i < 8; i++) {
        uint64_t check = 1 << i;
        if (nLocalServices & check) {
            switch (check) {
                case NODE_NETWORK:
                    services+= "NETWORK/";
                    break;
                case NODE_BLOOM:
                case NODE_BLOOM_WITHOUT_MN:
                    services+= "BLOOM/";
                    break;
                default:
                    services+= "UNKNOWN/";
            }
        }
    }

    proxyType proxy;
    GetProxy(NET_IPV4, proxy);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("version", CLIENT_VERSION);
    obj.pushKV("protocolversion", PROTOCOL_VERSION);
    obj.pushKV("services", services);
#ifdef ENABLE_WALLET
    if (pwalletMain) {
        obj.pushKV("walletversion", pwalletMain->GetVersion());
        obj.pushKV("balance", ValueFromAmount(pwalletMain->GetBalance()));
        obj.pushKV("staking status", (pwalletMain->pStakerStatus->IsActive() ?
                                                "Staking Active" :
                                                "Staking Not Active"));
    }
#endif
    obj.pushKV("blocks", (int)chainActive.Height());
    obj.pushKV("timeoffset", GetTimeOffset());
    obj.pushKV("connections", (int)vNodes.size());
    obj.pushKV("proxy", (proxy.IsValid() ? proxy.proxy.ToStringIPPort() : std::string()));
    obj.pushKV("difficulty", (double)GetDifficulty());
    obj.pushKV("testnet", Params().NetworkID() == CBaseChainParams::TESTNET);
    obj.pushKV("moneysupply",ValueFromAmount(chainActive.Tip()->nMoneySupply));
    UniValue zwageObj(UniValue::VOBJ);
    for (auto denom : libzerocoin::zerocoinDenomList) {
        zwageObj.pushKV(std::to_string(denom), ValueFromAmount(chainActive.Tip()->mapZerocoinSupply.at(denom) * (denom*COIN)));
    }
    zwageObj.pushKV("total", ValueFromAmount(chainActive.Tip()->GetZerocoinSupply()));
    obj.pushKV("zerocoinsupply", zwageObj);
    obj.pushKV("burned", ValueFromAmount(nBurnedCoins));

#ifdef ENABLE_WALLET
    if (pwalletMain) {
        obj.pushKV("keypoololdest", pwalletMain->GetOldestKeyPoolTime());
        obj.pushKV("keypoolsize", (int64_t)pwalletMain->KeypoolCountExternalKeys());
    }
    if (pwalletMain && pwalletMain->IsCrypted())
        obj.pushKV("unlocked_until", nWalletUnlockTime);
    obj.pushKV("paytxfee", ValueFromAmount(payTxFee.GetFeePerK()));
#endif
    obj.pushKV("relayfee", ValueFromAmount(::minRelayTxFee.GetFeePerK()));
    obj.pushKV("errors", GetWarnings("statusbar"));
    return obj;
}

UniValue mnsync(const UniValue& params, bool fHelp)
{
    std::string strMode;
    if (params.size() == 1)
        strMode = params[0].get_str();

    if (fHelp || params.size() != 1 || (strMode != "status" && strMode != "reset")) {
        throw std::runtime_error(
            "mnsync \"status|reset\"\n"
            "\nReturns the sync status or resets sync.\n"

            "\nArguments:\n"
            "1. \"mode\"    (string, required) either 'status' or 'reset'\n"

            "\nResult ('status' mode):\n"
            "{\n"
            "  \"IsBlockchainSynced\": true|false,    (boolean) 'true' if blockchain is synced\n"
            "  \"lastMasternodeList\": xxxx,        (numeric) Timestamp of last MN list message\n"
            "  \"lastMasternodeWinner\": xxxx,      (numeric) Timestamp of last MN winner message\n"
            "  \"lastBudgetItem\": xxxx,            (numeric) Timestamp of last MN budget message\n"
            "  \"lastFailure\": xxxx,           (numeric) Timestamp of last failed sync\n"
            "  \"nCountFailures\": n,           (numeric) Number of failed syncs (total)\n"
            "  \"sumMasternodeList\": n,        (numeric) Number of MN list messages (total)\n"
            "  \"sumMasternodeWinner\": n,      (numeric) Number of MN winner messages (total)\n"
            "  \"sumBudgetItemProp\": n,        (numeric) Number of MN budget messages (total)\n"
            "  \"sumBudgetItemFin\": n,         (numeric) Number of MN budget finalization messages (total)\n"
            "  \"countMasternodeList\": n,      (numeric) Number of MN list messages (local)\n"
            "  \"countMasternodeWinner\": n,    (numeric) Number of MN winner messages (local)\n"
            "  \"countBudgetItemProp\": n,      (numeric) Number of MN budget messages (local)\n"
            "  \"countBudgetItemFin\": n,       (numeric) Number of MN budget finalization messages (local)\n"
            "  \"RequestedMasternodeAssets\": n, (numeric) Status code of last sync phase\n"
            "  \"RequestedMasternodeAttempt\": n, (numeric) Status code of last sync attempt\n"
            "}\n"

            "\nResult ('reset' mode):\n"
            "\"status\"     (string) 'success'\n"

            "\nExamples:\n" +
            HelpExampleCli("mnsync", "\"status\"") + HelpExampleRpc("mnsync", "\"status\""));
    }

    if (strMode == "status") {
        UniValue obj(UniValue::VOBJ);

        obj.pushKV("IsBlockchainSynced", masternodeSync.IsBlockchainSynced());
        obj.pushKV("lastMasternodeList", masternodeSync.lastMasternodeList);
        obj.pushKV("lastMasternodeWinner", masternodeSync.lastMasternodeWinner);
        obj.pushKV("lastBudgetItem", masternodeSync.lastBudgetItem);
        obj.pushKV("lastFailure", masternodeSync.lastFailure);
        obj.pushKV("nCountFailures", masternodeSync.nCountFailures);
        obj.pushKV("sumMasternodeList", masternodeSync.sumMasternodeList);
        obj.pushKV("sumMasternodeWinner", masternodeSync.sumMasternodeWinner);
        obj.pushKV("sumBudgetItemProp", masternodeSync.sumBudgetItemProp);
        obj.pushKV("sumBudgetItemFin", masternodeSync.sumBudgetItemFin);
        obj.pushKV("countMasternodeList", masternodeSync.countMasternodeList);
        obj.pushKV("countMasternodeWinner", masternodeSync.countMasternodeWinner);
        obj.pushKV("countBudgetItemProp", masternodeSync.countBudgetItemProp);
        obj.pushKV("countBudgetItemFin", masternodeSync.countBudgetItemFin);
        obj.pushKV("RequestedMasternodeAssets", masternodeSync.RequestedMasternodeAssets);
        obj.pushKV("RequestedMasternodeAttempt", masternodeSync.RequestedMasternodeAttempt);

        return obj;
    }

    if (strMode == "reset") {
        masternodeSync.Reset();
        return "success";
    }
    return "failure";
}

#ifdef ENABLE_WALLET
class DescribeAddressVisitor : public boost::static_visitor<UniValue>
{
private:
    isminetype mine;

public:
    DescribeAddressVisitor(isminetype mineIn) : mine(mineIn) {}

    UniValue operator()(const CNoDestination &dest) const { return UniValue(UniValue::VOBJ); }

    UniValue operator()(const CKeyID &keyID) const {
        UniValue obj(UniValue::VOBJ);
        CPubKey vchPubKey;
        obj.pushKV("isscript", false);
        if (bool(mine & ISMINE_ALL)) {
            pwalletMain->GetPubKey(keyID, vchPubKey);
            obj.pushKV("pubkey", HexStr(vchPubKey));
            obj.pushKV("iscompressed", vchPubKey.IsCompressed());
        }
        return obj;
    }

    UniValue operator()(const CScriptID &scriptID) const {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("isscript", true);
        CScript subscript;
        pwalletMain->GetCScript(scriptID, subscript);
        std::vector<CTxDestination> addresses;
        txnouttype whichType;
        int nRequired;
        ExtractDestinations(subscript, whichType, addresses, nRequired);
        obj.pushKV("script", GetTxnOutputType(whichType));
        obj.pushKV("hex", HexStr(subscript.begin(), subscript.end()));
        UniValue a(UniValue::VARR);
        for (const CTxDestination& addr : addresses)
            a.push_back(CBitcoinAddress(addr).ToString());
        obj.pushKV("addresses", a);
        if (whichType == TX_MULTISIG)
            obj.pushKV("sigsrequired", nRequired);
        return obj;
    }
};
#endif

/*
    Used for updating/reading spork settings on the network
*/
UniValue spork(const UniValue& params, bool fHelp)
{
    if (params.size() == 1 && params[0].get_str() == "show") {
        UniValue ret(UniValue::VOBJ);
        for (const auto& sporkDef : sporkDefs) {
            ret.pushKV(sporkDef.name, sporkManager.GetSporkValue(sporkDef.sporkId));
        }
        return ret;
    } else if (params.size() == 1 && params[0].get_str() == "active") {
        UniValue ret(UniValue::VOBJ);
        for (const auto& sporkDef : sporkDefs) {
            ret.pushKV(sporkDef.name, sporkManager.IsSporkActive(sporkDef.sporkId));
        }
        return ret;
    } else if (params.size() == 2) {
        // advanced mode, update spork values
        SporkId nSporkID = sporkManager.GetSporkIDByName(params[0].get_str());
        if (nSporkID == SPORK_INVALID) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid spork name");
        }

        // SPORK VALUE
        int64_t nValue = params[1].getInt<int64_t>();

        //broadcast new spork
        if (sporkManager.UpdateSpork(nSporkID, nValue)) {
            return "success";
        } else {
            return "failure";
        }
    }

    throw std::runtime_error(
        "spork \"name\" ( value )\n"
        "\nReturn spork values or their active state.\n"

        "\nArguments:\n"
        "1. \"name\"        (string, required)  \"show\" to show values, \"active\" to show active state.\n"
        "                       When set up as a spork signer, the name of the spork can be used to update it's value.\n"
        "2. value           (numeric, required when updating a spork) The new value for the spork.\n"

        "\nResult (show):\n"
        "{\n"
        "  \"spork_name\": nnn      (key/value) Key is the spork name, value is it's current value.\n"
        "  ,...\n"
        "}\n"

        "\nResult (active):\n"
        "{\n"
        "  \"spork_name\": true|false      (key/value) Key is the spork name, value is a boolean for it's active state.\n"
        "  ,...\n"
        "}\n"

        "\nResult (name):\n"
        " \"success|failure\"       (string) Whether or not the update succeeded.\n"

        "\nExamples:\n" +
        HelpExampleCli("spork", "show") + HelpExampleRpc("spork", "show"));
}

UniValue validateaddress(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "validateaddress \"digiwageaddress\"\n"
            "\nReturn information about the given DIGIWAGE address.\n"

            "\nArguments:\n"
            "1. \"digiwageaddress\"     (string, required) The DIGIWAGE address to validate\n"

            "\nResult:\n"
            "{\n"
            "  \"isvalid\" : true|false,         (boolean) If the address is valid or not. If not, this is the only property returned.\n"
            "  \"address\" : \"digiwageaddress\",    (string) The DIGIWAGE address validated\n"
            "  \"scriptPubKey\" : \"hex\",       (string) The hex encoded scriptPubKey generated by the address\n"
            "  \"ismine\" : true|false,          (boolean) If the address is yours or not\n"
            "  \"isstaking\" : true|false,       (boolean) If the address is a staking address for DIGIWAGE cold staking\n"
            "  \"iswatchonly\" : true|false,     (boolean) If the address is watchonly\n"
            "  \"isscript\" : true|false,        (boolean) If the key is a script\n"
            "  \"hex\" : \"hex\",                (string, optional) The redeemscript for the P2SH address\n"
            "  \"pubkey\" : \"publickeyhex\",    (string) The hex value of the raw public key\n"
            "  \"iscompressed\" : true|false,    (boolean) If the address is compressed\n"
            "  \"account\" : \"account\"         (string) DEPRECATED. The account associated with the address, \"\" is the default account\n"
            "  \"hdkeypath\" : \"keypath\"       (string, optional) The HD keypath if the key is HD and available\n"
            "  \"hdchainid\" : \"<hash>\"        (string, optional) The ID of the HD chain\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("validateaddress", "\"1PSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\"") + HelpExampleRpc("validateaddress", "\"1PSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\""));

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    CBitcoinAddress address(params[0].get_str());
    bool isValid = address.IsValid();

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("isvalid", isValid);
    if (isValid) {
        CTxDestination dest = address.Get();
        std::string currentAddress = address.ToString();
        ret.pushKV("address", currentAddress);
        CScript scriptPubKey = GetScriptForDestination(dest);
        ret.pushKV("scriptPubKey", HexStr(scriptPubKey.begin(), scriptPubKey.end()));

#ifdef ENABLE_WALLET
        isminetype mine = pwalletMain ? IsMine(*pwalletMain, dest) : ISMINE_NO;
        ret.pushKV("ismine", bool(mine & (ISMINE_SPENDABLE_ALL | ISMINE_COLD)));
        ret.pushKV("isstaking", address.IsStakingAddress());
        // Remove extra closing parenthesis from this line
        ret.pushKV("iswatchonly", bool(mine & ISMINE_WATCH_ONLY));
        UniValue detail = boost::apply_visitor(DescribeAddressVisitor(mine), dest);
        ret.pushKVs(detail);
        if (pwalletMain && pwalletMain->mapAddressBook.count(dest))
            ret.pushKV("account", pwalletMain->mapAddressBook[dest].name);
        CKeyID keyID;
        CHDChain hdChainCurrent;
        if (pwalletMain && address.GetKeyID(keyID) && pwalletMain->mapHdPubKeys.count(keyID) && pwalletMain->GetHDChain(hdChainCurrent))
        {
            ret.pushKV("hdkeypath", pwalletMain->mapHdPubKeys[keyID].GetKeyPath());
            ret.pushKV("hdchainid", hdChainCurrent.GetID().GetHex());
        }
#endif
    }
    return ret;
}

/**
 * Used by addmultisigaddress / createmultisig:
 */
CScript _createmultisig_redeemScript(const UniValue& params)
{
    int nRequired = params[0].getInt<int>();
    const UniValue& keys = params[1].get_array();

    // Gather public keys
    if (nRequired < 1)
        throw std::runtime_error("a multisignature address must require at least one key to redeem");
    if ((int)keys.size() < nRequired)
        throw std::runtime_error(
            strprintf("not enough keys supplied "
                      "(got %u keys, but need at least %d to redeem)",
                keys.size(), nRequired));
    if (keys.size() > 16)
        throw std::runtime_error("Number of addresses involved in the multisignature address creation > 16\nReduce the number");
    std::vector<CPubKey> pubkeys;
    pubkeys.resize(keys.size());
    for (unsigned int i = 0; i < keys.size(); i++) {
        const std::string& ks = keys[i].get_str();
#ifdef ENABLE_WALLET
        // Case 1: DIGIWAGE address and we have full public key:
        CBitcoinAddress address(ks);
        if (pwalletMain && address.IsValid()) {
            CKeyID keyID;
            if (!address.GetKeyID(keyID))
                throw std::runtime_error(
                    strprintf("%s does not refer to a key", ks));
            CPubKey vchPubKey;
            if (!pwalletMain->GetPubKey(keyID, vchPubKey))
                throw std::runtime_error(
                    strprintf("no full public key for address %s", ks));
            if (!vchPubKey.IsFullyValid())
                throw std::runtime_error(" Invalid public key: " + ks);
            pubkeys[i] = vchPubKey;
        }

        // Case 2: hex public key
        else
#endif
            if (IsHex(ks)) {
            CPubKey vchPubKey(ParseHex(ks));
            if (!vchPubKey.IsFullyValid())
                throw std::runtime_error(" Invalid public key: " + ks);
            pubkeys[i] = vchPubKey;
        } else {
            throw std::runtime_error(" Invalid public key: " + ks);
        }
    }
    CScript result = GetScriptForMultisig(nRequired, pubkeys);

    if (result.size() > MAX_SCRIPT_ELEMENT_SIZE)
        throw std::runtime_error(
            strprintf("redeemScript exceeds size limit: %d > %d", result.size(), MAX_SCRIPT_ELEMENT_SIZE));

    return result;
}

UniValue createmultisig(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 2)
        throw std::runtime_error(
            "createmultisig nrequired [\"key\",...]\n"
            "\nCreates a multi-signature address with n signature of m keys required.\n"
            "It returns a json object with the address and redeemScript.\n"

            "\nArguments:\n"
            "1. nrequired      (numeric, required) The number of required signatures out of the n keys or addresses.\n"
            "2. \"keys\"       (string, required) A json array of keys which are DIGIWAGE addresses or hex-encoded public keys\n"
            "     [\n"
            "       \"key\"    (string) DIGIWAGE address or hex-encoded public key\n"
            "       ,...\n"
            "     ]\n"

            "\nResult:\n"
            "{\n"
            "  \"address\":\"multisigaddress\",  (string) The value of the new multisig address.\n"
            "  \"redeemScript\":\"script\"       (string) The string value of the hex-encoded redemption script.\n"
            "}\n"

            "\nExamples:\n"
            "\nCreate a multisig address from 2 addresses\n" +
            HelpExampleCli("createmultisig", "2 \"[\\\"16sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"171sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"") +
            "\nAs a json rpc call\n" +
            HelpExampleRpc("createmultisig", "2, \"[\\\"16sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"171sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\""));

    // Construct using pay-to-script-hash:
    CScript inner = _createmultisig_redeemScript(params);
    CScriptID innerID(inner);
    CBitcoinAddress address(innerID);

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", address.ToString());
    result.pushKV("redeemScript", HexStr(inner.begin(), inner.end()));

    return result;
}

UniValue verifymessage(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw std::runtime_error(
            "verifymessage \"digiwageaddress\" \"signature\" \"message\"\n"
            "\nVerify a signed message\n"

            "\nArguments:\n"
            "1. \"digiwageaddress\"  (string, required) The DIGIWAGE address to use for the signature.\n"
            "2. \"signature\"       (string, required) The signature provided by the signer in base 64 encoding (see signmessage).\n"
            "3. \"message\"         (string, required) The message that was signed.\n"

            "\nResult:\n"
            "true|false   (boolean) If the signature is verified or not.\n"

            "\nExamples:\n"
            "\nUnlock the wallet for 30 seconds\n" +
            HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
            "\nCreate the signature\n" +
            HelpExampleCli("signmessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" \"my message\"") +
            "\nVerify the signature\n" +
            HelpExampleCli("verifymessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" \"signature\" \"my message\"") +
            "\nAs json rpc\n" +
            HelpExampleRpc("verifymessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\", \"signature\", \"my message\""));

    LOCK(cs_main);

    std::string strAddress = params[0].get_str();
    std::string strSign = params[1].get_str();
    std::string strMessage = params[2].get_str();

    CBitcoinAddress addr(strAddress);
    if (!addr.IsValid())
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");

    CKeyID keyID;
    if (!addr.GetKeyID(keyID))
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");

    bool fInvalid = false;
    std::vector<unsigned char> vchSig = DecodeBase64(strSign.c_str(), &fInvalid);

    if (fInvalid)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Malformed base64 encoding");

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig))
        return false;

    return (pubkey.GetID() == keyID);
}

UniValue setmocktime(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "setmocktime timestamp\n"
            "\nSet the local time to given timestamp (-regtest only)\n"

            "\nArguments:\n"
            "1. timestamp  (integer, required) Unix seconds-since-epoch timestamp\n"
            "   Pass 0 to go back to using the system time.");

    if (!Params().IsRegTestNet())
        throw std::runtime_error("setmocktime for regression testing (-regtest mode) only");

    LOCK(cs_main);

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM));
    SetMockTime(params[0].getInt<int64_t>());

    return NullUniValue;
}

#ifdef ENABLE_WALLET
UniValue getstakingstatus(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "getstakingstatus\n"
            "\nReturns an object containing various staking information.\n"

            "\nResult:\n"
            "{\n"
            "  \"staking_status\": true|false,      (boolean) whether the wallet is staking or not\n"
            "  \"staking_enabled\": true|false,     (boolean) whether staking is enabled/disabled in digiwage.conf\n"
            "  \"coldstaking_enabled\": true|false, (boolean) whether cold-staking is enabled/disabled in digiwage.conf\n"
            "  \"haveconnections\": true|false,     (boolean) whether network connections are present\n"
            "  \"mnsync\": true|false,              (boolean) whether the required masternode/spork data is synced\n"
            "  \"walletunlocked\": true|false,      (boolean) whether the wallet is unlocked\n"
            "  \"stakeablecoins\": n                (numeric) number of stakeable UTXOs\n"
            "  \"stakingbalance\": d                (numeric) WAGE value of the stakeable coins (minus reserve balance, if any)\n"
            "  \"stakesplitthreshold\": d           (numeric) value of the current threshold for stake split\n"
            "  \"lastattempt_age\": n               (numeric) seconds since last stake attempt\n"
            "  \"lastattempt_depth\": n             (numeric) depth of the block on top of which the last stake attempt was made\n"
            "  \"lastattempt_hash\": xxx            (hex string) hash of the block on top of which the last stake attempt was made\n"
            "  \"lastattempt_coins\": n             (numeric) number of stakeable coins available during last stake attempt\n"
            "  \"lastattempt_tries\": n             (numeric) number of stakeable coins checked during last stake attempt\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getstakingstatus", "") + HelpExampleRpc("getstakingstatus", ""));


    if (!pwalletMain)
        throw JSONRPCError(RPC_IN_WARMUP, "Try again after active chain is loaded");
    {
        LOCK2(cs_main, &pwalletMain->cs_wallet);
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("staking_status", pwalletMain->pStakerStatus->IsActive());
        obj.pushKV("staking_enabled", GetBoolArg("-staking", true));
        bool fColdStaking = GetBoolArg("-coldstaking", true);
        obj.pushKV("coldstaking_enabled", fColdStaking);
        obj.pushKV("haveconnections", !vNodes.empty());
        obj.pushKV("mnsync", !masternodeSync.NotCompleted());
        obj.pushKV("walletunlocked", !pwalletMain->IsLocked());
        std::vector<COutput> vCoins;
        pwalletMain->StakeableCoins(&vCoins);
        obj.pushKV("stakeablecoins", (int)vCoins.size());
        obj.pushKV("stakingbalance", ValueFromAmount(pwalletMain->GetStakingBalance(fColdStaking)));
        obj.pushKV("stakesplitthreshold", ValueFromAmount(pwalletMain->nStakeSplitThreshold));
        CStakerStatus* ss = pwalletMain->pStakerStatus;
        if (ss) {
            obj.pushKV("lastattempt_age", (int)(GetTime() - ss->GetLastTime()));
            obj.pushKV("lastattempt_depth", (chainActive.Height() - ss->GetLastHeight()));
            obj.pushKV("lastattempt_hash", ss->GetLastHash().GetHex());
            obj.pushKV("lastattempt_coins", ss->GetLastCoins());
            obj.pushKV("lastattempt_tries", ss->GetLastTries());
        }
        return obj;
    }


}

UniValue listforgeitems(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "listforgeitems\n"
            "\nReturns an array containing the list of owned ZFIs.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"name\": xxx,                       (string) the plaintext name of the ZFI\n"
            "    \"tx\": xxx,                         (hex string) the TX hash of the ZFI\n"
            "    \"vout\": n,                         (numeric) the vout involving the TX of the ZFI\n"
            "  } ...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listforgeitems", "") + HelpExampleRpc("listforgeitems", ""));


    if (!pwalletMain)
        throw JSONRPCError(RPC_IN_WARMUP, "Try again after active chain is loaded");
    {
        LOCK2(cs_main, &pwalletMain->cs_wallet);
        UniValue results(UniValue::VARR);
        for (const CForge::CForgeItem& item : forgeMain.getEntries()) {
            UniValue obj(UniValue::VOBJ);
            obj.pushKV("name", item.getName());
            obj.pushKV("tx", item.getTxHash());
            obj.pushKV("vout", item.getOutputIndex());
            results.push_back(obj);
        }
        return results;
    }


}
#endif // ENABLE_WALLET