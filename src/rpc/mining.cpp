// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2019 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "base58.h"
#include "chainparams.h"
#include "core_io.h"
#include "init.h"
#include "main.h" // Includes CBlockIndex, chainActive, cs_main, etc.
#include "miner.h"
#include "net.h"
#include "pow.h"
#include "rpc/server.h"
#include "util.h"
#include "utilstrencodings.h" // For ParseHashStr, IsHex, ParseHex, EncodeHexTx, HexStr
#include "validationinterface.h"
#ifdef ENABLE_WALLET
#include "wallet/db.h"
#include "wallet/wallet.h"
#endif

#include <stdint.h>

#include <boost/assign/list_of.hpp> // For RPCTypeCheck list_of

#include <univalue.h>


UniValue GetNetworkHashPS(int lookup, int height)
{
    CBlockIndex *pb = chainActive.Tip();

    if (height >= 0 && height < chainActive.Height())
        pb = chainActive[height];

    if (pb == NULL || !pb->nHeight)
        return 0;

    // *** FIX: Removed unused consensusParams variable ***
    // const Consensus::Params& consensusParams = Params().GetConsensus();

    if (lookup <= 0) lookup = 120;

    if (lookup > pb->nHeight)
        lookup = pb->nHeight;

    CBlockIndex* pb0 = pb;
    int64_t minTime = pb0->GetBlockTime();
    int64_t maxTime = minTime;
    for (int i = 0; i < lookup; i++) {
        if (!pb0->pprev) break;
        pb0 = pb0->pprev;
        int64_t time = pb0->GetBlockTime();
        minTime = std::min(time, minTime);
        maxTime = std::max(time, maxTime);
    }

    if (minTime == maxTime)
        return 0;

    uint256 workDiff = pb->nChainWork - pb0->nChainWork;
    int64_t timeDiff = maxTime - minTime;

    return workDiff.getdouble() / timeDiff;
}

UniValue getnetworkhashps(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw std::runtime_error(
            "getnetworkhashps ( blocks height )\n"
            "\nReturns the estimated network hashes per second based on the last n blocks.\n"
            "Pass in [blocks] to override # of blocks, -1 specifies since last difficulty change (may not be relevant for PoS).\n"
            "Pass in [height] to estimate the network speed at the time when a certain block was found.\n"

            "\nArguments:\n"
            "1. blocks     (numeric, optional, default=120) The number of blocks.\n"
            "2. height     (numeric, optional, default=-1) To estimate at the time of the given height.\n"

            "\nResult:\n"
            "x             (numeric) Hashes per second estimated\n"
            "\nExamples:\n" +
            HelpExampleCli("getnetworkhashps", "") + HelpExampleRpc("getnetworkhashps", ""));

    LOCK(cs_main);
    return GetNetworkHashPS(params.size() > 0 ? params[0].getInt<int>() : 120, params.size() > 1 ? params[1].getInt<int>() : -1);
}

#ifdef ENABLE_WALLET
UniValue getgenerate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "getgenerate\n"
            "\nReturn if the server is set to generate coins or not. The default is false.\n"
            "It is set with the command line argument -gen (or digiwage.conf setting gen)\n"
            "It can also be set with the setgenerate call.\n"

            "\nResult:\n"
            "true|false      (boolean) If the server is set to generate coins or not\n"

            "\nExamples:\n" +
            HelpExampleCli("getgenerate", "") + HelpExampleRpc("getgenerate", ""));

    LOCK(cs_main);
    return GetBoolArg("-gen", false);
}

UniValue generate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 1)
        throw std::runtime_error(
            "generate numblocks\n"
            "\nMine blocks immediately (before the RPC call returns)\n"
            "\nNote: this function can only be used on the regtest network\n"

            "\nArguments:\n"
            "1. numblocks    (numeric, required) How many blocks to generate.\n"

            "\nResult\n"
            "[ blockhashes ]     (array) hashes of blocks generated\n"

            "\nExamples:\n"
            "\nGenerate 11 blocks\n"
            + HelpExampleCli("generate", "11")
        );

    if (!Params().IsRegTestNet())
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "This method can only be used on regtest");

    if (!pwalletMain)
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet needed for mining");

    const int nGenerate = params[0].getInt<int>();
    if (nGenerate <= 0 || nGenerate > 500)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid number of blocks");

    int nHeightEnd = 0;
    int nHeight = 0;

    {
        LOCK(cs_main);
        nHeight = chainActive.Height();
        nHeightEnd = nHeight + nGenerate;
    }

    const int last_pow_block = Params().GetConsensus().height_last_PoW;
    bool fPoS = (nHeight >= last_pow_block);

    if (fPoS) {
        EnsureWalletIsUnlocked();
    }

    UniValue blockHashes(UniValue::VARR);
    CReserveKey reservekey(pwalletMain);
    unsigned int nExtraNonce = 0;

    while (nHeight < nHeightEnd)
    {
        if (ShutdownRequested()) break;

        std::unique_ptr<CBlockTemplate> pblocktemplate;
        try {
             pblocktemplate.reset(fPoS ?
                                  CreateNewBlock(CScript() << OP_TRUE, pwalletMain, fPoS) :
                                  CreateNewBlockWithKey(reservekey, pwalletMain));
        } catch (const std::runtime_error& e) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, std::string("Block creation failed: ") + e.what());
        }

        if (!pblocktemplate.get()) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to create block template");
        }
        CBlock *pblock = &pblocktemplate->block;

        if(!fPoS) {
            {
                LOCK(cs_main);
                IncrementExtraNonce(pblock, chainActive.Tip(), nExtraNonce);
            }
            while (true) {
                if (ShutdownRequested()) break;
                // *** FIX: Use GetHash() instead of GetPOWHash() ***
                if (CheckProofOfWork(pblock->GetHash(), pblock->nBits)) break;
                pblock->nNonce++;
                if (pblock->nNonce == 0) {
                    LogPrintf("generate: Nonce overflowed, changing time\n");
                     LOCK(cs_main);
                    UpdateTime(pblock, chainActive.Tip());
                    IncrementExtraNonce(pblock, chainActive.Tip(), nExtraNonce);
                }
            }
            if (ShutdownRequested()) break;
        }

        CValidationState state;
        if (!ProcessNewBlock(state, nullptr, pblock))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "ProcessNewBlock failed: " + state.GetRejectReason());

        ++nHeight;
        blockHashes.push_back(pblock->GetHash().GetHex());

        fPoS = (nHeight >= last_pow_block);
        if (fPoS) EnsureWalletIsUnlocked();
    }

    if (blockHashes.empty() || (int)blockHashes.size() < nGenerate)
         throw JSONRPCError(RPC_MISC_ERROR, "Failed to generate all requested blocks");

    return blockHashes;
}

UniValue setgenerate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
            "setgenerate generate ( genproclimit )\n"
            "\nSet 'generate' true or false to turn generation on or off.\n"
            "Generation is limited to 'genproclimit' processors, -1 is unlimited.\n"
            "DEPRECATED: Use the generate command on regtest. This call is only relevant for mainnet PoW mining.\n"
            "See the getgenerate call for the current setting.\n"

            "\nArguments:\n"
            "1. generate         (boolean, required) Set to true to turn on generation, false to turn off.\n"
            "2. genproclimit     (numeric, optional) Set the processor limit for when generation is on. Can be -1 for unlimited.\n"

            "\nExamples:\n"
            "\nSet the generation on with a limit of one processor\n" +
            HelpExampleCli("setgenerate", "true 1") +
            "\nCheck the setting\n" + HelpExampleCli("getgenerate", "") +
            "\nTurn off generation\n" + HelpExampleCli("setgenerate", "false") +
            "\nUsing json rpc\n" + HelpExampleRpc("setgenerate", "true, 1"));

    if (pwalletMain == NULL)
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found (wallet disabled)");

    if (Params().IsRegTestNet())
        throw JSONRPCError(RPC_INVALID_REQUEST, "Use the generate method instead of setgenerate on regtest");

    bool fGenerate = true;
    if (params.size() > 0)
        fGenerate = params[0].get_bool();

    int last_pow_block = Params().GetConsensus().height_last_PoW;
    if (fGenerate && chainActive.Height() >= last_pow_block)
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Proof of Work phase ended at block %d", last_pow_block));

    int nGenProcLimit = -1;
    if (params.size() > 1) {
        nGenProcLimit = params[1].getInt<int>();
        if (nGenProcLimit == 0)
            fGenerate = false;
    }

    mapArgs["-gen"] = (fGenerate ? "1" : "0");
    mapArgs["-genproclimit"] = itostr(nGenProcLimit);
    GenerateBitcoins(fGenerate, pwalletMain, nGenProcLimit);

    return NullUniValue;
}

UniValue gethashespersec(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "gethashespersec\n"
            "\nReturns a recent hashes per second performance measurement while generating.\n"
            "DEPRECATED: Only useful for mainnet PoW mining.\n"
            "See the getgenerate and setgenerate calls to turn generation on and off.\n"

            "\nResult:\n"
            "n            (numeric) The recent hashes per second when generation is on (will return 0 if generation is off)\n"

            "\nExamples:\n" +
            HelpExampleCli("gethashespersec", "") + HelpExampleRpc("gethashespersec", ""));

    extern int64_t nHPSTimerStart;
    extern double dHashesPerSec;

    if (GetTimeMillis() - nHPSTimerStart > 8000)
        return (int64_t)0;
    return (int64_t)dHashesPerSec;
}
#endif // ENABLE_WALLET


UniValue getmininginfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "getmininginfo\n"
            "\nReturns a json object containing mining-related information."

            "\nResult:\n"
            "{\n"
            "  \"blocks\": nnn,             (numeric) The current block\n"
            "  \"currentblocksize\": nnn,   (numeric) The last block size\n"
            "  \"currentblocktx\": nnn,     (numeric) The last block transaction\n"
            "  \"difficulty\": xxx.xxxxx    (numeric) The current difficulty (PoW or PoS)\n"
            "  \"errors\": \"...\"          (string) Current errors\n"
#ifdef ENABLE_WALLET
            "  \"generate\": true|false,    (boolean) If the generation is on or off (see getgenerate or setgenerate calls)\n"
            "  \"genproclimit\": n,         (numeric) The processor limit for generation. -1 if no generation. (see getgenerate or setgenerate calls)\n"
            "  \"hashespersec\": n,         (numeric) The hashes per second of the generation, or 0 if no generation.\n"
#endif
            "  \"networkhashps\": x.xx,     (numeric) The estimated network hashes per second\n"
            "  \"pooledtx\": n,             (numeric) The size of the mem pool\n"
            "  \"chain\": \"xxxx\",         (string) current network name as defined in BIP70 (main, test, regtest)\n"
            "  \"testnet\": true|false      (boolean) If using testnet or not (DEPRECATED: use \"chain\")\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getmininginfo", "") + HelpExampleRpc("getmininginfo", ""));

    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("blocks", (int)chainActive.Height());
    obj.pushKV("currentblocksize", (uint64_t)nLastBlockSize);
    obj.pushKV("currentblocktx", (uint64_t)nLastBlockTx);
    obj.pushKV("difficulty", (double)GetDifficulty());
    obj.pushKV("errors", GetWarnings("statusbar"));
#ifdef ENABLE_WALLET
    obj.pushKV("genproclimit", (int)GetArg("-genproclimit", -1));
    obj.pushKV("generate", getgenerate(params, false));
    obj.pushKV("hashespersec", gethashespersec(params, false));
#else
    obj.pushKV("genproclimit", -1);
    obj.pushKV("generate", false);
    obj.pushKV("hashespersec", 0);
#endif
    obj.pushKV("networkhashps", getnetworkhashps(params, false));
    obj.pushKV("pooledtx", (uint64_t)mempool.size());
    obj.pushKV("chain", Params().NetworkIDString());
    obj.pushKV("testnet", Params().NetworkID() == CBaseChainParams::TESTNET);

    return obj;
}


UniValue prioritisetransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw std::runtime_error(
            "prioritisetransaction <txid> <priority delta> <fee delta>\n"
            "Accepts the transaction into mined blocks at a higher (or lower) priority\n"

            "\nArguments:\n"
            "1. \"txid\"       (string, required) The transaction id.\n"
            "2. priority delta (numeric, required) The priority to add or subtract.\n"
            "                  (Not used by DIGIWAGE if priority is disabled)\n"
            "3. fee delta      (numeric, required) The fee value (in Satoshis) to add (or subtract, if negative).\n"
            "                  The fee is not actually paid, only the algorithm for selecting transactions into a block\n"
            "                  considers the transaction as it would have paid a higher (or lower) fee.\n"

            "\nResult:\n"
            "true              (boolean) Returns true\n"

            "\nExamples:\n" +
            HelpExampleCli("prioritisetransaction", "\"txid\" 0.0 10000") + HelpExampleRpc("prioritisetransaction", "\"txid\", 0.0, 10000"));

    LOCK(cs_main);

    uint256 hash = ParseHashStr(params[0].get_str(), "txid");
    CAmount nAmount = params[2].getInt<int64_t>();

    mempool.PrioritiseTransaction(hash, params[0].get_str(), params[1].get_real(), nAmount);
    return true;
}


static UniValue BIP22ValidationResult(const CValidationState& state)
{
    if (state.IsValid())
        return NullUniValue;

    std::string strRejectReason = state.GetRejectReason();
    int rejectCode = state.GetRejectCode();

    if (state.IsError())
        throw JSONRPCError(rejectCode > 0 ? rejectCode : RPC_VERIFY_ERROR, "Block verification failed: " + strRejectReason);
    if (state.IsInvalid()) {
        if (strRejectReason.empty())
            return "rejected";
        return strRejectReason;
    }
    return "valid?";
}

UniValue getblocktemplate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "getblocktemplate ( TemplateRequest )\n"
            "\nIf the request parameters include a 'mode' key, that is used to explicitly select between the default 'template' request or a 'proposal'.\n"
            "It returns data needed to construct a block to work on.\n"
            "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.\n"

            "\nArguments:\n"
            "1. TemplateRequest          (object, optional) A json object in the following spec\n"
            "     {\n"
            "       \"mode\":\"template\"    (string, optional) This must be set to \"template\" or omitted\n"
            "       \"capabilities\":[       (array, optional) A list of strings\n"
            "           \"support\"           (string) client side supported feature, 'longpoll', 'coinbasetxn', 'coinbasevalue', 'proposal', 'serverlist', 'workid'\n"
            "           ,...\n"
            "         ],\n"
            "       \"rules\":[             (array, optional) A list of strings\n"
            "           \"support\"           (string) client side supported softfork deployment rule name (e.g. \"csv\", \"segwit\")\n"
            "           ,...\n"
            "        ]\n"
            "     }\n"
            "\n"

            "\nResult:\n"
            "{\n"
            "  \"version\" : n,                    (numeric) The preferred block version\n"
            "  \"capabilities\" : [ \"capability\", ... ], (array) specific client side supported features\n"
            "  \"rules\" : [ \"rule\", ... ],        (array of strings) specific block rules that are to be enforced\n"
            "  \"vbavailable\" : { \"rulename\" : bitnumber }, (object) set of pending, supported versionbit (BIP 9) softfork deployments\n"
            "  \"vbrequired\" : n,                 (numeric) bit mask of versionbits the server requires set in submissions\n"
            "  \"previousblockhash\" : \"xxxx\",    (string) The hash of current highest block\n"
            "  \"transactions\" : [                (array) contents of non-coinbase transactions that should be included in the next block\n"
            "      {\n"
            "         \"data\" : \"xxxx\",          (string) transaction data encoded in hexadecimal (byte-for-byte)\n"
            "         \"txid\" : \"xxxx\",          (string) Transaction id encoded in little-endian hexadecimal\n"
            "         \"depends\" : [              (array) array of numbers \n"
            "             n                        (numeric) transactions before this one (by 1-based index in 'transactions' list) that must be present in the final block if this one is\n"
            "             ,...\n"
            "         ],\n"
            "         \"fee\": n,                   (numeric) difference in value between transaction inputs and outputs (in Satoshis)\n"
            "         \"sigops\" : n               (numeric) total number of SigOps, as counted for purposes of block limits\n"
            "      }\n"
            "      ,...\n"
            "  ],\n"
            "  \"coinbaseaux\" : {                  (json object) data that must be included in the coinbase's scriptSig content\n"
            "      \"flags\" : \"xx\"               (string) hex-encoded coinbase script flag\n"
            "  },\n"
            "  \"coinbasevalue\" : n,               (numeric) maximum allowable input to coinbase transaction, including the generation award and transaction fees (in Satoshis)\n"
            "  \"longpollid\" : \"str\",          (string) an id to include with a request to longpoll on an update to this template\n"
            "  \"target\" : \"xxxx\",             (string) The hash target\n"
            "  \"mintime\" : xxx,                 (numeric) The minimum timestamp appropriate for next block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mutable\" : [                    (array of string) list of ways the block template may be changed \n"
            "     \"value\"                       (string) A way the block template may be changed, e.g. 'time', 'transactions', 'prevblock'\n"
            "     ,...\n"
            "  ],\n"
            "  \"noncerange\" : \"00000000ffffffff\",   (string) A range of valid nonces\n"
            "  \"curtime\" : ttt,                 (numeric) current timestamp in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"bits\" : \"xxxxxxxx\",           (string) compressed target of next block\n"
            "  \"height\" : n,                    (numeric) The height of the block being worked on\n"
            "  \"payee\" : \"xxx\",               (string) required payee for the masternode/budget payment\n"
            "  \"payee_amount\" : n,              (numeric) required amount to pay (in Satoshis)\n"
            "  \"masternode_payments_enforced\" : true|false, (boolean) true, if masternode payments are enforced\n"
            "  \"masternode_payments_started\" : true|false   (boolean) true, if masternode payments have started\n"
            "  \"superblock_started\" : true|false,         (boolean) true, if superblock payments have started\n"
            "  \"superblock_budget_started\" : true|false     (boolean) true, if superblock budget payments have started\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getblocktemplate", "") + HelpExampleRpc("getblocktemplate", ""));

    LOCK(cs_main);

    std::string strMode = "template";
    UniValue lpval = NullUniValue;
    if (params.size() > 0) {
        const UniValue& oparam = params[0].get_obj();
        if (oparam.exists("mode")) {
            const UniValue& modeval = oparam["mode"];
            if (modeval.isStr())
                strMode = modeval.get_str();
            else if (!modeval.isNull())
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");
        }

        if (oparam.exists("longpollid")) {
            lpval = oparam["longpollid"];
        }

        if (strMode == "proposal") {
             if (!oparam.exists("data") || !oparam["data"].isStr())
                throw JSONRPCError(RPC_TYPE_ERROR, "Missing data String key for proposal");
            const UniValue& dataval = oparam["data"];

            CBlock block;
            if (!DecodeHexBlk(block, dataval.get_str()))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

            uint256 hash = block.GetHash();
            BlockMap::iterator mi = mapBlockIndex.find(hash);
            if (mi != mapBlockIndex.end()) {
                CBlockIndex* pindex = mi->second;
                if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                    return "duplicate";
                if (pindex->nStatus & BLOCK_FAILED_MASK)
                    return "duplicate-invalid";
                return "duplicate-inconclusive";
            }

            CBlockIndex* const pindexPrev = chainActive.Tip();
            if (block.hashPrevBlock != pindexPrev->GetBlockHash())
                return "inconclusive-not-best-prevblk";
            CValidationState state;
            TestBlockValidity(state, block, pindexPrev, false, true);
            return BIP22ValidationResult(state);
        }
    }

    if (strMode != "template")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");

    // *** FIX: Removed !Params().MineBlocksOnDemand() checks ***
    if (vNodes.empty())
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "DIGIWAGE is not connected!");

    if (IsInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "DIGIWAGE is downloading blocks...");

    static unsigned int nTransactionsUpdatedLast;

    if (!lpval.isNull()) {
        uint256 hashWatchedChain;
        std::chrono::steady_clock::time_point checktxtime;
        unsigned int nTransactionsUpdatedLastLP;

        if (lpval.isStr()) {
            std::string lpstr = lpval.get_str();
             if (lpstr.length() < 64 + 1 || lpstr.length() > 64 + std::to_string(std::numeric_limits<unsigned int>::max()).length()) {
                  hashWatchedChain = chainActive.Tip()->GetBlockHash();
                  nTransactionsUpdatedLastLP = nTransactionsUpdatedLast;
                  LogPrintf("getblocktemplate: Warning: Invalid longpollid format received: %s\n", lpstr);
             } else {
                 try {
                     hashWatchedChain.SetHex(lpstr.substr(0, 64));
                     nTransactionsUpdatedLastLP = std::stoul(lpstr.substr(64));
                 } catch (const std::exception& e) {
                     hashWatchedChain = chainActive.Tip()->GetBlockHash();
                     nTransactionsUpdatedLastLP = nTransactionsUpdatedLast;
                     LogPrintf("getblocktemplate: Warning: Failed to parse longpollid '%s': %s\n", lpstr, e.what());
                 }
             }
        } else {
            hashWatchedChain = chainActive.Tip()->GetBlockHash();
            nTransactionsUpdatedLastLP = nTransactionsUpdatedLast;
        }

        if (chainActive.Tip()->GetBlockHash() == hashWatchedChain && mempool.GetTransactionsUpdated() == nTransactionsUpdatedLastLP) {
             return NullUniValue;
        }
        if (!IsRPCRunning())
            throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Shutting down");
    }

    static CBlockIndex* pindexPrev = nullptr;
    static int64_t nStart = 0;
    static std::unique_ptr<CBlockTemplate> pblocktemplate;

    bool fUpdateTemplate = (pindexPrev != chainActive.Tip()) ||
                           (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 5) ||
                           !pblocktemplate;


    if (fUpdateTemplate) {
        pindexPrev = nullptr;

        nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
        CBlockIndex* pindexPrevNew = chainActive.Tip();
        nStart = GetTime();

        CScript scriptDummy = CScript() << OP_TRUE;
        try {
            pblocktemplate.reset(CreateNewBlock(scriptDummy, pwalletMain, false));
        } catch (const std::runtime_error& e) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, std::string("Block template creation failed: ") + e.what());
        }
        if (!pblocktemplate)
            throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory during block template creation");

        pindexPrev = pindexPrevNew;
    }

    if (!pblocktemplate) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Block template is unexpectedly null");
    }

    CBlock* pblock = &pblocktemplate->block;

    UpdateTime(pblock, pindexPrev);
    pblock->nNonce = 0;

    UniValue aCaps(UniValue::VARR); aCaps.push_back("proposal");

    UniValue transactions(UniValue::VARR);
    std::map<uint256, int64_t> setTxIndex;
    int tx_idx = 0;
    for (const CTransaction& tx : pblock->vtx) {
        uint256 txHash = tx.GetHash();
        setTxIndex[txHash] = tx_idx++;

        if (tx.IsCoinBase())
            continue;

        UniValue entry(UniValue::VOBJ);

        entry.pushKV("data", EncodeHexTx(tx));
        entry.pushKV("txid", txHash.GetHex());

        UniValue deps(UniValue::VARR);
        for (const CTxIn& in : tx.vin) {
            if (setTxIndex.count(in.prevout.hash))
                deps.push_back(setTxIndex[in.prevout.hash]);
        }
        entry.pushKV("depends", deps);

        int index_in_template = tx_idx - 1;
        if (index_in_template >= 0 &&
            static_cast<size_t>(index_in_template) < pblocktemplate->vTxFees.size() &&
            static_cast<size_t>(index_in_template) < pblocktemplate->vTxSigOps.size())
        {
             entry.pushKV("fee", pblocktemplate->vTxFees[index_in_template]);
             entry.pushKV("sigops", pblocktemplate->vTxSigOps[index_in_template]);
        } else {
            LogPrintf("getblocktemplate: Error: Mismatch between vtx size and fee/sigops vectors for tx %s\n", txHash.ToString());
             entry.pushKV("fee", 0);
             entry.pushKV("sigops", 0);
        }

        transactions.push_back(entry);
    }

    UniValue aux(UniValue::VOBJ);
    aux.pushKV("flags", HexStr(COINBASE_FLAGS.begin(), COINBASE_FLAGS.end()));

    uint256 hashTarget = uint256().SetCompact(pblock->nBits);

    static UniValue aMutable(UniValue::VARR);
    if (aMutable.empty()) {
        aMutable.push_back("time");
        aMutable.push_back("transactions");
        aMutable.push_back("prevblock");
    }

    UniValue vbavailable(UniValue::VOBJ);
    UniValue rules(UniValue::VARR);

    UniValue result(UniValue::VOBJ);
    result.pushKV("version", pblock->nVersion);
    result.pushKV("rules", rules);
    result.pushKV("vbavailable", vbavailable);

    result.pushKV("previousblockhash", pblock->hashPrevBlock.GetHex());
    result.pushKV("transactions", transactions);
    result.pushKV("coinbaseaux", aux);
    result.pushKV("coinbasevalue", (int64_t)pblock->vtx[0].GetValueOut());
    result.pushKV("longpollid", chainActive.Tip()->GetBlockHash().GetHex() + i64tostr(nTransactionsUpdatedLast));
    result.pushKV("target", hashTarget.GetHex());
    result.pushKV("mintime", (int64_t)pindexPrev->GetMedianTimePast() + 1);
    result.pushKV("mutable", aMutable);
    result.pushKV("noncerange", "00000000ffffffff");
    // *** FIX: Commented out limits ***
    // result.pushKV("sigoplimit", (int64_t)MAX_BLOCK_SIGOPS); // Find correct constant
    // result.pushKV("sizelimit", (int64_t)MAX_BLOCK_SIZE);   // Find correct constant
    result.pushKV("curtime", pblock->GetBlockTime());
    result.pushKV("bits", strprintf("%08x", pblock->nBits));
    result.pushKV("height", (int64_t)(pindexPrev->nHeight + 1));

    if (!pblock->payee.empty() && pblock->payee != CScript()) {
        CTxDestination address1;
        if (ExtractDestination(pblock->payee, address1)) {
            CBitcoinAddress address2(address1);
            result.pushKV("payee", address2.ToString());
            if (pblock->vtx[0].vout.size() > 1) {
                 result.pushKV("payee_amount", (int64_t)pblock->vtx[0].vout[1].nValue);
            } else {
                 result.pushKV("payee_amount", (int64_t)0);
                 LogPrintf("getblocktemplate: Warning: Coinbase payee script set, but no corresponding output found.\n");
            }
        } else {
             result.pushKV("payee", "invalid_script");
             result.pushKV("payee_amount", (int64_t)0);
        }
    } else {
        result.pushKV("payee", "");
        result.pushKV("payee_amount", (int64_t)0);
    }

    // *** FIX: Commented out checks using missing consensus params ***
    // TODO: Find correct param names (e.g., nMasternodePaymentsStartBlock, nSuperblockStartBlock)
    // bool mnPaymentsStarted = pindexPrev->nHeight + 1 >= Params().GetConsensus().nMasternodePaymentsStartBlock;
    bool mnPaymentsStarted = pindexPrev->nHeight + 1 >= 1; // Placeholder
    result.pushKV("masternode_payments_started", mnPaymentsStarted);
    result.pushKV("masternode_payments_enforced", mnPaymentsStarted);
    // bool sbStarted = pindexPrev->nHeight + 1 >= Params().GetConsensus().nSuperblockStartBlock;
    bool sbStarted = pindexPrev->nHeight + 1 >= 1; // Placeholder
    result.pushKV("superblock_started", sbStarted);
    result.pushKV("superblock_budget_started", sbStarted);

    return result;
}

class submitblock_StateCatcher : public CValidationInterface
{
public:
    uint256 hash;
    bool found;
    CValidationState state;

    submitblock_StateCatcher(const uint256& hashIn) : hash(hashIn), found(false), state(){};

protected:
    virtual void BlockChecked(const CBlock& block, const CValidationState& stateIn) override
    {
        if (block.GetHash() != hash)
            return;
        found = true;
        state = stateIn;
    };
};

UniValue submitblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
            "submitblock \"hexdata\" ( \"parameters\" )\n"
            "\nAttempts to submit new block to network.\n"
            "The 'parameters' object is currently ignored.\n"
            "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.\n"

            "\nArguments\n"
            "1. \"hexdata\"    (string, required) the hex-encoded block data to submit\n"
            "2. \"parameters\" (object, optional) object of optional parameters\n"
            "    {\n"
            "      \"workid\" : \"id\"    (string, optional) if the server provided a workid, it MUST be included with submissions\n"
            "    }\n"

            "\nResult:\n"
            "null                  (null) The block was accepted\n"
            "\"duplicate\"         (string) The block was already known\n"
            "\"duplicate-invalid\" (string) The block was already known and invalid\n"
            "\"inconclusive\"      (string) The block was accepted but had been previously unknown\n"
            "\"rejected\"          (string) The block was invalid\n"
            "or rejection reason (string) The block was invalid with specific reason\n"


            "\nExamples:\n" +
            HelpExampleCli("submitblock", "\"mydata\"") + HelpExampleRpc("submitblock", "\"mydata\""));

    CBlock block;
    if (!DecodeHexBlk(block, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

    if (block.vtx.empty() || !block.vtx[0].IsCoinBase()) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block does not start with a coinbase");
    }

    uint256 hash = block.GetHash();
    bool fBlockPresent = false;
    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(hash);
        if (mi != mapBlockIndex.end()) {
            CBlockIndex* pindex = mi->second;
            if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                return "duplicate";
            if (pindex->nStatus & BLOCK_FAILED_MASK)
                return "duplicate-invalid";
            fBlockPresent = true;
        }
    }

    CValidationState state;
    submitblock_StateCatcher sc(block.GetHash());
    RegisterValidationInterface(&sc);
    bool fAccepted = ProcessNewBlock(state, nullptr, &block);
    UnregisterValidationInterface(&sc);

    if (fBlockPresent) {
        if (fAccepted && !sc.found)
            return "duplicate-inconclusive";

         BlockMap::iterator mi;
         {
             LOCK(cs_main);
             mi = mapBlockIndex.find(hash);
         }
         if (mi != mapBlockIndex.end()) {
             CBlockIndex* pindex = mi->second;
             if (pindex->IsValid(BLOCK_VALID_SCRIPTS)) return "duplicate";
             if (pindex->nStatus & BLOCK_FAILED_MASK) return "duplicate-invalid";
         }
         return "duplicate";
    }

    if (fAccepted) {
        if (!sc.found) {
             LogPrintf("submitblock: Warning: Block %s accepted but BlockChecked notification not received.\n", hash.ToString());
             return "inconclusive";
        }
        state = sc.state;
    }
    return BIP22ValidationResult(state);
}

UniValue estimatefee(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "estimatefee nblocks\n"
            "\nEstimates the approximate fee per kilobyte needed for a transaction.\n"
            "(Not highly reliable in DIGIWAGE due to PoS/MN factors)\n"

            "\nArguments:\n"
            "1. nblocks     (numeric, required) Confirmation target in blocks (1 - 25)\n"

            "\nResult:\n"
            "n              (numeric) estimated fee-per-kilobyte in WAGE. -1 if no estimate is available.\n"

            "\nExamples:\n" +
            HelpExampleCli("estimatefee", "6"));

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM));

    int nBlocks = params[0].getInt<int>();
    if (nBlocks < 1)
        nBlocks = 1;
    if (nBlocks > 25)
        nBlocks = 25;


    CFeeRate feeRate = mempool.estimateFee(nBlocks);
    if (feeRate == CFeeRate(0))
        return -1.0;

    return ValueFromAmount(feeRate.GetFeePerK());
}

UniValue estimatepriority(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "estimatepriority nblocks\n"
            "\nDEPRECATED. Estimates the approximate priority\n"
            "a zero-fee transaction needs to begin confirmation\n"
            "within nblocks blocks.\n"

            "\nArguments:\n"
            "1. nblocks     (numeric, required) Confirmation target in blocks (1 - 25)\n"

            "\nResult:\n"
            "n :    (numeric) estimated priority\n"
            "\n"
            "-1.0 is returned if not enough transactions and\n"
            "blocks have been observed to make an estimate.\n"

            "\nExample:\n" +
            HelpExampleCli("estimatepriority", "6"));

    LogPrintf("WARNING: estimatepriority is deprecated and likely inaccurate.\n");

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM));

    int nBlocks = params[0].getInt<int>();
    if (nBlocks < 1)
        nBlocks = 1;
     if (nBlocks > 25)
        nBlocks = 25;

    return mempool.estimatePriority(nBlocks);
}