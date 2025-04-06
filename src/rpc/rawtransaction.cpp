// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "core_io.h"
#include "init.h"
#include "keystore.h"
#include "main.h"
#include "net.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "swifttx.h"
#include "uint256.h"
#include "utilmoneystr.h"
#include "util.h" // Needed for FormatStateMessage (or fallback)
#include "coins.h" // Needed for CCoinsViewCache, CCoins
#include "script/interpreter.h" // Needed for SignatureChecker classes

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <stdint.h>

#include <boost/assign/list_of.hpp> // Keep for RPCTypeCheck list_of

#include <univalue.h>


void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex)
{
    txnouttype type;
    std::vector<CTxDestination> addresses;
    int nRequired;

    out.pushKV("asm", scriptPubKey.ToString());
    if (fIncludeHex)
        out.pushKV("hex", HexStr(scriptPubKey.begin(), scriptPubKey.end()));

    if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired)) {
        out.pushKV("type", GetTxnOutputType(type));
        return;
    }

    out.pushKV("reqSigs", nRequired);
    out.pushKV("type", GetTxnOutputType(type));

    UniValue a(UniValue::VARR);
    for (const CTxDestination& addr : addresses)
        a.push_back(CBitcoinAddress(addr).ToString());
    out.pushKV("addresses", a);
}

void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry)
{
    TxToUniv(tx, uint256(), entry);

    if (!hashBlock.IsNull()) {
        entry.pushKV("blockhash", hashBlock.GetHex());
        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                entry.pushKV("confirmations", 1 + chainActive.Height() - pindex->nHeight);
                entry.pushKV("time", pindex->GetBlockTime());
                entry.pushKV("blocktime", pindex->GetBlockTime());
            }
            else
                entry.pushKV("confirmations", 0);
        }
    }
    entry.pushKV("txid", tx.GetHash().GetHex());
    entry.pushKV("size", (int)::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION));
    // Add hash/vsize if applicable and not added by TxToUniv
    // entry.pushKV("hash", tx.GetWitnessHash().GetHex()); // If witness is supported
    // entry.pushKV("vsize", GetVirtualTransactionSize(tx)); // If witness is supported
}

UniValue getrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
        throw std::runtime_error(
            "getrawtransaction \"txid\" ( verbose \"blockhash\" )\n"

            "\nNOTE: By default this function only works sometimes. This is when the tx is in the mempool\n"
            "or there is an unspent output in the utxo for this transaction. To make it always work,\n"
            "you need to maintain a transaction index, using the -txindex command line option.\n"

            "\nReturn the raw transaction data.\n"
            "\nIf verbose is 'true', returns an Object with information about 'txid'.\n"
            "If verbose is 'false' or omitted, returns a string that is serialized, hex-encoded data for 'txid'.\n"

            "\nArguments:\n"
            "1. \"txid\"      (string, required) The transaction id\n"
            "2. verbose     (bool, optional, default=false) If false, return a string, otherwise return a json object\n"
            "3. \"blockhash\" (string, optional) The block in which to look for the transaction\n"

            "\nResult (if verbose is not set or set to false):\n"
            "\"data\"      (string) The serialized, hex-encoded data for 'txid'\n"

            "\nResult (if verbose is set to true):\n"
            "{\n"
            "  \"in_active_chain\": b, (bool) Whether specified block is in the active chain or not (only present with explicit \"blockhash\" argument)\n"
            "  \"hex\" : \"data\",       (string) The serialized, hex-encoded data for 'txid'\n"
            "  \"txid\" : \"id\",        (string) The transaction id (same as provided)\n"
            "  \"size\" : n,             (numeric) The serialized transaction size\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) \n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n      (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [              (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in WAGE\n" // Adjusted currency
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric, optional) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string, optional)\n"
            "           \"address\"        (string) DIGIWAGE address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"blockhash\" : \"hash\",   (string) the block hash\n"
            "  \"confirmations\" : n,      (numeric) The confirmations\n"
            "  \"time\" : ttt,             (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"blocktime\" : ttt         (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("getrawtransaction", "\"mytxid\"")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" true")
            + HelpExampleRpc("getrawtransaction", "\"mytxid\", true")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" false \"myblockhash\"")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" true \"myblockhash\"")
        );

    LOCK(cs_main);

    bool in_active_chain = true;
    uint256 hash = ParseHashV(params[0], "parameter 1");
    CBlockIndex* blockindex = nullptr;

    bool fVerbose = false;
    if (params.size() > 1 && !params[1].isNull()) {
        fVerbose = params[1].isNum() ? (params[1].getInt<int>() != 0) : params[1].get_bool();
    }

    if (params.size() > 2 && !params[2].isNull()) {
        uint256 blockhash = ParseHashV(params[2], "parameter 3");
        BlockMap::iterator it = mapBlockIndex.find(blockhash);
        if (it == mapBlockIndex.end()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block hash not found");
        }
        blockindex = it->second;
        in_active_chain = chainActive.Contains(blockindex);
    }

    CTransaction tx;
    uint256 hash_block;
    if (!GetTransaction(hash, tx, hash_block, true, blockindex)) {
        std::string errmsg;
        if (blockindex) {
            if (!(blockindex->nStatus & BLOCK_HAVE_DATA)) {
                throw JSONRPCError(RPC_MISC_ERROR, "Block not available");
            }
            errmsg = "No such transaction found in the provided block";
        } else {
            errmsg = fTxIndex
              ? "No such mempool or blockchain transaction"
              : "No such mempool transaction. Use -txindex to enable blockchain transaction queries";
        }
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, errmsg + ". Use gettransaction for wallet transactions.");
    }

    if (!fVerbose) {
        return EncodeHexTx(tx);
    }

    UniValue result(UniValue::VOBJ);
    if (blockindex) result.pushKV("in_active_chain", in_active_chain);
    TxToJSON(tx, hash_block, result);
    return result;
}

#ifdef ENABLE_WALLET
UniValue listunspent(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 4)
        throw std::runtime_error(
            "listunspent ( minconf maxconf  [\"address\",...] watchonlyconfig )\n"
            "\nReturns array of unspent transaction outputs\n"
            "with between minconf and maxconf (inclusive) confirmations.\n"
            "Optionally filter to only include txouts paid to specified addresses.\n"
            "Results are an array of Objects, each of which has:\n"
            "{txid, vout, address, account, scriptPubKey, redeemScript, amount, confirmations, spendable}\n" // Removed solvable

            "\nArguments:\n"
            "1. minconf          (numeric, optional, default=1) The minimum confirmations to filter\n"
            "2. maxconf          (numeric, optional, default=9999999) The maximum confirmations to filter\n"
            "3. \"addresses\"    (string, optional) A json array of DIGIWAGE addresses to filter\n"
            "    [\n"
            "      \"address\"   (string) DIGIWAGE address\n"
            "      ,...\n"
            "    ]\n"
            "4. watchonlyconfig  (numeric, optional, default=1) 1 = list regular unspent transactions, 2 = list only watchonly transactions,  3 = list all unspent transactions (including watchonly)\n"

            "\nResult\n"
            "[                   (array of json object)\n"
            "  {\n"
            "    \"txid\" : \"txid\",        (string) the transaction id\n"
            "    \"vout\" : n,               (numeric) the vout value\n"
            "    \"address\" : \"address\",  (string) the DIGIWAGE address\n"
            "    \"account\" : \"account\",  (string) DEPRECATED. The associated account, or \"\" for the default account\n"
            "    \"scriptPubKey\" : \"key\", (string) the script key\n"
            "    \"redeemScript\" : \"key\", (string, optional) the redeemscript key (present if P2SH)\n"
            "    \"amount\" : x.xxx,         (numeric) the transaction amount in WAGE\n" // Adjusted currency
            "    \"confirmations\" : n,      (numeric) The number of confirmations\n"
            "    \"spendable\" : true|false  (boolean) Whether we have the private keys to spend this output\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples\n" +
            HelpExampleCli("listunspent", "") + HelpExampleCli("listunspent", "6 9999999 '[\"1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\",\"1LtvqCaApEdUGFkpKMM4MstjcaL4dKg8SP\"]'") + HelpExampleRpc("listunspent", "6, 9999999, [\"1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\",\"1LtvqCaApEdUGFkpKMM4MstjcaL4dKg8SP\"]") + // Original Example Addresses
            HelpExampleCli("listunspent", "1 9999999 '[]' 3") + HelpExampleRpc("listunspent", "1, 9999999, [], 3")); // Watchonly example

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM)(UniValue::VNUM)(UniValue::VARR)(UniValue::VNUM), true);

    int nMinDepth = 1;
    if (params.size() > 0 && !params[0].isNull())
        nMinDepth = params[0].getInt<int>();

    int nMaxDepth = 9999999;
    if (params.size() > 1 && !params[1].isNull())
        nMaxDepth = params[1].getInt<int>();

    std::set<CBitcoinAddress> setAddress;
    if (params.size() > 2 && !params[2].isNull()) {
        UniValue inputs = params[2].get_array();
        for (unsigned int inx = 0; inx < inputs.size(); inx++) {
            const UniValue& input = inputs[inx];
            CBitcoinAddress address(input.get_str());
            if (!address.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid DIGIWAGE address: ") + input.get_str());
            if (setAddress.count(address))
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + input.get_str());
            setAddress.insert(address);
        }
    }

    int nWatchonlyConfig = 1;
    if(params.size() > 3 && !params[3].isNull()) {
        nWatchonlyConfig = params[3].getInt<int>();
        if (nWatchonlyConfig > 3 || nWatchonlyConfig < 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid watchonlyconfig parameter, must be 1, 2 or 3");
    }

    UniValue results(UniValue::VARR);
    std::vector<COutput> vecOutputs;
    assert(pwalletMain != NULL);
    LOCK2(cs_main, pwalletMain->cs_wallet);

    bool fIncludeWatchOnly = (nWatchonlyConfig == 2 || nWatchonlyConfig == 3);
    pwalletMain->AvailableCoins(&vecOutputs, true, NULL, false, ALL_COINS, fIncludeWatchOnly);

    bool fOnlySpendable = (nWatchonlyConfig == 1);

    for (const COutput& out : vecOutputs) {
        if (out.nDepth < nMinDepth || out.nDepth > nMaxDepth)
            continue;

        if (fOnlySpendable && !out.fSpendable)
            continue;
        if (nWatchonlyConfig == 2 && out.fSpendable)
             continue;

        if (!out.tx || (unsigned int)out.i >= out.tx->vout.size()) continue;

        const CTxOut& txout = out.tx->vout[out.i];

        CTxDestination addressDest;
        bool addressMatches = true;
        if (!setAddress.empty()) {
            if (!ExtractDestination(txout.scriptPubKey, addressDest) || !setAddress.count(addressDest)) {
                addressMatches = false;
            }
        }
        if (!addressMatches) continue;


        CAmount nValue = txout.nValue;
        const CScript& pk = txout.scriptPubKey;

        UniValue entry(UniValue::VOBJ);
        entry.pushKV("txid", out.tx->GetHash().GetHex());
        entry.pushKV("vout", out.i);

        if (ExtractDestination(pk, addressDest)) {
            entry.pushKV("address", CBitcoinAddress(addressDest).ToString());
            if (pwalletMain->mapAddressBook.count(addressDest)) {
                entry.pushKV("account", pwalletMain->mapAddressBook.at(addressDest).name);
            } else {
                 entry.pushKV("account", "");
            }
        } else {
             entry.pushKV("account", "");
        }

        entry.pushKV("scriptPubKey", HexStr(pk.begin(), pk.end()));

        if (pk.IsPayToScriptHash()) {
            CTxDestination scriptDest;
            if (ExtractDestination(pk, scriptDest)) {
                 const CScriptID* scriptID = boost::get<CScriptID>(&scriptDest);
                 if (scriptID) {
                     CScript redeemScript;
                     if (pwalletMain->GetCScript(*scriptID, redeemScript))
                         entry.pushKV("redeemScript", HexStr(redeemScript.begin(), redeemScript.end()));
                 }
            }
        }

        entry.pushKV("amount", ValueFromAmount(nValue));
        entry.pushKV("confirmations", out.nDepth);
        entry.pushKV("spendable", out.fSpendable);

        results.push_back(entry);
    }

    return results;
}
#endif

UniValue createrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 3)
        throw std::runtime_error(
            "createrawtransaction [{\"txid\":\"id\",\"vout\":n,...},...] {\"address\":amount,...} ( locktime )\n"
            "\nCreate a transaction spending the given inputs and sending to the given addresses.\n"
            "Returns hex-encoded raw transaction.\n"
            "Note that the transaction's inputs are not signed, and\n"
            "it is not stored in the wallet or transmitted to the network.\n"

            "\nArguments:\n"
            "1. \"inputs\"                (array, required) A json array of json objects\n" // Renamed for clarity
            "     [\n"
            "       {\n"
            "         \"txid\":\"id\",      (string, required) The transaction id\n"
            "         \"vout\":n,           (numeric, required) The output number\n"
            "         \"sequence\":n        (numeric, optional) The sequence number\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "2. \"outputs\"               (object, required) A json object with addresses as keys and amounts as values\n" // Renamed for clarity
            "    {\n"
            "      \"address\": amount,    (numeric or string, required) The key is the DIGIWAGE address, the value is the WAGE amount\n" // Adjusted currency, allow string amount
            "      \"data\": \"hex\"         (string, required) A key named 'data' represents an OP_RETURN output, the value is the hex-encoded data\n" // Added OP_RETURN example
            "      ,...\n"
            "    }\n"
            "3. locktime                  (numeric, optional, default=0) Raw locktime. Non-0 value also locktime-activates inputs\n"

            "\nResult:\n"
            "\"transaction\"            (string) hex string of the transaction\n"

            "\nExamples:\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":0.01}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":0.01,\\\"data\\\":\\\"68656c6c6f776f726c64\\\"}\"") // Added OP_RETURN example
            + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"{\\\"address\\\":0.01}\""));

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR)(UniValue::VOBJ)(UniValue::VNUM), true);
    if (params[0].isNull() || params[1].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, arguments 1 and 2 must be non-null");

    UniValue inputs = params[0].get_array();
    UniValue sendTo = params[1].get_obj();

    CMutableTransaction rawTx;

    if (params.size() > 2 && !params[2].isNull()) {
        int64_t nLockTime = params[2].getInt<int64_t>();
        if (nLockTime < 0 || nLockTime > std::numeric_limits<uint32_t>::max())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, locktime out of range");
        rawTx.nLockTime = nLockTime;
    }

    for (unsigned int idx = 0; idx < inputs.size(); idx++) {
        const UniValue& input = inputs[idx];
        const UniValue& o = input.get_obj();

        const UniValue& txidValue = find_value(o, "txid");
        if (txidValue.isNull() || !txidValue.isStr())
             throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing or non-string txid");
        uint256 txid = ParseHashV(txidValue, "txid");


        const UniValue& vout_v = find_value(o, "vout");
        if (!vout_v.isNum())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        int nOutput = vout_v.getInt<int>();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        uint32_t nSequence = (rawTx.nLockTime ? std::numeric_limits<uint32_t>::max() - 1 : std::numeric_limits<uint32_t>::max());
        const UniValue& sequenceObj = find_value(o, "sequence");
        if (!sequenceObj.isNull()) {
             if(sequenceObj.isNum()) {
                int64_t seqNr64 = sequenceObj.getInt<int64_t>();
                if (seqNr64 < 0 || seqNr64 > std::numeric_limits<uint32_t>::max())
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, sequence number is out of range");
                else
                    nSequence = (uint32_t)seqNr64;
             } else {
                 throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, sequence must be a number");
             }
        }


        CTxIn in(COutPoint(txid, nOutput), CScript(), nSequence);

        rawTx.vin.push_back(in);
    }

    std::set<CBitcoinAddress> setAddress;
    std::vector<std::string> addrList = sendTo.getKeys();
    for (const std::string& name_ : addrList) {
        const UniValue& value = find_value(sendTo, name_);
         if(value.isNull()) continue;

        if (name_ == "data") {
            if (!value.isStr())
                 throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, data must be a hex string");
            std::vector<unsigned char> data = ParseHexV(value, "Data");

            CTxOut out(0, CScript() << OP_RETURN << data);
            rawTx.vout.push_back(out);
        } else {
            CBitcoinAddress address(name_);
            if (!address.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid DIGIWAGE address: ") + name_);

            if (setAddress.count(address))
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + name_);
            setAddress.insert(address);

            CScript scriptPubKey = GetScriptForDestination(address.Get());
            CAmount nAmount = AmountFromValue(value);

            CTxOut out(nAmount, scriptPubKey);
            rawTx.vout.push_back(out);
        }
    }

    return EncodeHexTx(rawTx);
}

UniValue decoderawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "decoderawtransaction \"hexstring\"\n"
            "\nReturn a JSON object representing the serialized, hex-encoded transaction.\n"

            "\nArguments:\n"
            "1. \"hexstring\"      (string, required) The transaction hex string\n"

            "\nResult:\n"
            "{\n"
            "  \"txid\" : \"id\",        (string) The transaction id\n"
            "  \"hash\" : \"id\",        (string) The transaction hash (differs from txid for witness transactions)\n" // Added hash
            "  \"size\" : n,             (numeric) The transaction size\n"
            "  \"vsize\" : n,            (numeric) The virtual transaction size (differs from size for witness transactions)\n" // Added vsize
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) The output number\n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n,     (numeric) The script sequence number\n"
            "       \"txinwitness\": [\"hex\", ...] (array of strings) hex-encoded witness data (if any)\n" // Added witness if applicable
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [             (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in WAGE\n" // Adjusted currency
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric, optional) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string, optional)\n"
            "           \"address\"          (string) DIGIWAGE address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("decoderawtransaction", "\"hexstring\"") + HelpExampleRpc("decoderawtransaction", "\"hexstring\""));

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR));

    CTransaction tx;

    if (!DecodeHexTx(tx, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    UniValue result(UniValue::VOBJ);
    TxToJSON(tx, UINT256_ZERO, result);
    // TxToJSON now adds txid and size. Add others if needed.
    // result.pushKV("hash", tx.GetWitnessHash().GetHex()); // If witness applicable
    // result.pushKV("vsize", GetVirtualTransactionSize(tx)); // If witness applicable
    return result;
}

UniValue decodescript(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "decodescript \"hexstring\"\n"
            "\nDecode a hex-encoded script.\n"

            "\nArguments:\n"
            "1. \"hexstring\"     (string, required) the hex encoded script\n"

            "\nResult:\n"
            "{\n"
            "  \"asm\":\"asm\",   (string) Script public key\n"
            "  \"hex\":\"hex\",   (string, optional) hex encoded public key (if different from input)\n"
            "  \"type\":\"type\", (string) The output type (e.g. 'pubkeyhash')\n"
            "  \"reqSigs\": n,    (numeric, optional) The required signatures (if relevant)\n"
            "  \"addresses\": [   (json array of string, optional)\n"
            "     \"address\"     (string) DIGIWAGE address\n"
            "     ,...\n"
            "  ],\n"
            "  \"p2sh\":\"address\" (string, optional) address corresponding to P2SH output of this script (if relevant)\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("decodescript", "\"hexstring\"") + HelpExampleRpc("decodescript", "\"hexstring\""));

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR));

    UniValue r(UniValue::VOBJ);
    std::string hexScript = params[0].get_str();

    if (hexScript.empty()) {
        CScript script;
        ScriptPubKeyToJSON(script, r, false);
    } else {
        std::vector<unsigned char> scriptData(ParseHexV(params[0], "argument"));
        CScript script(scriptData.begin(), scriptData.end());
        ScriptPubKeyToJSON(script, r, false);

        txnouttype type;
        std::vector<CTxDestination> destinations;
        int nRequired;
        if (ExtractDestinations(script, type, destinations, nRequired) && type != TX_SCRIPTHASH) {
             r.pushKV("p2sh", CBitcoinAddress(CScriptID(script)).ToString());
        }
    }

    // Check existence using find_value before adding hex
    if (find_value(r, "hex").isNull() && !hexScript.empty()) {
        r.pushKV("hex", hexScript);
    }

    return r;
}

/** Pushes a JSON object for script verification or signing errors to vErrorsRet. */
static void TxInErrorToJSON(const CTxIn& txin, UniValue& vErrorsRet, const std::string& strMessage)
{
    UniValue entry(UniValue::VOBJ);
    entry.pushKV("txid", txin.prevout.hash.ToString());
    entry.pushKV("vout", (uint64_t)txin.prevout.n);
    entry.pushKV("scriptSig", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
    entry.pushKV("sequence", (uint64_t)txin.nSequence);
    entry.pushKV("error", strMessage);
    vErrorsRet.push_back(entry);
}

UniValue signrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 4)
        throw std::runtime_error(
            "signrawtransaction \"hexstring\" ( [{\"txid\":\"id\",\"vout\":n,\"scriptPubKey\":\"hex\",\"redeemScript\":\"hex\"},...] [\"privatekey1\",...] sighashtype )\n"
            "\nSign inputs for raw transaction (serialized, hex-encoded).\n"
            "The second optional argument (may be null) is an array of previous transaction outputs that\n"
            "this transaction depends on but may not yet be in the block chain.\n"
            "The third optional argument (may be null) is an array of base58-encoded private\n"
            "keys that, if given, will be the only keys used to sign the transaction.\n"
#ifdef ENABLE_WALLET
            + HelpRequiringPassphrase() + "\n"
#endif

            "\nArguments:\n"
            "1. \"hexstring\"     (string, required) The transaction hex string\n"
            "2. \"prevtxs\"       (array, optional) An json array of previous dependent transaction outputs\n"
            "     [               (json array of json objects, or 'null' if none provided)\n"
            "       {\n"
            "         \"txid\":\"id\",             (string, required) The transaction id\n"
            "         \"vout\":n,                  (numeric, required) The output number\n"
            "         \"scriptPubKey\": \"hex\",   (string, required) script key\n"
            "         \"redeemScript\": \"hex\"    (string, optional for P2SH) redeem script\n"
            "       }\n"
            "       ,...\n"
            "    ]\n"
            "3. \"privatekeys\"     (array, optional) A json array of base58-encoded private keys for signing\n"
            "    [                  (json array of strings, or 'null' if none provided)\n"
            "      \"privatekey\"   (string) private key in base58-encoding\n"
            "      ,...\n"
            "    ]\n"
            "4. \"sighashtype\"     (string, optional, default=ALL) The signature hash type. Must be one of\n"
            "       \"ALL\"\n"
            "       \"NONE\"\n"
            "       \"SINGLE\"\n"
            "       \"ALL|ANYONECANPAY\"\n"
            "       \"NONE|ANYONECANPAY\"\n"
            "       \"SINGLE|ANYONECANPAY\"\n"

            "\nResult:\n"
            "{\n"
            "  \"hex\" : \"value\",           (string) The hex-encoded raw transaction with signature(s)\n"
            "  \"complete\" : true|false,   (boolean) If the transaction has a complete set of signatures\n"
            "  \"errors\" : [                 (json array of objects, optional) Script verification errors (if there are any)\n"
            "    {\n"
            "      \"txid\" : \"hash\",           (string) The hash of the referenced, previous transaction\n"
            "      \"vout\" : n,                (numeric) The index of the output to spent and used as input\n"
            "      \"scriptSig\" : \"hex\",       (string) The hex-encoded signature script\n"
            "      \"sequence\" : n,            (numeric) Script sequence number\n"
            "      \"error\" : \"text\"           (string) Verification or signing error related to the input\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("signrawtransaction", "\"myhex\"") +
            HelpExampleCli("signrawtransaction", "\"myhex\" \"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0,\\\"scriptPubKey\\\":\\\"0014751e76e8199196d454941c45d1b3a323f1433bd6\\\"}]\" \"[\\\"privkey1\\\"]\"") +
            HelpExampleRpc("signrawtransaction", "\"myhex\""));

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VARR)(UniValue::VARR)(UniValue::VSTR), true);

    std::vector<unsigned char> txData(ParseHexV(params[0], "argument 1"));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    std::vector<CMutableTransaction> txVariants;
    while (!ssData.empty()) {
        try {
            CMutableTransaction tx;
            ssData >> tx;
            txVariants.push_back(tx);
        } catch (const std::exception&) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
        }
    }

    if (txVariants.empty())
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing transaction");

    CMutableTransaction mergedTx(txVariants[0]);

    std::map<COutPoint, CScript> mapPrevOutScriptPubKeys;
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK(mempool.cs);
        CCoinsViewCache& viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool);

        for (const CMutableTransaction& txv : txVariants) {
            for (const CTxIn& txin : txv.vin) {
                const COutPoint& prevout = txin.prevout;
                if (mapPrevOutScriptPubKeys.find(prevout) == mapPrevOutScriptPubKeys.end()) {
                    const CCoins* coins = view.AccessCoins(prevout.hash);
                    if (coins && prevout.n < coins->vout.size() && !coins->vout[prevout.n].IsNull()) {
                         mapPrevOutScriptPubKeys[prevout] = coins->vout[prevout.n].scriptPubKey;
                    }
                }
            }
        }
         view.SetBackend(viewDummy);
    }


    bool fGivenKeys = false;
    CBasicKeyStore tempKeystore;
    if (params.size() > 1 && !params[1].isNull()) {
        UniValue prevTxs = params[1].get_array();
        for (unsigned int idx = 0; idx < prevTxs.size(); idx++) {
            const UniValue& p = prevTxs[idx];
            if (!p.isObject())
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"txid\",\"vout\",\"scriptPubKey\"}");

            UniValue prevOut = p.get_obj();

            const UniValue& txidValue = find_value(prevOut, "txid");
            const UniValue& voutValue = find_value(prevOut, "vout");
            const UniValue& scriptPubKeyValue = find_value(prevOut, "scriptPubKey");

            if (txidValue.isNull() || voutValue.isNull() || scriptPubKeyValue.isNull())
                 throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing required fields in prevtx object");

            uint256 txid = ParseHashV(txidValue, "txid");
            int nOut = voutValue.getInt<int>();
            if (nOut < 0)
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");

            COutPoint outpoint(txid, nOut);
            std::vector<unsigned char> pkData(ParseHexV(scriptPubKeyValue, "scriptPubKey"));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            mapPrevOutScriptPubKeys[outpoint] = scriptPubKey;


            const UniValue& redeemScriptValue = find_value(prevOut, "redeemScript");
            if (!redeemScriptValue.isNull() && scriptPubKey.IsPayToScriptHash()) {
                std::vector<unsigned char> rsData(ParseHexV(redeemScriptValue, "redeemScript"));
                CScript redeemScript(rsData.begin(), rsData.end());
                tempKeystore.AddCScript(redeemScript);
            }
        }
    }

    if (params.size() > 2 && !params[2].isNull()) {
        fGivenKeys = true;
        UniValue keys = params[2].get_array();
        for (unsigned int idx = 0; idx < keys.size(); idx++) {
            UniValue k = keys[idx];
            CBitcoinSecret vchSecret;
            bool fGood = vchSecret.SetString(k.get_str());
            if (!fGood)
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
            CKey key = vchSecret.GetKey();
            if (!key.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Private key outside allowed range");
            tempKeystore.AddKey(key);
        }
    }
#ifdef ENABLE_WALLET
    else if (pwalletMain) {
        EnsureWalletIsUnlocked();
    }
#endif


#ifdef ENABLE_WALLET
    const CKeyStore& keystore = ((fGivenKeys || !pwalletMain) ? tempKeystore : *pwalletMain);
#else
    const CKeyStore& keystore = tempKeystore;
#endif

    int nHashType = SIGHASH_ALL;
    if (params.size() > 3 && !params[3].isNull()) {
        static std::map<std::string, int> mapSigHashValues =
            boost::assign::map_list_of
            (std::string("ALL"), int(SIGHASH_ALL))
            (std::string("ALL|ANYONECANPAY"), int(SIGHASH_ALL | SIGHASH_ANYONECANPAY))
            (std::string("NONE"), int(SIGHASH_NONE))
            (std::string("NONE|ANYONECANPAY"), int(SIGHASH_NONE | SIGHASH_ANYONECANPAY))
            (std::string("SINGLE"), int(SIGHASH_SINGLE))
            (std::string("SINGLE|ANYONECANPAY"), int(SIGHASH_SINGLE | SIGHASH_ANYONECANPAY))
            .convert_to_container<std::map<std::string, int> >();
        std::string strHashType = params[3].get_str();
        if (mapSigHashValues.count(strHashType))
            nHashType = mapSigHashValues[strHashType];
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid sighash param");
    }

    bool fHashSingle = ((nHashType & 0x1f) == SIGHASH_SINGLE);

    UniValue vErrors(UniValue::VARR);

    for (unsigned int i = 0; i < mergedTx.vin.size(); i++) {
        CTxIn& txin = mergedTx.vin[i];
        const COutPoint& prevout = txin.prevout;

        auto it = mapPrevOutScriptPubKeys.find(prevout);
        if (it == mapPrevOutScriptPubKeys.end()) {
             TxInErrorToJSON(txin, vErrors, "Input scriptPubKey not found");
             continue;
        }
        const CScript& prevPubKey = it->second;

        txin.scriptSig.clear();

        if (!fHashSingle || (i < mergedTx.vout.size())) {
             SignSignature(keystore, prevPubKey, mergedTx, i, nHashType);
        }

        for (const CMutableTransaction& txv : txVariants) {
            if (i < txv.vin.size())
                txin.scriptSig = CombineSignatures(prevPubKey, CTransaction(mergedTx), i, txin.scriptSig, txv.vin[i].scriptSig);
        }

        ScriptError serror = SCRIPT_ERR_OK;
        if (!VerifyScript(txin.scriptSig, prevPubKey, STANDARD_SCRIPT_VERIFY_FLAGS, MutableTransactionSignatureChecker(&mergedTx, i), &serror)) {
             if (serror == SCRIPT_ERR_SIG_COUNT || serror == SCRIPT_ERR_PUBKEY_COUNT) {
                 TxInErrorToJSON(txin, vErrors, std::string(ScriptErrorString(serror)) + " (possibly incomplete)");
             } else {
                 TxInErrorToJSON(txin, vErrors, ScriptErrorString(serror));
             }
        }
    }

    bool fComplete = vErrors.empty();

    UniValue result(UniValue::VOBJ);
    result.pushKV("hex", EncodeHexTx(mergedTx));
    result.pushKV("complete", fComplete);
    if (!vErrors.empty()) {
        result.pushKV("errors", vErrors);
    }

    return result;
}

UniValue sendrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
        throw std::runtime_error(
            "sendrawtransaction \"hexstring\" ( allowhighfees swiftx )\n"
            "\nSubmits raw transaction (serialized, hex-encoded) to local node and network.\n"
            "\nAlso see createrawtransaction and signrawtransaction calls.\n"

            "\nArguments:\n"
            "1. \"hexstring\"      (string, required) The hex string of the raw transaction\n"
            "2. allowhighfees    (boolean, optional, default=false) Allow high fees\n"
            "3. swiftx           (boolean, optional, default=false) Use SwiftX (InstantSend) to send this transaction\n"

            "\nResult:\n"
            "\"txid\"              (string) The transaction hash in hex\n"

            "\nExamples:\n"
            "\nCreate a transaction\n" +
            HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":0.01}\"") +
            "\nSign the transaction, and get back the hex\n" + HelpExampleCli("signrawtransaction", "\"signedhex\"") +
            "\nSend the transaction (signed hex)\n" + HelpExampleCli("sendrawtransaction", "\"signedhex\"") +
            "\nSend with high fees allowed\n" + HelpExampleCli("sendrawtransaction", "\"signedhex\" true") +
            "\nSend using SwiftX\n" + HelpExampleCli("sendrawtransaction", "\"signedhex\" false true") +
            "\nAs a JSON-RPC call\n" + HelpExampleRpc("sendrawtransaction", "\"signedhex\", false, true"));

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VBOOL)(UniValue::VBOOL), true);

    CTransaction tx;
    if (!DecodeHexTx(tx, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    uint256 hashTx = tx.GetHash();

    bool fAllowHighFees = false;
    if (params.size() > 1 && !params[1].isNull())
        fAllowHighFees = params[1].get_bool();

    bool fSwiftX = false;
    if (params.size() > 2 && !params[2].isNull())
        fSwiftX = params[2].get_bool();

    AssertLockNotHeld(cs_main);
    {
        LOCK(cs_main);
        CCoinsViewCache& view = *pcoinsTip;
        bool fHaveChain = false;
        const CCoins* coins = view.AccessCoins(hashTx);
        if(coins && !coins->IsPruned()) {
             fHaveChain = true;
        }
        bool fHaveMempool = mempool.exists(hashTx);

        if (fHaveChain) {
             throw JSONRPCError(RPC_TRANSACTION_ALREADY_IN_CHAIN, "transaction already in block chain");
        }
        if (fHaveMempool) {
             return hashTx.GetHex();
        }
    }


    CValidationState state;
    bool fMissingInputs = false;

    if (fSwiftX) {
         if (mapTxLockReq.count(hashTx)) {
            // LogPrintf("instantsend", "sendrawtransaction: Transaction %s already has a lock request\n", hashTx.ToString());
         } else {
            mapTxLockReq.insert(std::make_pair(hashTx, tx));
            CreateNewLock(tx); // Deprecated copy warning here
            RelayTransactionLockReq(tx, true);
         }
    }


    if (!AcceptToMemoryPool(mempool, state, tx, false, &fMissingInputs, !fAllowHighFees)) {
        if (state.IsInvalid()) {
            throw JSONRPCError(RPC_TRANSACTION_REJECTED, strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
        } else {
            if (fMissingInputs) {
                throw JSONRPCError(RPC_TRANSACTION_ERROR, "Missing inputs");
            }
            throw JSONRPCError(RPC_TRANSACTION_ERROR, state.GetRejectReason());
        }
    }


    RelayTransaction(tx);

    return hashTx.GetHex();
}


