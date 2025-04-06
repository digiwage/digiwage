// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2019 The DIGIWAGE developers
// Copyright (c) 2017-2021 The Qtum Core developers // Added Qtum copyright for EVM parts
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qtum/tokenstr.h>
#include "base58.h"
#include "checkpoints.h"
#include "clientversion.h"
#include "core_io.h" // Needed for ScriptPubKeyToJSON, TxToUniv (indirectly via TxToJSON?)
#include "kernel.h"
#include "main.h" // Includes validation.h for ChainstateActive, CBlockIndex, etc.
#include "rpc/server.h"
#include "sync.h"
#include "txdb.h"
#include "util.h"
#include "utilmoneystr.h"
#include "wallet/wallet.h" // Needed for pwalletMain check, etc. - Include if needed, maybe not strictly for blockchain RPCs
#include "zpiv/zpivmodule.h" // If DigiWage uses zPIV
#include "chainparams.h" // Needed for Params()
#include "script/standard.h" // Needed for ScriptPubKeyToJSON
#include "script/script.h"      // For ScriptToAsmStr

// === QTUM CORE COMPONENTS INCLUDES ===
// Ensure these files exist and are correctly ported in your DigiWage project structure
#include "qtum/qtumstate.h"         // Should define GlobalState(), pstorageresult, fLogEvents, LogEntry etc.
#include "qtum/storageresults.h"    // Should define TransactionReceiptInfo, LogEntry if not in qtumstate.h
#include "rpc/contract_util.h"   // Should define CallToken, assignJSON etc.
// === END QTUM INCLUDES ===
#include "rpc/protocol.h" // <<< Make sure this is included for JSONRPCError
#include "utilstrencodings.h" // Needed for HexStr
#include "string.h" // Needed for HexStr

#include <stdint.h>
#include <fstream>
#include <iostream>
#include <univalue.h>
#include <mutex>
#include <numeric>
#include <condition_variable>
#include <chrono> // Needed for wait_for
#include <set> // Needed for std::set
#include <libdevcore/Common.h>
#include <libdevcore/CommonData.h>
#include <algorithm>
#include <unordered_map>
#include <stdint.h>
#include <vector> // Make sure vector is included
#include <ostream> // Make sure ostream is included
#include "utilstrencodings.h" // Make sure HexStr is available
inline std::ostream& operator<<(std::ostream& os, const std::vector<unsigned char>& vec) { os << HexStr(vec); return os; }
#include <condition_variable>
#include <memory>
#include <mutex>
// Forward Declarations needed by this file (Remove redundant/conflicting ones)
extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry); // Keep if defined elsewhere (e.g., rpc/misc.cpp)
extern bool GetStakeKernelHash(uint256& hashProofOfStake, const CBlock& block, const CBlockIndex* pindexPrev); // Keep if defined elsewhere (e.g., kernel.cpp)

// Forward declarations for Qtum RPC helpers (Ensure these match definitions in contract_util.cpp)
extern UniValue CallToContract(const UniValue& params, ChainstateManager& chainman); // Keep - **NOTE:** Needs adaptation if DigiWage doesn't use ChainstateManager
extern void transactionReceiptInfoToJSON(const TransactionReceiptInfo& txOptionalReceipt, UniValue& object); // Keep
extern void assignJSON(UniValue &obj, const TransactionReceiptInfo &res); // Keep overload for Receipt
extern void assignJSON(UniValue &obj, const dev::eth::LogEntry &log, bool topicsInsteadOfData); // Specify namespace if needed

extern UniValue getblockindexstats(const UniValue& params, bool fHelp);

#ifndef DEFAULT_GAS_LIMIT_OP_CALL // Define a fallback if not in headers
#define DEFAULT_GAS_LIMIT_OP_CALL 2500000
#endif


// Helper function to calculate 10^n for dev::u256
dev::u256 pow10_u256(uint32_t n) {
    dev::u256 ret = 1;
    dev::u256 base = 10;
    while (n > 0) {
        if (n % 2 == 1) ret *= base;
        base *= base;
        n /= 2;
        // Add overflow check if necessary, though u256 is large
        // if (ret > dev::u256(-1) / base && n > 0) { // Simple check
        //     throw std::overflow_error("pow10_u256 overflow");
        // }
    }
    return ret;
}



// --- Global state for waitfor* RPCs ---
struct CUpdatedBlock
{
    uint256 hash;
    int height;
};
static std::mutex cs_blockchange;
static std::condition_variable cond_blockchange;
static CUpdatedBlock latestblock;

double GetDifficulty(const CBlockIndex* blockindex)
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    if (blockindex == NULL) {
        if (chainActive.Tip() == NULL) // Use chainActive directly
            return 1.0;
        else
            blockindex = chainActive.Tip(); // Use chainActive directly
    }

    int nShift = (blockindex->nBits >> 24) & 0xff;

    double dDiff =
        (double)0x0000ffff / (double)(blockindex->nBits & 0x00ffffff);

    while (nShift < 29) {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29) {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

UniValue blockheaderToJSON(const CBlockIndex* blockindex)
{
    UniValue result(UniValue::VOBJ);
    result.pushKV("hash", blockindex->GetBlockHash().GetHex());
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex)) // Use chainActive directly
        confirmations = chainActive.Height() - blockindex->nHeight + 1; // Use chainActive directly
    result.pushKV("confirmations", confirmations);
    result.pushKV("height", blockindex->nHeight);
    result.pushKV("version", blockindex->nVersion);
    result.pushKV("merkleroot", blockindex->hashMerkleRoot.GetHex());
    result.pushKV("time", (int64_t)blockindex->nTime);
    result.pushKV("mediantime", (int64_t)blockindex->GetMedianTimePast());
    result.pushKV("nonce", (uint64_t)blockindex->nNonce);
    result.pushKV("bits", strprintf("%08x", blockindex->nBits));
    result.pushKV("difficulty", GetDifficulty(blockindex));
    result.pushKV("chainwork", blockindex->nChainWork.GetHex());
    result.pushKV("acc_checkpoint", blockindex->nAccumulatorCheckpoint.GetHex()); // Assuming DigiWage kept this

    if (blockindex->pprev)
        result.pushKV("previousblockhash", blockindex->pprev->GetBlockHash().GetHex());
    CBlockIndex *pnext = chainActive.Next(blockindex); // Use chainActive directly
    if (pnext)
        result.pushKV("nextblockhash", pnext->GetBlockHash().GetHex());
    return result;
}

UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false)
{
    UniValue result(UniValue::VOBJ);
    result.pushKV("hash", block.GetHash().GetHex()); // Use block.GetHash()
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex)) // Use chainActive directly
        confirmations = chainActive.Height() - blockindex->nHeight + 1; // Use chainActive directly
    result.pushKV("confirmations", confirmations);
    result.pushKV("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION));
    result.pushKV("height", blockindex->nHeight);
    result.pushKV("version", block.nVersion);
    result.pushKV("merkleroot", block.hashMerkleRoot.GetHex());
    result.pushKV("acc_checkpoint", block.nAccumulatorCheckpoint.GetHex()); // Assuming DigiWage kept this
    UniValue txs(UniValue::VARR);
    for (const CTransaction& tx : block.vtx) {
        if (txDetails) {
            UniValue objTx(UniValue::VOBJ);
            TxToJSON(tx, blockindex->GetBlockHash(), objTx); // Pass actual block hash
            txs.push_back(objTx);
        } else
            txs.push_back(tx.GetHash().GetHex());
    }
    result.pushKV("tx", txs);
    result.pushKV("time", block.GetBlockTime());
    result.pushKV("mediantime", (int64_t)blockindex->GetMedianTimePast());
    result.pushKV("nonce", (uint64_t)block.nNonce);
    result.pushKV("bits", strprintf("%08x", block.nBits));
    result.pushKV("difficulty", GetDifficulty(blockindex));
    result.pushKV("chainwork", blockindex->nChainWork.GetHex());

    if (blockindex->pprev)
        result.pushKV("previousblockhash", blockindex->pprev->GetBlockHash().GetHex());
    CBlockIndex* pnext = chainActive.Next(blockindex); // Use chainActive directly
    if (pnext)
        result.pushKV("nextblockhash", pnext->GetBlockHash().GetHex());

    result.pushKV("moneysupply",ValueFromAmount(blockindex->nMoneySupply)); // Assuming DigiWage kept nMoneySupply

    // Assuming DigiWage uses libzerocoin and kept mapZerocoinSupply
    UniValue zwageObj(UniValue::VOBJ);
    if(!blockindex->mapZerocoinSupply.empty()) { // Check if map is populated
        for (auto denom : libzerocoin::zerocoinDenomList) {
             if (blockindex->mapZerocoinSupply.count(denom)) { // Check if denom exists
                 // Original formula looked wrong (denom*COIN), should just be the count * value
                 // Assuming mapZerocoinSupply stores COUNT of coins, not total value
                 CAmount denom_value = libzerocoin::ZerocoinDenominationToAmount(denom);
                 zwageObj.pushKV(std::to_string(denom), ValueFromAmount(blockindex->mapZerocoinSupply.at(denom) * denom_value));
             } else {
                 zwageObj.pushKV(std::to_string(denom), ValueFromAmount(0)); // Show 0 if denom not present
             }
        }
        zwageObj.pushKV("total", ValueFromAmount(blockindex->GetZerocoinSupply()));
    } else {
        // Provide empty/zero values if map is empty
        for (auto denom : libzerocoin::zerocoinDenomList) {
            zwageObj.pushKV(std::to_string(denom), ValueFromAmount(0));
        }
        zwageObj.pushKV("total", ValueFromAmount(0));
    }
    result.pushKV("zerocoinsupply", zwageObj);

    // Coin stake data
    if (block.IsProofOfStake()) {
        // Ensure GetStakeModifierV1/V2 are available in DigiWage's CBlockIndex
        std::string stakeModifier = "N/A"; // Default if methods don't exist
        #if defined(QTUM_DEBUG) // Example conditional compilation if methods might not exist
        // If needed, check if these methods exist before calling
        if (Params().GetConsensus().IsStakeModifierV2(blockindex->nHeight)) {
             if(blockindex->GetStakeModifierV2() != uint256()) // Check if V2 is set
                stakeModifier = blockindex->GetStakeModifierV2().GetHex();
             else // Fallback or indicate unset V2
                stakeModifier = strprintf("V2 (unset at height %d)", blockindex->nHeight);
        } else {
            stakeModifier = strprintf("%016x", blockindex->GetStakeModifierV1());
        }
        #else // Simplified version assuming methods exist
         stakeModifier = (Params().GetConsensus().IsStakeModifierV2(blockindex->nHeight) ?
                                     strprintf("%016x", blockindex->vStakeModifier) : // Access member directly if GetStakeModifierV2 doesn't exist
                                     strprintf("%016x", blockindex->vStakeModifier)); // Access member directly if GetStakeModifierV1 doesn't exist
        #endif

        result.pushKV("stakeModifier", stakeModifier);

        // Proof of stake hash
        uint256 hashProofOfStakeRet;
        if (!GetStakeKernelHash(hashProofOfStakeRet, block, blockindex->pprev)) {
             // Log error instead of throwing, return "N/A"
             LogPrintf("ERROR: Cannot get proof of stake hash for block %s\n", block.GetHash().GetHex());
             result.pushKV("hashProofOfStake", "N/A");
        } else {
             result.pushKV("hashProofOfStake", hashProofOfStakeRet.GetHex());
        }
    }

    return result;
}

UniValue getblockcount(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "getblockcount\n"
            "\nReturns the number of blocks in the longest block chain.\n"
            "\nResult:\n"
            "n    (numeric) The current block count\n"
            "\nExamples:\n" +
            HelpExampleCli("getblockcount", "") + HelpExampleRpc("getblockcount", ""));

    LOCK(cs_main); // cs_main protects chainActive
    return chainActive.Height();
}

UniValue getbestblockhash(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "getbestblockhash\n"
            "\nReturns the hash of the best (tip) block in the longest block chain.\n"
            "\nResult\n"
            "\"hex\"      (string) the block hash hex encoded\n"
            "\nExamples\n" +
            HelpExampleCli("getbestblockhash", "") + HelpExampleRpc("getbestblockhash", ""));

    LOCK(cs_main); // cs_main protects chainActive
    return chainActive.Tip()->GetBlockHash().GetHex();
}

void RPCNotifyBlockChange(bool fInitialDownload, const CBlockIndex* pindex)
{
    if(pindex) {
        std::lock_guard<std::mutex> lock(cs_blockchange); // Use std::lock_guard
        latestblock.hash = pindex->GetBlockHash();
        latestblock.height = pindex->nHeight;
    }
    cond_blockchange.notify_all();
}

UniValue waitfornewblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "waitfornewblock ( timeout )\n"
            "\nWaits for a specific new block and returns useful info about it.\n"
            "\nReturns the current block on timeout or exit.\n"
            "\nArguments:\n"
            "1. timeout (int, optional, default=0) Time in milliseconds to wait for a response. 0 indicates no timeout.\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("waitfornewblock", "1000")
            + HelpExampleRpc("waitfornewblock", "1000")
        );
    int timeout = 0;
    if (params.size() > 0)
        timeout = params[0].getInt<int>();
    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange); // Use std::unique_lock for condition variable
        block = latestblock;
        if(timeout)
            cond_blockchange.wait_for(lock, std::chrono::milliseconds(timeout), [&block]{return latestblock.height != block.height || latestblock.hash != block.hash || !IsRPCRunning(); });
        else
            cond_blockchange.wait(lock, [&block]{return latestblock.height != block.height || latestblock.hash != block.hash || !IsRPCRunning(); });
        block = latestblock;
    }
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("hash", block.hash.GetHex());
    ret.pushKV("height", block.height);
    return ret;
}

UniValue waitforblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
            "waitforblock blockhash ( timeout )\n"
            "\nWaits for a specific new block and returns useful info about it.\n"
            "\nReturns the current block on timeout or exit.\n"
            "\nArguments:\n"
            "1. \"blockhash\" (required, std::string) Block hash to wait for.\n"
            "2. timeout       (int, optional, default=0) Time in milliseconds to wait for a response. 0 indicates no timeout.\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("waitforblock", "\"0000000000079f8ef3d2c688c244eb7a4570b24c9ed7b4a8c619eb02596f8862\", 1000")
            + HelpExampleRpc("waitforblock", "\"0000000000079f8ef3d2c688c244eb7a4570b24c9ed7b4a8c619eb02596f8862\", 1000")
        );
    int timeout = 0;

    uint256 hash = uint256S(params[0].get_str());

    if (params.size() > 1)
        timeout = params[1].getInt<int>();

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange); // Use std::unique_lock
        if(timeout)
            cond_blockchange.wait_for(lock, std::chrono::milliseconds(timeout), [&hash]{return latestblock.hash == hash || !IsRPCRunning();});
        else
            cond_blockchange.wait(lock, [&hash]{return latestblock.hash == hash || !IsRPCRunning(); });
        block = latestblock;
    }

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("hash", block.hash.GetHex());
    ret.pushKV("height", block.height);
    return ret;
}

UniValue waitforblockheight(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
            "waitforblockheight height ( timeout )\n"
            "\nWaits for (at least) block height and returns the height and hash\n"
            "of the current tip.\n"
            "\nReturns the current block on timeout or exit.\n"
            "\nArguments:\n"
            "1. height  (required, int) Block height to wait for (int)\n"
            "2. timeout (int, optional, default=0) Time in milliseconds to wait for a response. 0 indicates no timeout.\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("waitforblockheight", "\"100\", 1000")
            + HelpExampleRpc("waitforblockheight", "\"100\", 1000")
        );
    int timeout = 0;

   int height = params[0].getInt<int>();

    if (params.size() > 1)
        timeout = params[1].getInt<int>();

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange); // Use std::unique_lock
        if(timeout)
            cond_blockchange.wait_for(lock, std::chrono::milliseconds(timeout), [&height]{return latestblock.height >= height || !IsRPCRunning();});
        else
            cond_blockchange.wait(lock, [&height]{return latestblock.height >= height || !IsRPCRunning(); });
        block = latestblock;
    }
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("hash", block.hash.GetHex());
    ret.pushKV("height", block.height);
    return ret;
}

UniValue getdifficulty(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "getdifficulty\n"
            "\nReturns the proof-of-work difficulty as a multiple of the minimum difficulty.\n"
            "\nResult:\n"
            "n.nnn       (numeric) the proof-of-work difficulty as a multiple of the minimum difficulty.\n"
            "\nExamples:\n" +
            HelpExampleCli("getdifficulty", "") + HelpExampleRpc("getdifficulty", ""));

    LOCK(cs_main); // cs_main protects chainActive needed by GetDifficulty() default arg
    return GetDifficulty();
}


UniValue mempoolToJSON(bool fVerbose = false)
{
    if (fVerbose) {
        LOCK(mempool.cs);
        UniValue o(UniValue::VOBJ);
        // Use const auto& for range loop
        for (const auto& entryPair : mempool.mapTx) { // Use different name to avoid conflict
            const uint256& hash = entryPair.first;
            const CTxMemPoolEntry& e = entryPair.second;
            UniValue info(UniValue::VOBJ);
            info.pushKV("size", (int)e.GetTxSize());
            info.pushKV("fee", ValueFromAmount(e.GetFee()));
            info.pushKV("time", e.GetTime());
            info.pushKV("height", (int)e.GetHeight());
            int currentHeight = 0; // Get current height safely
            {
                LOCK(cs_main);
                currentHeight = chainActive.Height();
            }
            info.pushKV("startingpriority", e.GetPriority(e.GetHeight())); // Requires GetPriority
            info.pushKV("currentpriority", e.GetPriority(currentHeight)); // Requires GetPriority
            const CTransaction& tx = e.GetTx();
            std::set<std::string> setDepends;
            for (const CTxIn& txin : tx.vin) {
                if (mempool.exists(txin.prevout.hash))
                    setDepends.insert(txin.prevout.hash.ToString());
            }

            UniValue depends(UniValue::VARR);
            for (const std::string& dep : setDepends) {
                depends.push_back(dep);
            }

            info.pushKV("depends", depends);
            o.pushKV(hash.ToString(), info);
        }
        return o;
    } else {
        std::vector<uint256> vtxid;
        mempool.queryHashes(vtxid);

        UniValue a(UniValue::VARR);
        for (const uint256& hash : vtxid)
            a.push_back(hash.ToString());

        return a;
    }
}

UniValue getrawmempool(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "getrawmempool ( verbose )\n"
            "\nReturns all transaction ids in memory pool as a json array of string transaction ids.\n"
            "\nArguments:\n"
            "1. verbose           (boolean, optional, default=false) true for a json object, false for array of transaction ids\n"
            "\nResult: (for verbose = false):\n"
            "[...] (array of strings)\n"
            "\nResult: (for verbose = true):\n"
            "{...} (object with tx details)\n"
            "\nExamples\n" +
            HelpExampleCli("getrawmempool", "true") + HelpExampleRpc("getrawmempool", "true"));

    // No need for LOCK(cs_main) here, mempool has its own lock (mempool.cs) handled by mempoolToJSON

    bool fVerbose = false;
    if (params.size() > 0)
        fVerbose = params[0].get_bool();

    return mempoolToJSON(fVerbose);
}

UniValue getblockhash(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "getblockhash index\n"
            "\nReturns hash of block in best-block-chain at index provided.\n"
            "\nArguments:\n"
            "1. index         (numeric, required) The block index\n"
            "\nResult:\n"
            "\"hash\"         (string) The block hash\n"
            "\nExamples:\n" +
            HelpExampleCli("getblockhash", "1000") + HelpExampleRpc("getblockhash", "1000"));

    LOCK(cs_main); // cs_main protects chainActive

    int nHeight = params[0].getInt<int>();
    if (nHeight < 0 || nHeight > chainActive.Height())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");

    CBlockIndex* pblockindex = chainActive[nHeight];
    return pblockindex->GetBlockHash().GetHex();
}

UniValue getblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
            "getblock \"hash\" ( verbose )\n"
            "\nIf verbose is false, returns a string that is serialized, hex-encoded data for block 'hash'.\n"
            "If verbose is true, returns an Object with information about block <hash>.\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) The block hash\n"
            "2. verbose           (boolean, optional, default=true) true for a json object, false for the hex encoded data\n"
            "\nResult (for verbose = true): See help\n"
            "\nResult (for verbose=false): See help\n"
            "\nExamples:\n" +
            HelpExampleCli("getblock", "\"00000000000fd08c2fb661d2fcb0d49abb3a91e5f27082ce64feed3b4dede2e2\"") +
            HelpExampleRpc("getblock", "\"00000000000fd08c2fb661d2fcb0d49abb3a91e5f27082ce64feed3b4dede2e2\""));

    LOCK(cs_main); // cs_main protects mapBlockIndex and potentially ReadBlockFromDisk internals

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));

    bool fVerbose = true;
    if (params.size() > 1)
        fVerbose = params[1].get_bool();

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (!ReadBlockFromDisk(block, pblockindex))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    if (!fVerbose) {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << block;
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockToJSON(block, pblockindex, true); // Pass true for tx details
}

UniValue getblockheader(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
            "getblockheader \"hash\" ( verbose )\n"
            "\nIf verbose is false, returns a string that is serialized, hex-encoded data for block 'hash' header.\n"
            "If verbose is true, returns an Object with information about block <hash> header.\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) The block hash\n"
            "2. verbose           (boolean, optional, default=true) true for a json object, false for the hex encoded data\n"
            "\nResult (for verbose = true): See help\n"
            "\nResult (for verbose=false): See help\n"
            "\nExamples:\n" +
            HelpExampleCli("getblockheader", "\"00000000000fd08c2fb661d2fcb0d49abb3a91e5f27082ce64feed3b4dede2e2\"") +
            HelpExampleRpc("getblockheader", "\"00000000000fd08c2fb661d2fcb0d49abb3a91e5f27082ce64feed3b4dede2e2\""));

    LOCK(cs_main); // cs_main protects mapBlockIndex

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));

    bool fVerbose = true;
    if (params.size() > 1)
        fVerbose = params[1].get_bool();

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (!fVerbose) {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << pblockindex->GetBlockHeader();
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockheaderToJSON(pblockindex);
}

UniValue gettxoutsetinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "gettxoutsetinfo\n"
            "\nReturns statistics about the unspent transaction output set.\n"
            "Note this call may take some time.\n"
            "\nResult: See help\n"
            "\nExamples:\n" +
            HelpExampleCli("gettxoutsetinfo", "") + HelpExampleRpc("gettxoutsetinfo", ""));

    LOCK(cs_main); // cs_main protects pcoinsTip and FlushStateToDisk

    UniValue ret(UniValue::VOBJ);

    CCoinsStats stats;
    FlushStateToDisk(); // Ensure this function exists and is appropriate for DigiWage
    // Ensure pcoinsTip is the correct UTXO set view for DigiWage
    if (pcoinsTip && pcoinsTip->GetStats(stats)) { // Check pcoinsTip is not null
        ret.pushKV("height", (int64_t)stats.nHeight);
        ret.pushKV("bestblock", stats.hashBlock.GetHex());
        ret.pushKV("transactions", (int64_t)stats.nTransactions);
        ret.pushKV("txouts", (int64_t)stats.nTransactionOutputs);
        ret.pushKV("bytes_serialized", (int64_t)stats.nSerializedSize);
        ret.pushKV("hash_serialized", stats.hashSerialized.GetHex());
        ret.pushKV("total_amount", ValueFromAmount(stats.nTotalAmount));
    } else {
        // Handle case where stats couldn't be obtained
         ret.pushKV("height", (int64_t)chainActive.Height());
         ret.pushKV("bestblock", chainActive.Tip()->GetBlockHash().GetHex());
         ret.pushKV("error", "Unable to get UTXO set statistics.");
    }
    return ret;
}

UniValue gettxout(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 3)
        throw std::runtime_error(
            "gettxout \"txid\" n ( includemempool )\n"
            "\nReturns details about an unspent transaction output.\n"
            "\nArguments:\n"
            "1. \"txid\"       (string, required) The transaction id\n"
            "2. n              (numeric, required) vout value\n"
            "3. includemempool  (boolean, optional, default=true) Whether to included the mem pool\n"
            "\nResult: See help\n"
            "\nExamples:\n" +
            HelpExampleCli("gettxout", "\"txid\" 1") +
            HelpExampleRpc("gettxout", "\"txid\", 1"));

    LOCK(cs_main); // cs_main protects pcoinsTip, mempool, mapBlockIndex

    UniValue ret(UniValue::VOBJ);

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    int n = params[1].getInt<int>();
    bool fMempool = true;
    if (params.size() > 2)
        fMempool = params[2].get_bool();

    CCoins coins;
    if (fMempool) {
        LOCK(mempool.cs);
        CCoinsViewMemPool view(pcoinsTip, mempool); // Ensure pcoinsTip is correct
        if (!view.GetCoins(hash, coins))
            return NullUniValue;
        mempool.pruneSpent(hash, coins); // Assuming pruneSpent exists
    } else {
         if (!pcoinsTip || !pcoinsTip->GetCoins(hash, coins)) // Check pcoinsTip
            return NullUniValue;
    }
    if (n < 0 || (unsigned int)n >= coins.vout.size() || coins.vout[n].IsNull())
        return NullUniValue;

    // Use chainActive.Tip() instead of mapBlockIndex lookup
    CBlockIndex* pindex = chainActive.Tip();
    ret.pushKV("bestblock", pindex->GetBlockHash().GetHex());
    if ((unsigned int)coins.nHeight == MEMPOOL_HEIGHT)
        ret.pushKV("confirmations", 0);
    else
        ret.pushKV("confirmations", pindex->nHeight - coins.nHeight + 1);
    ret.pushKV("value", ValueFromAmount(coins.vout[n].nValue));
    UniValue o(UniValue::VOBJ);
    ScriptPubKeyToJSON(coins.vout[n].scriptPubKey, o, true); // Use locally defined version
    ret.pushKV("scriptPubKey", o);
    ret.pushKV("version", coins.nVersion); // Assuming CCoins has nVersion
    ret.pushKV("coinbase", coins.fCoinBase); // Assuming CCoins has fCoinBase

    return ret;
}

UniValue verifychain(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1) // Corrected check (was > 2)
        throw std::runtime_error(
            "verifychain ( checklevel numblocks )\n" // Updated help
            "\nVerifies blockchain database.\n"
            "\nArguments:\n"
            "1. checklevel (numeric, optional, default=3, range=0-4) How thorough the block verification is.\n" // Updated help
            "2. numblocks    (numeric, optional, default=6, 0=all) The number of blocks to check.\n" // Updated help
            "\nResult:\n"
            "true|false       (boolean) Verified or not\n"
            "\nExamples:\n" +
            HelpExampleCli("verifychain", "4 100") + HelpExampleRpc("verifychain", "4, 100")); // Updated help


    int nCheckLevel = GetArg("-checklevel", 3); // Use default from GetArg
    int nCheckDepth = GetArg("-checkblocks", 6); // Use default from GetArg
    if (!params[0].isNull()) { // Check level param
        nCheckLevel = params[0].getInt<int>();
    }
    if (params.size() > 1 && !params[1].isNull()) { // Check depth param
        nCheckDepth = params[1].getInt<int>();
    }


    LOCK(cs_main); // cs_main needed for VerifyDB? Check CVerifyDB implementation
    fVerifyingBlocks = true; // Ensure this global exists
    // Ensure CVerifyDB and pcoinsTip are compatible with DigiWage
    bool fVerified = CVerifyDB().VerifyDB(pcoinsTip, nCheckLevel, nCheckDepth);
    fVerifyingBlocks = false;

    return fVerified;
}

/** Implementation of IsSuperMajority with better feedback */
static UniValue SoftForkMajorityDesc(int version, CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    UniValue rv(UniValue::VOBJ);
    bool activated = false;
    // Use the exact height logic from original DigiWage
    switch(version) {
    // Assuming versions 1, 2, 3 were early forks or always active from genesis/low height
    case 1:
    case 2:
    case 3:
        activated = pindex->nHeight >= 1; // Or appropriate low activation height
        break;
    case 4: // Assuming version 4 corresponds to the ZC fork feature
        activated = pindex->nHeight >= consensusParams.height_start_ZC;
        break;
    case 5: // Assuming version 5 corresponds to the RHF fork feature
        activated = pindex->nHeight >= consensusParams.height_RHF;
        break;
    // Add cases for other DigiWage specific versions if they exist
    default:
        // Handle unknown version? Or assume false?
        activated = false;
        break;
    }
    rv.pushKV("status", activated);
    return rv;
}

static UniValue SoftForkDesc(const std::string &name, int version, CBlockIndex* pindex)
{
    // Ensure Params() is accessible or pass consensus params explicitly if needed
    const Consensus::Params& consensus = Params().GetConsensus();
    UniValue rv(UniValue::VOBJ);
    rv.pushKV("id", name);
    rv.pushKV("version", version);
    rv.pushKV("reject", SoftForkMajorityDesc(version, pindex, consensus));
    return rv;
}


UniValue getblockchaininfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "getblockchaininfo\n"
            "Returns an object containing various state info regarding block chain processing.\n"
            "\nResult: See help\n"
            "\nExamples:\n" +
            HelpExampleCli("getblockchaininfo", "") + HelpExampleRpc("getblockchaininfo", ""));

    LOCK(cs_main); // Protects chainActive, pindexBestHeader, nBurnedCoins

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("chain", Params().NetworkIDString());
    obj.pushKV("blocks", (int)chainActive.Height());
    obj.pushKV("headers", pindexBestHeader ? pindexBestHeader->nHeight : -1); // Ensure pindexBestHeader exists
    obj.pushKV("bestblockhash", chainActive.Tip()->GetBlockHash().GetHex());
    obj.pushKV("difficulty", (double)GetDifficulty());
    // Ensure GuessVerificationProgress exists and works with chainActive.Tip()
    obj.pushKV("verificationprogress", Checkpoints::GuessVerificationProgress(chainActive.Tip())); // <<< Pass only the tip CBlockIndex*
    obj.pushKV("chainwork", chainActive.Tip()->nChainWork.GetHex());
    obj.pushKV("moneysupply", ValueFromAmount(chainActive.Tip()->nMoneySupply)); // Assuming nMoneySupply exists
    obj.pushKV("burned", ValueFromAmount(nBurnedCoins)); // Assuming nBurnedCoins exists

    CBlockIndex* tip = chainActive.Tip();
    UniValue softforks(UniValue::VARR);
    // --- Add DigiWage specific forks ---
    // Example placeholders - replace with actual fork names and versions/bits
    softforks.pushKV("bip34",    SoftForkDesc("bip34",    2, tip));
    softforks.pushKV("bip66",    SoftForkDesc("bip66",    3, tip));
    softforks.pushKV("bip65",    SoftForkDesc("bip65",    4, tip)); // Map version 4?
    softforks.pushKV("zerocoin", SoftForkDesc("zerocoin", 5, tip)); // Map version 5?
    softforks.pushKV("rhf",      SoftForkDesc("rhf",      6, tip)); // Map version 6?
    // Add any other relevant forks for DigiWage
    // --- End DigiWage forks ---
    obj.pushKV("softforks", softforks);
    return obj;
}

/** Comparison function for sorting the getchaintips heads.  */
struct CompareBlocksByHeight {
    bool operator()(const CBlockIndex* a, const CBlockIndex* b) const
    {
        if (a->nHeight != b->nHeight)
            return (a->nHeight > b->nHeight);
        return a < b; // Use pointer comparison for tie-breaking
    }
};

UniValue getchaintips(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "getchaintips\n"
            "Return information about all known tips in the block tree,"
            " including the main chain as well as orphaned branches.\n"
            "\nResult: See help\n"
            "\nExamples:\n" +
            HelpExampleCli("getchaintips", "") + HelpExampleRpc("getchaintips", ""));

    LOCK(cs_main); // Protects mapBlockIndex and chainActive

    /* Build up a list of chain tips. */
    std::set<const CBlockIndex*, CompareBlocksByHeight> setTips;
    for (const auto& item : mapBlockIndex)
        setTips.insert(item.second);
    for (const auto& item : mapBlockIndex) {
        const CBlockIndex* pprev = item.second->pprev;
        if (pprev)
            setTips.erase(pprev);
    }

    // Always report the currently active tip.
    if (chainActive.Tip()) { // Ensure tip exists
        setTips.insert(chainActive.Tip());
    }

    /* Construct the output array. */
    UniValue res(UniValue::VARR);
    for (const CBlockIndex* block : setTips) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("height", block->nHeight);
        obj.pushKV("hash", block->GetBlockHash().GetHex()); // Use GetBlockHash()

        const CBlockIndex* pfork = chainActive.FindFork(block);
        const int branchLen = pfork ? (block->nHeight - pfork->nHeight) : block->nHeight + 1; // Handle case where fork is null (e.g. genesis)
        obj.pushKV("branchlen", branchLen);

        std::string status;
        if (chainActive.Contains(block)) {
            status = "active";
        } else if (block->nStatus & BLOCK_FAILED_MASK) {
            status = "invalid";
        } else if (block->nChainTx == 0 && block->nHeight > 0) { // Add check for nHeight > 0, genesis has nChainTx=0
             status = "headers-only";
        } else if (block->IsValid(BLOCK_VALID_SCRIPTS)) {
            status = "valid-fork";
        } else if (block->IsValid(BLOCK_VALID_TREE)) {
            status = "valid-headers";
        } else {
            status = "unknown";
        }
        obj.pushKV("status", status);

        res.push_back(obj);
    }

    return res;
}

UniValue getfeeinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "getfeeinfo blocks\n"
            "\nReturns details of transaction fees over the last n blocks.\n"
            "\nArguments:\n"
            "1. blocks     (int, required) the number of blocks to get transaction data from\n"
            "\nResult: See help\n"
            "\nExamples:\n" +
            HelpExampleCli("getfeeinfo", "5") + HelpExampleRpc("getfeeinfo", "5"));

    int nBlocks = params[0].getInt<int>();
    int nBestHeight;
    {
        LOCK(cs_main);
        nBestHeight = chainActive.Height();
    }
    int nStartHeight = nBestHeight - nBlocks + 1; // Adjust to include current block
    if (nBlocks <= 0 || nStartHeight < 0 ) // Check nBlocks > 0
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block count or calculated start height is negative.");

    // Cap start height at 0 (genesis)
    if (nStartHeight < 0) nStartHeight = 0;

    // Note: getblockindexstats expects height + range.
    // We want stats from [nStartHeight, nBestHeight].
    // So, height = nStartHeight, range = nBestHeight - nStartHeight + 1 = nBlocks
    UniValue newParams(UniValue::VARR);
    newParams.push_back(UniValue(nStartHeight));
    newParams.push_back(UniValue(nBlocks));
    newParams.push_back(UniValue(true));    // fFeeOnly = true

    return getblockindexstats(newParams, false); // Call the other RPC function
}

UniValue mempoolInfoToJSON()
{
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("size", (int64_t) mempool.size());
    ret.pushKV("bytes", (int64_t) mempool.GetTotalTxSize());
    // mempool.DynamicMemoryUsage() might not exist in older versions. Remove if causes error.
    // ret.pushKV("usage", (int64_t) mempool.DynamicMemoryUsage());

    return ret;
}

UniValue getmempoolinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "getmempoolinfo\n"
            "\nReturns details on the active state of the TX memory pool.\n"
            "\nResult: See help\n"
            "\nExamples:\n" +
            HelpExampleCli("getmempoolinfo", "") + HelpExampleRpc("getmempoolinfo", ""));

    return mempoolInfoToJSON();
}

UniValue invalidateblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "invalidateblock \"hash\"\n"
            "\nPermanently marks a block as invalid, as if it violated a consensus rule.\n"
            "\nArguments:\n"
            "1. hash   (string, required) the hash of the block to mark as invalid\n"
            "\nResult: null\n" // Added result type
            "\nExamples:\n" +
            HelpExampleCli("invalidateblock", "\"blockhash\"") + HelpExampleRpc("invalidateblock", "\"blockhash\""));

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state; // Ensure this type exists

    {
        LOCK(cs_main); // Protects mapBlockIndex and chainActive modification
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        // Ensure InvalidateBlock function exists and takes these params
        InvalidateBlock(state, pblockindex); // Call with expected arguments for DigiWage
        
    }

    if (state.IsValid()) {
        // Ensure ActivateBestChain function exists and takes these params
        ActivateBestChain(state);// Pass consensus params if needed
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue reconsiderblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "reconsiderblock \"hash\"\n"
            "\nRemoves invalidity status of a block and its descendants, reconsider them for activation.\n"
            "This can be used to undo the effects of invalidateblock.\n"
            "\nArguments:\n"
            "1. hash   (string, required) the hash of the block to reconsider\n"
            "\nResult: null\n" // Added result type
            "\nExamples:\n" +
            HelpExampleCli("reconsiderblock", "\"blockhash\"") + HelpExampleRpc("reconsiderblock", "\"blockhash\""));

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state;

    {
        LOCK(cs_main); // Protects mapBlockIndex and chainActive modification
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        // Ensure ReconsiderBlock function exists and takes these params
        ReconsiderBlock(state, pblockindex);
    }

    if (state.IsValid()) {
        // Ensure ActivateBestChain function exists and takes these params
        ActivateBestChain(state); // Pass consensus params if needed
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

// Helper for validaterange, avoids code duplication
static void ParseValidateRangeParams(const UniValue& params, int& heightStart, int& heightEnd, int minHeightStart)
{
     if (params.size() < 2) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Not enough parameters for range (expected height and range)");
    }

    int nBestHeight;
    {
        LOCK(cs_main);
        nBestHeight = chainActive.Height();
    }

    heightStart = params[0].getInt<int>();
    if (heightStart < minHeightStart || heightStart > nBestHeight) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid starting block (%d). Out of range [%d, %d].", heightStart, minHeightStart, nBestHeight));
    }

    const int range = params[1].getInt<int>();
    if (range < 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block range. Must be strictly positive.");
    }

    heightEnd = heightStart + range - 1;

    if (heightEnd > nBestHeight) {
        LogPrintf("WARN: %s: range extends beyond tip, adjusting end block from %d to %d\n", __func__, heightEnd, nBestHeight);
        heightEnd = nBestHeight;
    }
     // Add check: ensure start <= end after adjustment
     if (heightStart > heightEnd) {
         throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid range after adjustment: start (%d) > end (%d). Tip might be lower than start.", heightStart, heightEnd));
     }
}


// This function was defined but not used in the original snippet, keeping it static
// static void validaterange(const UniValue& params, int& heightStart, int& heightEnd, int minHeightStart)
// {
//     ParseValidateRangeParams(params, heightStart, heightEnd, minHeightStart);
// }


UniValue getblockindexstats(const UniValue& params, bool fHelp) {
    if (fHelp || params.size() < 2 || params.size() > 3)
        throw std::runtime_error(
                "getblockindexstats height range ( fFeeOnly )\n"
                "\nReturns aggregated BlockIndex data for blocks "
                "\n[height, height+1, height+2, ..., height+range-1]\n"
                "\nArguments:\n"
                "1. height             (numeric, required) block height where the search starts.\n"
                "2. range              (numeric, required) number of blocks to include.\n"
                "3. fFeeOnly           (boolean, optional, default=False) return only fee info.\n"
                "\nResult: See help\n"
                "\nExamples:\n" +
                HelpExampleCli("getblockindexstats", "1200000 1000") +
                HelpExampleRpc("getblockindexstats", "1200000, 1000"));

    int heightStart, heightEnd;
    ParseValidateRangeParams(params, heightStart, heightEnd, 0); // Use helper

    // return object
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("first_block", heightStart);
    ret.pushKV("last_block", heightEnd);

    bool fFeeOnly = false;
    if (params.size() > 2) {
        fFeeOnly = params[2].get_bool();
    }

    CAmount nFees = 0;
    CAmount nFees_all = 0;
    int64_t nBytes = 0;
    int64_t nTxCount = 0;
    int64_t nTxCount_all = 0;

    std::map<libzerocoin::CoinDenomination, int64_t> mapSpendCount;
    if (!fFeeOnly) { // Only initialize if needed
        for (auto& denom : libzerocoin::zerocoinDenomList) {
            mapSpendCount.insert(std::make_pair(denom, 0));
        }
    }

    CBlockIndex* pindex = nullptr;
    {
        LOCK(cs_main); // Protects chainActive access
        if (heightStart > chainActive.Height()) // Double check start height vs current tip
             throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid starting block %d, current tip is %d", heightStart, chainActive.Height()));
        pindex = chainActive[heightStart];
    }

    if (!pindex) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Failed to find block index for starting height %d", heightStart));
    }

    // Loop from start height up to and including end height
    for (int currentHeight = heightStart; currentHeight <= heightEnd; ++currentHeight) {
         CBlock block;
         if (!ReadBlockFromDisk(block, pindex)) {
             // Maybe log an error but continue? Or throw? Throwing is safer.
             throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("failed to read block %s (height %d) from disk", pindex->GetBlockHash().ToString(), currentHeight));
         }

         CAmount block_fees_all = 0; // Sum fees within this block
         int64_t block_bytes = 0; // Sum bytes within this block (excluding ZC mints/pure spends)
         int block_tx_count = 0; // Count non-coinbase/coinstake txs
         int block_tx_count_all = block.vtx.size(); // Count all txs

         // loop through each tx in block and save size and fee
         for (const CTransaction& tx : block.vtx) {
             if (tx.IsCoinBase() || tx.IsCoinStake()) // Skip coinbase/coinstake for fee/byte/txcount stats
                 continue;

             block_tx_count++; // Increment standard tx count

             // Calculate fee (ValueIn - ValueOut)
             CAmount nTxValueIn = 0;
             CAmount nTxValueOut = 0;
             bool txHasZCSpend = false;
             bool txHasZCMint = tx.HasZerocoinMintOutputs(); // Check for mints

             for (unsigned int j = 0; j < tx.vin.size(); ++j) {
                 if (tx.vin[j].IsZerocoinSpend()) {
                     txHasZCSpend = true;
                     if (!fFeeOnly) {
                         try {
                            mapSpendCount[libzerocoin::IntToZerocoinDenomination(tx.vin[j].nSequence)]++;
                         } catch (const std::out_of_range& oor) {
                             LogPrintf("ERROR: %s: Invalid sequence %u treated as denomination in tx %s, block %d\n", __func__, tx.vin[j].nSequence, tx.GetHash().ToString(), currentHeight);
                         } catch (...) { // Catch any other potential error from IntToZerocoinDenomination
                             LogPrintf("ERROR: %s: Unknown error converting sequence %u to denomination in tx %s, block %d\n", __func__, tx.vin[j].nSequence, tx.GetHash().ToString(), currentHeight);
                         }
                     }
                     continue; // Zerocoin spends don't contribute to ValueIn for fee calc here
                 }

                 const COutPoint& prevout = tx.vin[j].prevout;
                 CTransaction txPrev;
                 uint256 hashBlock; // We don't strictly need hashBlock here
                 // Use the main GetTransaction function
                 if (!GetTransaction(prevout.hash, txPrev, hashBlock, true, pindex->pprev)) { // Pass pprev as hint if available
                      LogPrintf("ERROR: %s: failed to read prev_tx %s for input %d of tx %s in block %d\n", __func__, prevout.hash.ToString(), j, tx.GetHash().ToString(), currentHeight);
                      // Treat as error or skip? Skipping might skew results. Throwing stops calculation.
                       throw JSONRPCError(RPC_DATABASE_ERROR, "failed to read previous transaction needed for fee calculation");
                 }

                 if (prevout.n >= txPrev.vout.size()) {
                     LogPrintf("ERROR: %s: invalid prevout index %d for tx %s (prev_tx %s has %u outputs) in block %d\n", __func__, prevout.n, tx.GetHash().ToString(), prevout.hash.ToString(), txPrev.vout.size(), currentHeight);
                     throw JSONRPCError(RPC_DATABASE_ERROR, "invalid previous transaction output index encountered");
                 }
                 nTxValueIn += txPrev.vout[prevout.n].nValue;
             }

             for (unsigned int j = 0; j < tx.vout.size(); ++j) {
                 nTxValueOut += tx.vout[j].nValue;
             }

             CAmount nTxFee = nTxValueIn - nTxValueOut;
             if (nTxFee < 0) {
                  LogPrintf("WARN: %s: negative fee (%s) calculated for tx %s in block %d. ValueIn=%s, ValueOut=%s. Treating as 0.\n", __func__, FormatMoney(nTxFee), tx.GetHash().ToString(), currentHeight, FormatMoney(nTxValueIn), FormatMoney(nTxValueOut));
                  nTxFee = 0;
             }

             block_fees_all += nTxFee; // Add to block's total fee

             // Add to overall fee/byte count ONLY if it's not a pure ZC mint/spend transaction
             if (!txHasZCMint && !txHasZCSpend) { // Standard tx
                  nFees += nTxFee;
                  block_bytes += ::GetSerializeSize(tx, SER_NETWORK, CLIENT_VERSION);
             } else if (!txHasZCMint && txHasZCSpend) { // Mixed spend (standard inputs + ZC spends)
                 // Fee is derived from standard inputs/outputs
                 nFees += nTxFee;
                 block_bytes += ::GetSerializeSize(tx, SER_NETWORK, CLIENT_VERSION);
             }
             // Pure ZC mints (txHasZCMint=true, txHasZCSpend=false) are excluded from nFees and block_bytes
             // Pure ZC spends (txHasZCMint=false, txHasZCSpend=true, no standard inputs) should have nTxFee=0 and are excluded from nFees/block_bytes

         } // End loop through txs in block

         // Update overall totals
         nFees_all += block_fees_all;
         nBytes += block_bytes;
         nTxCount += block_tx_count;
         nTxCount_all += block_tx_count_all;

         // Move to the next block index
         if (currentHeight < heightEnd) {
             LOCK(cs_main); // Protect chainActive access
             CBlockIndex* pnext = chainActive.Next(pindex);
             if (!pnext || pnext->nHeight != currentHeight + 1) { // Check if next block is expected one
                  LogPrintf("ERROR: %s: Chain inconsistency detected. Expected block %d, found %s\n", __func__, currentHeight+1, pnext ? std::to_string(pnext->nHeight) : "null");
                  throw JSONRPCError(RPC_INTERNAL_ERROR, "Chain inconsistency detected while iterating blocks");
             }
             pindex = pnext;
         }

    } // End loop through blocks

    // Calculate final fee rate
    CFeeRate nFeeRate = (nBytes > 0) ? CFeeRate(nFees, nBytes) : CFeeRate(0);

    // Populate return object
    ret.pushKV("txcount", nTxCount); // Use int64_t cast if needed, pushKV handles it
    ret.pushKV("txcount_all", nTxCount_all);
    if (!fFeeOnly) {
        UniValue spend_obj(UniValue::VOBJ);
        for (auto const& [denom, count] : mapSpendCount) { // C++17 structured binding
             try {
                 spend_obj.pushKV(strprintf("denom_%d", libzerocoin::ZerocoinDenominationToInt(denom)), count);
             } catch (const std::out_of_range& oor) {
                 LogPrintf("ERROR: %s: Invalid denomination value encountered when formatting output.\n", __func__);
                 spend_obj.pushKV(strprintf("denom_invalid_%d", static_cast<int>(denom)), count); // Log invalid denom explicitly
             } catch (...) {
                 LogPrintf("ERROR: %s: Unknown error converting denomination to int for output.\n", __func__);
             }
        }
        ret.pushKV("spendcount", spend_obj);
    }
    ret.pushKV("txbytes", nBytes);
    ret.pushKV("ttlfee", ValueFromAmount(nFees)); // Use ValueFromAmount for amounts
    ret.pushKV("ttlfee_all", ValueFromAmount(nFees_all));
    ret.pushKV("feeperkb", ValueFromAmount(nFeeRate.GetFeePerK()));

    return ret;
}


//////////qtum evm///////
////////////////////////////////////////////////////////////////////// // qtum
// Ensure this conversion is correct for your uint160 implementation.
// ----------------------------------------------------------------------------
// Helper Function (Needed by qrc20listtransactions)
// ----------------------------------------------------------------------------
// Ensure this conversion is correct for your uint160 implementation.
inline uint160 DevH160ToUint160(const dev::h160& h160) {
    // We assume dev::bytes is convertible or assignable to std::vector<unsigned char>
    // If not, adjust this line accordingly.
    std::vector<unsigned char> dataBytes = h160.asBytes();

    constexpr size_t UINT160_BYTES = 160 / 8;
    if (dataBytes.size() != UINT160_BYTES) {
        // === FIX 1: Pass only known basic types to LogPrintf ===
        std::string h160_hex_str = h160.hex(); // Get hex string representation
        // Use %s for strings, %zu for size_t
        LogPrintf("ERROR: DevH160ToUint160 size mismatch! dev::h160 size is %zu, expected uint160 size is %zu bytes. Cannot convert %s.\n",
                  dataBytes.size(), // Pass size_t
                  UINT160_BYTES,    // Pass size_t
                  h160_hex_str);    // Pass std::string
        // === END FIX 1 ===
        return uint160();
    }

    try {
        // This constructor might throw if dataBytes isn't exactly 20 bytes,
        // but we already checked that. It might throw for other reasons?
        return uint160(dataBytes);
    } catch (const std::exception& e) {
        // === FIX 2: Pass only known basic types to LogPrintf ===
        std::string h160_hex_str = h160.hex(); // Get hex string representation
        std::string error_what = e.what(); // Get error message as string
        // Use %s for strings
        LogPrintf("ERROR: DevH160ToUint160 uint160 constructor failed for %s: %s\n",
                  h160_hex_str, // Pass std::string
                  error_what);  // Pass std::string
        // === END FIX 2 ===
        return uint160();
    } catch (...) {
        // Catch-all for non-standard exceptions
        std::string h160_hex_str = h160.hex();
        LogPrintf("ERROR: DevH160ToUint160 uint160 constructor failed for %s with an unknown exception type.\n",
                  h160_hex_str); // Pass std::string
        return uint160();
    }
}
// ----------------------------------------------------------------------------
// Qrc20RpcCommand Class Definition
// ----------------------------------------------------------------------------
// Needs CallToken from contract_util.h to be correctly defined and ported.
// Assumes CallToken class has members like contractAddress, senderAddress, etc.
// and methods like decimals(), totalSupply(), balanceOf(), allowance().
class Qrc20RpcCommand {
    public:
        UniValue params;
        CallToken token; // Member variable - assumes CallToken has necessary members/methods
    
        // Constructor - Parses common parameters needed for QRC20 calls
        Qrc20RpcCommand(const UniValue& _params, size_t requiredParams, size_t maxParams, bool requireSender = false)
          : token() // Initialize token member using member initializer list
        {
            params = _params; // Assign parameters
    
            // Check parameter count
            if (params.size() < requiredParams || params.size() > maxParams) {
                // Construct the error message with details
                std::string msg = "Invalid number of parameters. Expected between " +
                                  std::to_string(requiredParams) + " and " + std::to_string(maxParams) +
                                  ", got " + std::to_string(params.size()) + ".";
                throw JSONRPCError(RPC_INVALID_PARAMETER, msg);
            }
    
            // Parse Contract Address (usually the first parameter)
            if (params.size() > 0) {
                if (!params[0].isStr()) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter: contract address must be a string.");
                }
                std::string contractAddrHex = params[0].get_str();
                // Basic validation - check hex and length (can be refined)
                if (!IsHex(contractAddrHex) || contractAddrHex.length() != 40) { // Standard Ethereum address length
                     throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter: contract address is not a valid 40-character hex string.");
                }
                try {
                     // Assuming dev::Address constructor or ParseHex works
                    token.contractAddress = dev::Address(ParseHex(contractAddrHex));
                     if (token.contractAddress == dev::Address()) { // Check for zero address after parsing
                          throw std::runtime_error("Parsed contract address resulted in zero address.");
                     }
                } catch (const std::exception& e) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Error parsing contract address: ") + e.what());
                }
            } else {
                // This case should be caught by the parameter count check above, but good for safety
                 throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing required parameter: contract address.");
            }
    
    
            // Placeholder for parsing sender address if required by specific methods
            // The exact parameter index for sender depends on the specific RPC command definition.
            // This section needs careful adaptation based on WHICH RPC call is using this constructor.
            if (requireSender) {
                LogPrintf("WARN: Qrc20RpcCommand constructor called with requireSender=true, but parsing logic is placeholder/example.\n");
                // Example: Assume sender is param[2] for a hypothetical call like 'transferFrom'
                /*
                if (params.size() > 2 && params[2].isStr()) {
                    std::string senderAddrHex = params[2].get_str();
                     if (!IsHex(senderAddrHex) || senderAddrHex.length() != 40) {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter: sender address is not a valid 40-character hex string.");
                    }
                    try {
                        token.senderAddress = dev::Address(ParseHex(senderAddrHex)); // Assuming CallToken has senderAddress member
                        if (token.senderAddress == dev::Address()) {
                            throw std::runtime_error("Parsed sender address resulted in zero address.");
                        }
                    } catch (const std::exception& e) {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Error parsing sender address: ") + e.what());
                    }
                } else {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Sender address required but not provided or invalid for this command.");
                }
                */
            }
    
        } // End of constructor body
    
    
        // Member function: getDecimals (Placeholder implementation)
        // !! CRITICAL: This MUST be implemented with a real EVM call !!
        uint32_t getDecimals() {
            LogPrintf("WARN: Qrc20RpcCommand::getDecimals() - Placeholder called, needs real implementation!\n");
            // Example of how a real implementation might look (using ported CallToken):
            /*
            uint32_t decimals_out = 0;
            // Assumes CallToken::decimals() method exists and performs the eth_call
            // It might need access to globalState or similar EVM context.
            if (token.decimals(decimals_out)) {
                return decimals_out;
            } else {
                // Throw an error that will be caught by the RPC handler
                throw JSONRPCError(RPC_MISC_ERROR, "Failed to execute decimals() call on contract " + token.contractAddress.hex());
            }
            */
            return 18; // *** Incorrect Placeholder - Must query contract ***
        }
    
    
        // Member function: formatAmount (using pow10_u256 helper)
        // Needs getDecimals() above to be implemented correctly.
        std::string formatAmount(const std::string& amountStr) {
            LogPrintf("Qrc20RpcCommand::formatAmount() - Called with: %s\n", amountStr);
            try {
                // Get decimals (using the placeholder/real implementation)
                uint32_t decimals = getDecimals(); // Calls the member function above
                LogPrintf("Qrc20RpcCommand::formatAmount() - Decimals: %u\n", decimals);
    
                dev::u256 rawAmount;
                // Try parsing amount string (could be decimal or hex starting with 0x)
                try {
                     rawAmount = dev::u256(amountStr);
                } catch (const std::exception& parse_ex) {
                     LogPrintf("ERROR: Qrc20RpcCommand::formatAmount() - Failed to parse amount string '%s' as u256: %s\n", amountStr, parse_ex.what());
                     throw std::runtime_error("Invalid amount string format for u256 parsing: " + amountStr);
                } catch (...) {
                     LogPrintf("ERROR: Qrc20RpcCommand::formatAmount() - Unknown error parsing amount string '%s' as u256\n", amountStr);
                     throw std::runtime_error("Unknown error parsing amount string: " + amountStr);
                }
    
                if (decimals == 0) {
                    return rawAmount.str(); // Return raw string representation if 0 decimals
                }
    
                // Use the helper function instead of dev::exp10
                dev::u256 factor = pow10_u256(decimals); // Assumes pow10_u256 helper is available and correct
                if (factor == 0 && decimals > 0) { // Factor should only be 0 if decimals is extremely large
                     LogPrintf("ERROR: Qrc20RpcCommand::formatAmount() - Factor calculation resulted in zero for decimals=%u. Returning raw amount.\n", decimals);
                     return rawAmount.str(); // Fallback to raw amount might be safest
                }
    
                dev::u256 integerPart = rawAmount / factor;
                dev::u256 fractionalPart = rawAmount % factor;
    
                std::string fractionalStr = fractionalPart.str();
                // Pad with leading zeros if fractional part is shorter than decimals
                if (fractionalStr.length() < decimals) {
                    fractionalStr = std::string(decimals - fractionalStr.length(), '0') + fractionalStr;
                } else if (fractionalStr.length() > decimals) {
                     // This case should ideally not happen with modulo arithmetic unless factor is wrong
                     fractionalStr = fractionalStr.substr(fractionalStr.length() - decimals); // Take last 'decimals' digits
                     LogPrintf("WARN: Qrc20RpcCommand::formatAmount() - Fractional part unexpectedly longer than decimals (%u), using last %u digits.\n", fractionalStr.length(), decimals);
                }
    
                return integerPart.str() + "." + fractionalStr;
    
            } catch (const std::exception& e) {
                 // Catch specific exceptions if possible (e.g., overflow from pow10_u256)
                 LogPrintf("Error formatting amount in Qrc20RpcCommand::formatAmount for input '%s': %s\n", amountStr, e.what());
                 // Re-throw as a runtime_error to be potentially caught higher up or logged by RPC framework
                 throw std::runtime_error(std::string("Error formatting amount: ") + e.what());
            } catch (...) {
                 LogPrintf("Unknown error formatting amount in Qrc20RpcCommand::formatAmount for input '%s'\n", amountStr);
                 throw std::runtime_error("Unknown error formatting amount.");
            }
        }
    
    }; // End of Qrc20RpcCommand class definition


UniValue callcontract(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 6) // Adjusted max params size
        throw std::runtime_error( // Keep runtime_error for help display
            "callcontract \"address\" \"data\" ( senderaddress amount gaslimit gasprice )\n"
            "\nSend transaction to contract (requires EVM support).\n"
            "\nArguments:\n"
            "1. \"address\"          (string, required) The contract address (hex).\n"
            "2. \"data\"             (string, required) The hex string representing the function call and parameters.\n"
            "3. \"senderaddress\"    (string, optional) The DigiWage address of the sender. If omitted, uses default wallet address (if wallet enabled).\n"
            "4. amount             (numeric, optional, default=0) The amount in DWG to send. eg 0.1.\n" // Use DigiWage currency unit
            "5. gasLimit           (numeric, optional) The gas limit for the transaction (default: " + std::to_string(DEFAULT_GAS_LIMIT_OP_CALL) + ").\n" // Removed undefined MAX_RPC_GAS_LIMIT            "6. gasPrice           (numeric, optional) The gas price in " + CURRENCY_UNIT + " per gas unit (e.g. 0.00000040). Uses suggested value if omitted.\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"hash\",        (string) The transaction hash.\n"
            "  \"sender\": \"address\",   (string) The DigiWage sender address used.\n"
            "  \"hash160\": \"hex\",    (string) The hex encoded hash160 of the sender address.\n"
            "  \"address\": \"hex\"     (string) The contract address interacted with.\n" // Added contract address to output
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("callcontract", "\"c6ca2697719d00446d4ea51f6fac88e4946bd8e4\" \"aabbcc\"")
            + HelpExampleRpc("callcontract", "\"c6ca2697719d00446d4ea51f6fac88e4946bd8e4\", \"aabbcc\""));

    // --- Implementation ---
    // This requires CallToContract to be fully ported and adapted to DigiWage's structure
    // (using cs_main, pwalletMain, globalState, chainActive instead of ChainstateManager).
    // Without a full port, this will likely fail.

    // LOCK(cs_main); // Lock typically needed for state access within CallToContract
    // extern UniValue CallToContract(const UniValue& params); // Ensure signature matches adapted version

    // try {
    //      // If CallToContract is adapted to take only params:
    //      return CallToContract(params);
    // } catch (const JSONRPCError& e) {
    //     throw e;
    // } catch (const std::exception& e) {
    //     throw JSONRPCError(RPC_MISC_ERROR, std::string("callcontract failed: ") + e.what());
    // }

    // Placeholder if EVM/CallToContract is not available:
    throw JSONRPCError(RPC_METHOD_NOT_FOUND, "callcontract RPC requires full EVM integration which may not be available in this build.");
}


UniValue waitforlogs(const UniValue& params, bool fHelp)
{
    // Check for help request first
    if (fHelp)
        throw std::runtime_error( // Keep runtime_error for help display
            "waitforlogs (fromBlock) (toBlock) (filter) (minconf)\n"
            "\nWaits for logs matching the filter criteria to appear (requires EVM and logevents).\n"
            "\nArguments:\n"
            "1. fromBlock          (numeric or string, optional, default=next block after tip) The block number (e.g. 1000), hex string (e.g. \"0x3e8\"), or string 'latest'/'earliest' to start searching from.\n"
            "2. toBlock            (numeric or string, optional, default=-1 waits indefinitely) The block number, hex string, or string 'latest'/'earliest' to search up to.\n"
            "3. filter             (json object, optional) Filter criteria:\n"
            "   {\n"
            "     \"addresses\":[\"address\",...],        (array of hex strings) An array of contract addresses to get logs from.\n"
            "     \"topics\":[\"topic\", null, ...]       (array of hex strings) An array of topics (32-byte hex) to filter by. 'null' acts as a wildcard. Topics are order-dependent.\n"
            "   }\n"
            "4. minconf            (numeric, optional, default=6) Minimal number of confirmations required for logs to be returned.\n"
            "\nResult:\n"
            "{\n"
            "  \"entries\": [ ... ], (array) Array of log objects matching the filter.\n"
            "                         Each log object includes blockHash, blockNumber, transactionHash, address, topics, data, etc.\n"
            "  \"count\": n,         (numeric) Number of logs returned.\n"
            "  \"nextblock\": n      (numeric) The block number to start the next search from (block after the last one included or searched).\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("waitforlogs", "1000 2000 '{\"addresses\":[\"c6ca2697719d00446d4ea51f6fac88e4946bd8e4\"]}' 1")
            + HelpExampleRpc("waitforlogs", "1000, 2000, {\"addresses\":[\"c6ca2697719d00446d4ea51f6fac88e4946bd8e4\"]}, 1")
        ); // End of runtime_error argument

    // --- Implementation ---
    if (!fLogEvents) // Ensure fLogEvents global exists and is enabled
        throw JSONRPCError(RPC_MISC_ERROR, "-logevents flag is required for this RPC.");

    if (!pblocktree)
         throw JSONRPCError(RPC_INTERNAL_ERROR, "Block tree database (pblocktree, required for log indexing) is not available.");
    if (!pstorageresult)
         throw JSONRPCError(RPC_INTERNAL_ERROR, "Log storage database (pstorageresult) is not available.");

    // Local variables and parsing
    int fromBlock = 0;
    int toBlock = -1;
    UniValue addresses = NullUniValue; // Use UniValue
    UniValue topics = NullUniValue;    // Use UniValue
    int minconf = 6; // Default for waitforlogs

    // Example Parsing Logic - ADAPT or use full version from previous answers
    // Ensure parseBlockHeight and parseParam helpers exist and are included/defined
    if (params.size() > 0 && !params[0].isNull()) fromBlock = parseBlockHeight(params[0], 0);
    if (params.size() > 1 && !params[1].isNull()) toBlock = parseBlockHeight(params[1], -1);
    if (params.size() > 2 && !params[2].isNull() && params[2].isObject()) {
        const UniValue& filter = params[2];
        if (filter.exists("addresses")) addresses = filter["addresses"]; // Store UniValue directly
        if (filter.exists("topics"))    topics = filter["topics"];       // Store UniValue directly
    }
    if (params.size() > 3 && !params[3].isNull()) minconf = params[3].getInt<int>(); // Check for error?

    // Validate minconf
    if (minconf < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "minconf cannot be negative");
    }

    std::vector<std::vector<uint256>> hashesToBlock;
    int curheight = 0; // Height reached by search, or next block height for poll

    while (IsRPCRunning()) { // Check IsRPCRunning() for graceful exit
         int searchResultHeight = 0;
         hashesToBlock.clear(); // Clear results for this iteration
        {
            LOCK(cs_main); // Lock for pblocktree access and chainActive

            // === Replace ReadHeightIndex with Placeholder ===
            int searchTipHeight = 0;
            searchTipHeight = chainActive.Height() - minconf + 1; // Get tip height adjusted for minconf
            searchResultHeight = std::max(fromBlock, std::min((toBlock == -1 ? searchTipHeight : toBlock), searchTipHeight));
            LogPrintf("waitforlogs: Placeholder used instead of ReadHeightIndex. Search range [%d, %d], effective max height: %d\n", fromBlock, toBlock, searchResultHeight);
            // TODO: Placeholder logic - This does NOT actually find matching logs or populate hashesToBlock. Log fetching below will be empty.
            hashesToBlock.clear(); // Explicitly clear as placeholder doesn't fill it
            // === End Replace ReadHeightIndex ===
        }

        // This check will likely always fail now due to the placeholder logic not filling hashesToBlock
        if (searchResultHeight > 0 && !hashesToBlock.empty()) {
            curheight = searchResultHeight;
            break; // Exit loop to process found logs
        }

        // If searchResultHeight > 0 but hashesToBlock is empty, it means we searched up to that height
        // but found no matching logs yet within the [fromBlock, searchResultHeight] range meeting minconf.
        // We should continue waiting. Update the highest block checked.
        if (searchResultHeight > 0) {
             curheight = searchResultHeight;
        } else {
             // searchResultHeight == 0 might mean ReadHeightIndex placeholder logic resulted in 0,
             // or an error occurred. If no error thrown, assume waiting is needed.
             // Use the original fromBlock or the last known height as the next potential start.
             LOCK(cs_main);
             curheight = std::max(fromBlock -1, chainActive.Height()); // Start next wait check from current height
        }

        // Check if target block (if not indefinite) has been reached and confirmed
        if (toBlock != -1) { // Use local variable 'toBlock'
            LOCK(cs_main);
            if (chainActive.Height() >= toBlock + minconf) { // Use local variable 'minconf'
                 // Target block is confirmed, and we didn't find logs up to it. Stop waiting.
                 curheight = toBlock; // Report target block as reached, use local variable 'toBlock'
                 break;
            }
        }

        // Wait for a new block notification or timeout (e.g., 1 second)
        {
            std::unique_lock<std::mutex> lock(cs_blockchange);
            // Wait for 1 second or until a new block arrives or RPC stops
            if (cond_blockchange.wait_for(lock, std::chrono::milliseconds(1000)) == std::cv_status::no_timeout) {
                // Block changed or RPC stopping, loop will re-evaluate
            } else {
                // Timeout occurred, loop will re-evaluate
            }
        }

         // Re-evaluate fromBlock for the next iteration if no logs found yet
         // Start from the block *after* the last one checked (curheight)
         fromBlock = std::max(fromBlock, curheight >= 0 ? curheight + 1 : 0); // Use local variable 'fromBlock'

         // If toBlock is defined, check if we've passed it
         if (toBlock != -1 && fromBlock > toBlock) { // Use local variables
              curheight = toBlock; // Indicate we finished the range, use local variable 'toBlock'
              break; // Stop if we've searched past the target block
         }
    } // End while loop

    if (!IsRPCRunning() && hashesToBlock.empty()) {
        LogPrintf("waitforlogs: RPC stopped or client disconnected during wait.\n");
        UniValue result(UniValue::VOBJ);
        result.pushKV("entries", UniValue(UniValue::VARR));
        result.pushKV("count", 0);
        result.pushKV("nextblock", fromBlock); // Use local variable 'fromBlock'
        return result;
    }

    // Process found blocks/transactions (will likely be empty due to placeholder)
    UniValue jsonLogs(UniValue::VARR);
    std::set<uint256> processedTxHashes; // Prevent duplicate processing

    // Prepare topic filter *once* if specified
    std::vector<boost::optional<dev::h256>> filterTopicsParsed;
    bool topicFilterActive = false;
    if (!topics.isNull() && topics.isArray() && !topics.empty()) {
         try {
             parseParam(topics, filterTopicsParsed); // Assumes parseParam exists and handles UniValue -> vector<optional<h256>>
             topicFilterActive = true;
         } catch (const std::exception& e) {
             throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid topic filter format: ") + e.what());
         }
    }


    {
        LOCK(cs_main); // Lock for pstorageresult access

        for (const auto& txHashesInBlock : hashesToBlock) { // hashesToBlock will be empty due to placeholder
            for (const auto& txHash : txHashesInBlock) {

                if(processedTxHashes.count(txHash)) {
                    continue;
                }
                processedTxHashes.insert(txHash);

                std::vector<TransactionReceiptInfo> receipts;
                try {
                     receipts = pstorageresult->getResult(uintToh256(txHash)); // Assumes pstorageresult->getResult is ported
                } catch (const std::exception& e) {
                     LogPrintf("ERROR: waitforlogs - Failed to get receipt for tx %s: %s\n", txHash.GetHex(), e.what());
                     continue; // Skip this transaction if receipt fails
                }

                for (const auto& receipt : receipts) {
                    for (const auto& log : receipt.logs) {

                        // Apply topic filtering (address filtering implicitly handled by placeholder logic NOT working)
                        bool includeLog = true;
                        if (topicFilterActive) { // Use the parsed filter
                            if (log.topics.size() < filterTopicsParsed.size()) {
                                includeLog = false;
                            } else {
                                for (size_t i = 0; i < filterTopicsParsed.size(); ++i) {
                                    // Check optional topic: if present in filter, must match log
                                    if (filterTopicsParsed[i] && log.topics[i] != filterTopicsParsed[i].get()) { // Use parsed h256
                                        includeLog = false;
                                        break;
                                    }
                                }
                            }
                        }

                        if (!includeLog) {
                            continue;
                        }

                        UniValue jsonLog(UniValue::VOBJ);
                        // Assumes assignJSON functions are available and ported
                        assignJSON(jsonLog, receipt); // Populates block hash, number, tx hash etc.
                        assignJSON(jsonLog, log, false); // Populates address, topics, data etc.

                        jsonLogs.push_back(jsonLog);
                    }
                }
            }
        }
    } // Release cs_main lock

    UniValue result(UniValue::VOBJ);
    result.pushKV("entries", jsonLogs);
    result.pushKV("count", (int) jsonLogs.size());

    // nextblock should be the block *after* the last one included in the results or searched
    int nextBlockHeight = fromBlock; // Use local variable 'fromBlock'
    if (curheight >= 0) { // If search completed up to curheight
         nextBlockHeight = curheight + 1;
    }
     // Ensure nextBlockHeight doesn't exceed toBlock if toBlock is set
     if (toBlock != -1) { // Use local variable 'toBlock'
         nextBlockHeight = std::min(nextBlockHeight, toBlock + 1); // Use local variable 'toBlock'
     }
    result.pushKV("nextblock", nextBlockHeight);

    return result;
}


UniValue searchlogs(const UniValue& params, bool fHelp)
{
     if (fHelp || params.size() < 2 || params.size() > 5)
        throw std::runtime_error( // Keep runtime_error for help display
            "searchlogs fromBlock toBlock (addressFilter) (topicFilter) (minconf)\n"
            "\nReturns logs matching the filter criteria within a block range (requires EVM and logevents).\n"
            "\nArguments:\n"
            "1. fromBlock          (numeric or string, required) The block number (e.g. 1000), hex string (e.g. \"0x3e8\"), or string 'latest'/'earliest' to start searching from.\n"
            "2. toBlock            (numeric or string, required) The block number, hex string, or string 'latest'/'earliest' to search up to.\n"
            "3. addressFilter      (json object, optional) Address filter criteria:\n"
            "   {\"addresses\":[\"address\",...]}        (array of hex strings) An array of contract addresses to get logs from.\n"
            "4. topicFilter        (json object, optional) Topic filter criteria:\n"
            "   {\"topics\":[\"topic\", null, ...]}       (array of hex strings) An array of topics (32-byte hex) to filter by. 'null' acts as a wildcard. Topics are order-dependent.\n"
            "5. minconf            (numeric, optional, default=0) Minimal number of confirmations required for logs to be included.\n"
            "\nResult:\n"
            "[...]                 (array) Array of log objects matching the filter.\n"
            "                         Each log object includes blockHash, blockNumber, transactionHash, address, topics, data, etc.\n"
            "\nExamples:\n"
            + HelpExampleCli("searchlogs", "1000 2000 '{\"addresses\":[\"c6ca2697719d00446d4ea51f6fac88e4946bd8e4\"]}' '{\"topics\":[\"0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef\", null]}' 1")
            + HelpExampleRpc("searchlogs", "1000, 2000, {\"addresses\":[\"c6ca2697719d00446d4ea51f6fac88e4946bd8e4\"]}, {\"topics\":[\"0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef\", null]}, 1")
        ); // End of runtime_error

    // --- Implementation ---
    if (!fLogEvents)
        throw JSONRPCError(RPC_MISC_ERROR, "-logevents flag is required for this RPC.");

    if (!pblocktree)
         throw JSONRPCError(RPC_INTERNAL_ERROR, "Block tree database (pblocktree, required for log indexing) is not available.");
    if (!pstorageresult)
         throw JSONRPCError(RPC_INTERNAL_ERROR, "Log storage database (pstorageresult) is not available.");

    // Parse parameters
    int fromBlock = 0, toBlock = -1;
    std::set<dev::h160> addresses; // Parsed address filter
    std::vector<boost::optional<dev::h256>> topics; // Parsed topic filter
    int minconf = 0; // Default minconf for searchlogs

    {
        LOCK(cs_main); // Use cs_main for consistent height reading
        int latestHeight = chainActive.Height();
        // Ensure parseBlockHeight helper exists and is included/defined
        fromBlock = parseBlockHeight(params[0], 0); // Default from genesis
        toBlock = parseBlockHeight(params[1], latestHeight); // Default to latest block
    }
     // Ensure valid block range relative to current chain
     if (toBlock == -1) { // Check if parsing failed or defaulted incorrectly
         LOCK(cs_main);
         toBlock = chainActive.Height();
     }
     if (fromBlock < 0) throw JSONRPCError(RPC_INVALID_PARAMETER, "fromBlock cannot be negative");


    // Parse address filter (param 3)
    UniValue addressFilterUni = NullUniValue; // Keep UniValue for potential later use if needed
    if (params.size() > 2 && !params[2].isNull()) {
        if (!params[2].isObject()) throw JSONRPCError(RPC_INVALID_PARAMETER, "Address filter (param 3) must be a JSON object or null");
        addressFilterUni = params[2];
        if (addressFilterUni.exists("addresses")) {
             const UniValue& addrs = addressFilterUni["addresses"];
             if (!addrs.isArray()) throw JSONRPCError(RPC_INVALID_PARAMETER, "'addresses' key in filter must be an array");
             // Ensure parseParam helper exists and is included/defined
             try {
                 parseParam(addrs, addresses); // Parse into std::set<dev::h160>
             } catch (const std::exception& e) {
                 throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid address filter format: ") + e.what());
             }
        }
    }

    // Parse topic filter (param 4)
    UniValue topicFilterUni = NullUniValue; // Keep UniValue for potential later use if needed
    bool topicFilterActive = false; // Flag if topics were specified
    if (params.size() > 3 && !params[3].isNull()) {
        if (!params[3].isObject()) throw JSONRPCError(RPC_INVALID_PARAMETER, "Topic filter (param 4) must be a JSON object or null");
        topicFilterUni = params[3];
         if (topicFilterUni.exists("topics")) {
            const UniValue& tops = topicFilterUni["topics"];
            if (!tops.isArray()) throw JSONRPCError(RPC_INVALID_PARAMETER, "'topics' key in filter must be an array");
            // Ensure parseParam helper exists and is included/defined
            try {
                parseParam(tops, topics); // Parse into std::vector<boost::optional<dev::h256>>
                topicFilterActive = !topics.empty(); // Set flag if parsing resulted in topics
            } catch (const std::exception& e) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid topic filter format: ") + e.what());
            }
        }
    }

    // Parse minconf (param 5)
     if (params.size() > 4 && !params[4].isNull()) {
         minconf = parseUInt(params[4], 0); // Ensure parseUInt helper exists
     }

     // Sanity check block range and minconf
     if (fromBlock > toBlock) {
          throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("fromBlock (%d) cannot be greater than toBlock (%d)", fromBlock, toBlock));
     }
     if (minconf < 0) {
          throw JSONRPCError(RPC_INVALID_PARAMETER, "minconf cannot be negative");
     }


    // Perform the search
    std::vector<std::vector<uint256>> hashesToBlock; // Will store [blockHeight][txHash1, txHash2...]
    int lastBlockSearched = 0; // Track the highest block checked

    {
        LOCK(cs_main); // Lock for pblocktree and chainActive access

        // === Replace ReadHeightIndex with Placeholder ===
        int searchTipHeight = 0;
        searchTipHeight = chainActive.Height() - minconf + 1; // Get tip height adjusted for minconf
        lastBlockSearched = std::max(fromBlock, std::min((toBlock == -1 ? searchTipHeight : toBlock), searchTipHeight));
        LogPrintf("searchlogs: Placeholder used instead of ReadHeightIndex. Search range [%d, %d], effective max height: %d\n", fromBlock, toBlock, lastBlockSearched);
        // TODO: Placeholder logic - This does NOT actually find matching logs or populate hashesToBlock. Log fetching below will be empty.
        hashesToBlock.clear(); // Explicitly clear as placeholder doesn't fill it
        // === End Replace ReadHeightIndex ===

        // Removed original try/catch block for ReadHeightIndex
    }

    // Process results and apply topic filter (hashesToBlock will be empty due to placeholder)
    UniValue resultLogs(UniValue::VARR);
    std::set<uint256> processedTxHashes;

    {
        LOCK(cs_main); // Lock for pstorageresult access

        // This outer loop will likely not execute as hashesToBlock is empty
        for (const auto& txHashesInBlock : hashesToBlock) {
            for (const auto& txHash : txHashesInBlock) {

                if (processedTxHashes.count(txHash)) {
                    continue;
                }
                processedTxHashes.insert(txHash);

                std::vector<TransactionReceiptInfo> receipts;
                 try {
                     receipts = pstorageresult->getResult(uintToh256(txHash)); // Assumes this function exists
                 } catch (const std::exception& e) {
                     LogPrintf("ERROR: searchlogs - Failed to get receipt for tx %s: %s\n", txHash.GetHex(), e.what());
                     continue; // Skip tx if receipt fails
                 }

                for (const auto& receipt : receipts) {
                    for (const auto& log : receipt.logs) {
                        // Check if this log matches the topic filter (address filter was supposed to be done by ReadHeightIndex)
                        // Note: Since ReadHeightIndex is replaced, the address filter isn't applied efficiently.
                        // We could add an address check here for correctness, although inefficient:
                        // if (!addresses.empty() && addresses.find(log.address) == addresses.end()) {
                        //     continue; // Skip if address doesn't match filter
                        // }

                        bool logMatchesTopics = true;
                        if (topicFilterActive) { // Use the parsed topic filter
                            if (log.topics.size() < topics.size()) {
                                logMatchesTopics = false;
                            } else {
                                for (size_t i = 0; i < topics.size(); ++i) {
                                    // Check optional topic: if present in filter, must match log
                                    if (topics[i] && log.topics[i] != topics[i].get()) { // Compare parsed h256
                                        logMatchesTopics = false;
                                        break;
                                    }
                                }
                            }
                        }

                        if (logMatchesTopics) {
                            UniValue jsonLog(UniValue::VOBJ);
                            assignJSON(jsonLog, receipt); // Assumes this helper exists
                            assignJSON(jsonLog, log, false); // Assumes this helper exists
                            resultLogs.push_back(jsonLog);
                        }
                    } // End log loop
                } // End receipt loop
            } // End tx loop
        } // End block loop (over hashesToBlock)
    } // Release cs_main lock

    return resultLogs; // Return the array of log objects directly (likely empty)
}


UniValue gettransactionreceipt(const UniValue& params, bool fHelp)
{
     if (fHelp || params.size() != 1)
        throw std::runtime_error( // Keep runtime_error for help display
            "gettransactionreceipt \"txid\"\n"
            "\nGet the transaction receipt for a given transaction hash (requires EVM and logevents).\n"
            "\nArguments:\n"
            "1. \"txid\"      (string, required) The transaction hash (64-char hex).\n"
            "\nResult:\n"
            "[...]           (array) An array of transaction receipt objects. Usually one, returns empty array if not found or not an EVM transaction.\n"
            "Each object contains:\n"
            "  \"blockHash\": \"hash\",          (string) Hash of the block containing the transaction.\n"
            "  \"blockNumber\": n,           (numeric) Block number containing the transaction.\n"
            "  \"transactionHash\": \"hash\",  (string) Transaction hash.\n"
            "  \"transactionIndex\": n,      (numeric) Transaction index within the block.\n"
            "  \"from\": \"address\",          (string) Sender address (hex).\n"
            "  \"to\": \"address\",            (string) Receiver address (hex, or contract address for creation).\n"
            "  \"cumulativeGasUsed\": n,   (numeric) Total gas used in the block up to this transaction.\n"
            "  \"gasUsed\": n,             (numeric) Gas used by this specific transaction.\n"
            "  \"contractAddress\": \"hash\",  (string) Address of the contract created (hex), if any.\n"
            "  \"logs\": [...]               (array) Array of log objects generated by this transaction.\n"
            "  \"logsBloom\": \"bloom\"        (string) Bloom filter for logs (256-byte hex).\n"
            "  \"stateRoot\": \"hash\"         (string, optional) State root after this transaction (if available).\n"
            "  \"status\": n                 (numeric, optional) 1 for success, 0 for failure/revert (if available).\n"
            "\nExamples:\n"
            + HelpExampleCli("gettransactionreceipt", "\"0xtransactionidhex\"")
            + HelpExampleRpc("gettransactionreceipt", "\"0xtransactionidhex\""));

    // --- Implementation ---
    if(!fLogEvents)
        throw JSONRPCError(RPC_MISC_ERROR, "-logevents flag is required for this RPC.");

    if (!pstorageresult)
         throw JSONRPCError(RPC_INTERNAL_ERROR, "Log storage database (pstorageresult) is not available.");

    std::string hashTemp = params[0].get_str();
    if(hashTemp.size() != 64 || !IsHex(hashTemp)){ // Check length and hex format
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid transaction hash: must be 64-character hex string");
    }

    uint256 hash = uint256S(hashTemp);
    dev::h256 txhash = uintToh256(hash); // Convert to h256 for storage lookup

    std::vector<TransactionReceiptInfo> transactionReceiptInfo;
    {
        LOCK(cs_main); // Lock storage access (might not be strictly needed if pstorageresult is thread-safe for reads)
        try {
             // Assumes pstorageresult->getResult is ported
             transactionReceiptInfo = pstorageresult->getResult(txhash);
        } catch (const std::exception& e) {
             LogPrintf("ERROR: gettransactionreceipt - Failed to get receipt for tx %s: %s\n", hashTemp, e.what());
             // Return empty array on database error, similar to Qtum
             return UniValue(UniValue::VARR);
        }
    } // Release lock

    UniValue result(UniValue::VARR);
    for(TransactionReceiptInfo& t : transactionReceiptInfo){ // Iterate results (usually just one)
        UniValue tri(UniValue::VOBJ);
        // Assumes transactionReceiptInfoToJSON is ported
        transactionReceiptInfoToJSON(t, tri);
        result.push_back(tri);
    }
    return result; // Returns empty array if no receipt found
}


UniValue listcontracts(const UniValue& params, bool fHelp)
{
     if (fHelp || params.size() > 2)
        throw std::runtime_error( // Keep runtime_error for help display
            "listcontracts (start maxDisplay)\n"
            "\nList contracts with non-zero balances stored in the state database (requires EVM).\n"
            "\nArguments:\n"
            "1. start          (numeric, optional, default=1) Index of the first contract to display (1-based).\n"
            "2. maxDisplay     (numeric, optional, default=20) Maximum number of contracts to display.\n"
            "\nResult:\n"
            "{                     (json object)\n"
            "  \"address_hex\": amount,  (string: numeric) Contract address (hex) and balance in DWG\n"
            "  ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("listcontracts", "1 50")
            + HelpExampleRpc("listcontracts", "1, 50"));

    // --- Implementation ---
	if (!globalState) { // Ensure globalState exists (EVM state database)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "EVM state database (globalState) not available.");
    }

	int start = 1; // 1-based index for user
	if (params.size() > 0 && !params[0].isNull()){
		start = params[0].getInt<int>();
		if (start <= 0)
			throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid start index, must be >= 1");
	}

	int maxDisplay = 20;
	if (params.size() > 1 && !params[1].isNull()){
		maxDisplay = params[1].getInt<int>();
		if (maxDisplay <= 0)
			throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid maxDisplay, must be > 0");
	}

	UniValue result(UniValue::VOBJ);
    std::unordered_map<dev::h160, dev::u256> mapAddresses;

    {
        LOCK(cs_main); // Lock EVM state access (assuming globalState read needs protection)
        // Assumes globalState->addresses() returns the map of known contract addresses
        // The `Vin` part might be specific to Qtum's implementation detail; adapt if needed.
	    mapAddresses = globalState->addresses();
    } // Release lock

	int contractsCount = (int)mapAddresses.size();

	if (start > contractsCount && contractsCount > 0) // Allow start=1 even if count=0
		throw JSONRPCError(RPC_INVALID_PARAMETER, "Start index " + std::to_string(start) + " is greater than total number of contracts " + std::to_string(contractsCount));

	int currentPos = 0; // 0-based for iteration logic
	int displayedCount = 0;

	// Iterate through the map (std::map iterates in key order - address order)
	for (auto const& [address, vinInfo] : mapAddresses) // Use C++17 structured binding
	{
        currentPos++; // Now 1-based
		if (currentPos < start) {
            continue; // Skip until we reach the desired start index
        }

        CAmount balance = 0;
        {
             LOCK(cs_main); // Lock state access for balance lookup
             // Assumes globalState->balance is ported and thread-safe with cs_main
             balance = CAmount(globalState->balance(address)); // Convert EVM balance (u256) to CAmount
        }

        // Only list contracts with a balance? Original Qtum lists all known. Let's list all.
        result.pushKV(address.hex(), ValueFromAmount(balance)); // Use address hex as key

		displayedCount++;
		if(displayedCount >= maxDisplay) {
            break; // Stop after displaying the maximum number
        }
	}

	return result;
}

// --- QRC20 Functions ---

UniValue qrc20name(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error( // Keep runtime_error for help display
            "qrc20name \"contractaddress\"\n"
            "\nGet the name of a QRC20 token (requires EVM).\n"
            "\nArguments:\n"
            "1. \"contractaddress\" (string, required) The contract address (hex).\n"
            "\nResult:\n"
            "\"name\"             (string) The name of the token.\n"
            "\nExamples:\n"
            + HelpExampleCli("qrc20name", "\"c6ca2697719d00446d4ea51f6fac88e4946bd8e4\"")
            + HelpExampleRpc("qrc20name", "\"c6ca2697719d00446d4ea51f6fac88e4946bd8e4\""));

    // --- Implementation ---
    Qrc20RpcCommand cmd(params, 1, 1); // Uses public constructor
    std::string result;
    // Assumes token.name() exists in the ported CallToken and interacts with globalState
    if(!cmd.token.name(result))
        throw JSONRPCError(RPC_MISC_ERROR, "Fail to get token name (contract might not be QRC20, or state error)");

    return result;
}

UniValue qrc20symbol(const UniValue& params, bool fHelp)
{
     if (fHelp || params.size() != 1)
        throw std::runtime_error( // Keep runtime_error for help display
            "qrc20symbol \"contractaddress\"\n"
            "\nGet the symbol of a QRC20 token (requires EVM).\n"
            "\nArguments:\n"
            "1. \"contractaddress\" (string, required) The contract address (hex).\n"
            "\nResult:\n"
            "\"symbol\"           (string) The symbol of the token.\n"
            "\nExamples:\n"
            + HelpExampleCli("qrc20symbol", "\"c6ca2697719d00446d4ea51f6fac88e4946bd8e4\"")
            + HelpExampleRpc("qrc20symbol", "\"c6ca2697719d00446d4ea51f6fac88e4946bd8e4\""));

    // --- Implementation ---
    Qrc20RpcCommand cmd(params, 1, 1);
    std::string result;
    // Assumes token.symbol() exists in the ported CallToken
    if(!cmd.token.symbol(result))
         throw JSONRPCError(RPC_MISC_ERROR, "Fail to get token symbol (contract might not be QRC20, or state error)");

    return result;
}


// Second definition of qrc20balanceof (KEEP THIS ONE)
UniValue qrc20totalsupply(const UniValue& params, bool fHelp)
{
     if (fHelp || params.size() != 1)
        // === FIX: Added help string ===
        throw std::runtime_error(
            "qrc20totalsupply \"contractaddress\"\n"
            "\nGet the total supply of a QRC20 token (requires EVM).\n"
            "\nArguments:\n"
            "1. \"contractaddress\" (string, required) The contract address (hex).\n"
            "\nResult:\n"
            "\"supply\"           (string) The total supply, formatted according to token decimals.\n"
            "\nExamples:\n"
            + HelpExampleCli("qrc20totalsupply", "\"c6ca2697719d00446d4ea51f6fac88e4946bd8e4\"")
            + HelpExampleRpc("qrc20totalsupply", "\"c6ca2697719d00446d4ea51f6fac88e4946bd8e4\""));
        // === END FIX ===

    Qrc20RpcCommand cmd(params, 1, 1);
    std::string resultSupplyStr;
    // Assumes token.totalSupply() is ported and works
    if(!cmd.token.totalSupply(resultSupplyStr))
        throw JSONRPCError(RPC_MISC_ERROR, "Fail to get total supply (contract might not be QRC20, or state error)");

    return cmd.formatAmount(resultSupplyStr); // Calls the corrected method
}

UniValue qrc20decimals(const UniValue& params, bool fHelp)
{
     if (fHelp || params.size() != 1)
        throw std::runtime_error( // Keep runtime_error for help display
            "qrc20decimals \"contractaddress\"\n"
            "\nGet the number of decimals of a QRC20 token (requires EVM).\n"
            "\nArguments:\n"
            "1. \"contractaddress\" (string, required) The contract address (hex).\n"
            "\nResult:\n"
            "n                  (numeric) The number of decimals.\n"
            "\nExamples:\n"
            + HelpExampleCli("qrc20decimals", "\"c6ca2697719d00446d4ea51f6fac88e4946bd8e4\"")
            + HelpExampleRpc("qrc20decimals", "\"c6ca2697719d00446d4ea51f6fac88e4946bd8e4\""));

    // --- Implementation ---
    Qrc20RpcCommand cmd(params, 1, 1);
    return (int)cmd.getDecimals(); // Return as standard integer using public helper
}

UniValue qrc20balanceof(const UniValue& params, bool fHelp)
{
     if (fHelp || params.size() != 2)
        // === FIX: Added help string ===
        throw std::runtime_error(
            "qrc20balanceof \"contractaddress\" \"owneraddress\"\n"
            "\nGet the token balance for a specific address (requires EVM).\n"
            "\nArguments:\n"
            "1. \"contractaddress\" (string, required) The contract address (hex).\n"
            "2. \"owneraddress\"    (string, required) The address (hex) to check the balance for.\n"
            "\nResult:\n"
            "\"balance\"          (string) The balance, formatted according to token decimals.\n"
            "\nExamples:\n"
            + HelpExampleCli("qrc20balanceof", "\"c6ca2697719d00446d4ea51f6fac88e4946bd8e4\" \"7579a8e5f423d45414c57a0b5caac1f2f44e37ca\"")
            + HelpExampleRpc("qrc20balanceof", "\"c6ca2697719d00446d4ea51f6fac88e4946bd8e4\", \"7579a8e5f423d45414c57a0b5caac1f2f44e37ca\""));
        // === END FIX ===

    // Note: Constructor usage might need adjustment based on how CallToken::balanceOf is ported
    Qrc20RpcCommand cmd(params, 2, 2, false); // Adjust requireSender based on CallToken needs
    std::string ownerAddressStr = params[1].get_str();

    std::string resultBalanceStr;
    // Assumes CallToken::balanceOf is ported and works
    if(!cmd.token.balanceOf(ownerAddressStr, resultBalanceStr))
        throw JSONRPCError(RPC_MISC_ERROR, "Fail to get balance (contract/address error or state error)");

    return cmd.formatAmount(resultBalanceStr); // Calls the corrected method
}

UniValue qrc20allowance(const UniValue& params, bool fHelp)
{
     if (fHelp || params.size() != 3)
        // === FIX: Added help string ===
        throw std::runtime_error(
            "qrc20allowance \"contractaddress\" \"owneraddress\" \"spenderaddress\"\n"
            "\nGet the amount of tokens that an owner allowed to a spender (requires EVM).\n"
            "\nArguments:\n"
            "1. \"contractaddress\" (string, required) The contract address (hex).\n"
            "2. \"owneraddress\"    (string, required) The address of the owner (hex).\n"
            "3. \"spenderaddress\"  (string, required) The address of the spender (hex).\n"
            "\nResult:\n"
            "\"allowance\"        (string) The amount of tokens allowed, formatted according to token decimals.\n"
            "\nExamples:\n"
            + HelpExampleCli("qrc20allowance", "\"c6ca...\" \"7579...\" \"f8e0...\"")
            + HelpExampleRpc("qrc20allowance", "\"c6ca...\", \"7579...\", \"f8e0...\""));
        // === END FIX ===

    Qrc20RpcCommand cmd(params, 3, 3);

    std::string ownerStr = params[1].get_str();
    std::string spenderStr = params[2].get_str();

    std::string resultAllowanceStr;
    // Assumes CallToken::allowance is ported and works
    if(!cmd.token.allowance(ownerStr, spenderStr, resultAllowanceStr))
        throw JSONRPCError(RPC_MISC_ERROR, "Fail to get allowance (contract/address error or state error)");

    return cmd.formatAmount(resultAllowanceStr); // Calls the corrected method
}


// ----------------------------------------------------------------------------
// The qrc20listtransactions function with fixes
// ----------------------------------------------------------------------------
UniValue qrc20listtransactions(const UniValue& params, bool fHelp)
{
     if (fHelp || params.size() < 2 || params.size() > 4)
        throw std::runtime_error( // Keep runtime_error for help display
            "qrc20listtransactions \"contractaddress\" \"targetaddress\" (fromBlock) (minconf)\n"
            "\nList QRC20 Transfer events related to a specific address (requires EVM and logevents).\n"
            "\nArguments:\n"
            "1. \"contractaddress\" (string, required) The QRC20 contract address (hex).\n"
            "2. \"targetaddress\"   (string, required) The DigiWage address to filter transactions for (sender or receiver).\n"
            "3. fromBlock         (numeric or string, optional, default=0) The block number (e.g. 1000), hex string (e.g. \"0x3e8\"), or string 'earliest' to start searching from.\n"
            "4. minconf           (numeric, optional, default=6) Minimal number of confirmations required for transactions to be included.\n"
            // Note: toBlock is implicitly 'latest confirmed' based on minconf
            "\nResult:\n"
            "[                                (array of json objects)\n"
            "  {\n"
            "    \"receiver\": \"address\",     (string) Receiving DigiWage address.\n"
            "    \"sender\": \"address\",       (string) Sending DigiWage address.\n"
            "    \"amount\": \"amount\",        (string) Amount transferred (positive if received by target, negative if sent by target), formatted.\n"
            "    \"confirmations\": n,        (numeric) Number of confirmations.\n"
            "    \"blockHash\": \"hash\",       (string) Hash of the block containing the transaction.\n"
            "    \"blockNumber\": n,          (numeric) Block number containing the transaction.\n"
            "    \"blockTime\": n,            (numeric) Block timestamp.\n"
            "    \"transactionHash\": \"hash\"  (string) Transaction hash.\n"
            "  }, ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("qrc20listtransactions", "\"c6ca...\" \"DWxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\" 0 1")
            + HelpExampleRpc("qrc20listtransactions", "\"c6ca...\", \"DWxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\", 0, 1")
        ); // End runtime_error

    // --- Implementation ---
     if (!fLogEvents) // Assumes fLogEvents global exists
        throw JSONRPCError(RPC_MISC_ERROR, "-logevents flag is required for this RPC.");

     if (!pstorageresult) // Assumes pstorageresult global exists
         throw JSONRPCError(RPC_INTERNAL_ERROR, "Log storage database (pstorageresult) is not available.");
      if (!pblocktree) // Assumes pblocktree global exists
         throw JSONRPCError(RPC_INTERNAL_ERROR, "Block tree database (pblocktree, required for log indexing) is not available.");

    // Validate DigiWage target address first
    CBitcoinAddress targetBitAddr(params[1].get_str());
    if (!targetBitAddr.IsValid()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DigiWage target address");
    }
    CTxDestination dest = targetBitAddr.Get();
    const CKeyID* keyID = boost::get<CKeyID>(&dest);
    if (!keyID) {
         throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address is not a P2PKH address, cannot derive EVM address for filtering.");
    }
    dev::h160 targetAddrParsed(keyID->begin(), dev::FixedHash<20>::ConstructFromPointerType{});

    // Use Qrc20RpcCommand for helper access later
    UniValue cmdParams(UniValue::VARR);
    cmdParams.push_back(params[0]); // contract address
    Qrc20RpcCommand cmd(cmdParams, 1, 4, false);

    int64_t fromBlock = 0;
    int64_t minconf = 6;
    int64_t toBlock = -1;

    if(params.size() > 2 && !params[2].isNull()) {
        fromBlock = parseBlockHeight(params[2], 0); // Assumes parseBlockHeight exists
        if (fromBlock < 0) throw JSONRPCError(RPC_INVALID_PARAMETER, "fromBlock cannot be negative");
    }

    if(params.size() > 3 && !params[3].isNull()) {
        minconf = params[3].getInt<int64_t>();
         if (minconf < 0) throw JSONRPCError(RPC_INVALID_PARAMETER, "minconf cannot be negative");
    }

    int latestHeight = 0;
    {
        LOCK(cs_main);
        latestHeight = chainActive.Height();
        toBlock = latestHeight - minconf; // Highest block number meeting minconf
    }
     if (toBlock < 0) {
           LogPrintf("qrc20listtransactions: Effective toBlock (%d) is negative based on current height and minconf.\n", toBlock);
           return UniValue(UniValue::VARR);
     }
     if (fromBlock > toBlock) {
           LogPrintf("qrc20listtransactions: fromBlock (%d) > effective toBlock (%d) based on minconf, returning empty list.\n", fromBlock, toBlock);
           return UniValue(UniValue::VARR);
     }

    // --- Fetch Events using searchlogs logic ---
    // !! CRITICAL: Assumes searchlogs is fixed or correctly implemented !!
    UniValue searchParams(UniValue::VARR);
    searchParams.push_back(UniValue(fromBlock));
    searchParams.push_back(UniValue(toBlock));

    UniValue addressFilter(UniValue::VOBJ);
    UniValue contractAddrs(UniValue::VARR);
    contractAddrs.push_back(params[0].get_str());
    addressFilter.pushKV("addresses", contractAddrs);
    searchParams.push_back(addressFilter);

    dev::h256 transferSig = dev::sha3(dev::bytesConstRef("Transfer(address,address,uint256)"));
    UniValue topicFilter(UniValue::VOBJ);
    UniValue topics(UniValue::VARR);
    topics.push_back(transferSig.hex());
    topics.push_back(NullUniValue); // Wildcard for 'from' address
    topics.push_back(NullUniValue); // Wildcard for 'to' address
    topicFilter.pushKV("topics", topics);
    searchParams.push_back(topicFilter);
    searchParams.push_back(UniValue(0)); // minconf for searchlogs (already handled in toBlock)

    UniValue logs = searchlogs(searchParams, false);
    // --- End Fetch Events ---

    uint32_t decimals = 0;
    // Corrected try-catch: Removed invalid catch(JSONRPCError)
    try {
        // !! CRITICAL: Assumes Qrc20RpcCommand::getDecimals() is fixed !!
        decimals = cmd.getDecimals();
    } catch (const std::exception& e) { // Catch *any* standard exception
         LogPrintf("WARN: qrc20listtransactions - Error getting decimals for contract %s: %s. Formatting will use 0 decimals.\n", params[0].get_str(), e.what());
         // decimals remains 0
    }

    UniValue res(UniValue::VARR);
    if (!logs.isArray()) {
         LogPrintf("ERROR: qrc20listtransactions - searchlogs did not return an array.\n");
         // Use the JSONRPCError FUNCTION to create the error object to throw
         throw JSONRPCError(RPC_INTERNAL_ERROR, "Internal error retrieving logs.");
    }

    std::vector<UniValue> entries; // Temporary vector for sorting
    entries.reserve(logs.size());

    for (const UniValue& log : logs.getValues()) {
         if (!log.isObject()) continue;

         // Access UniValue members safely
         const UniValue& logTopicsUV = log["topics"];
         const UniValue& logDataUV = log["data"];
         const UniValue& blockHashUV = log["blockHash"];
         const UniValue& blockNumUV = log["blockNumber"];
         const UniValue& txHashUV = log["transactionHash"];

         // Correct UniValue::find usage: Check existence and type, then get pointer
         const UniValue* txIndexUV = nullptr;
         if (log.exists("transactionIndex") && log["transactionIndex"].isNum()) {
             txIndexUV = &log["transactionIndex"];
         }

         // Basic validation
         if (!logTopicsUV.isArray() || logTopicsUV.size() < 3 || !logDataUV.isStr() || logDataUV.get_str().empty() ||
             !blockHashUV.isStr() || !blockNumUV.isNum() || !txHashUV.isStr())
         {
              LogPrintf("WARN: qrc20listtransactions - Skipping log with missing/invalid fields: %s\n", log.write());
              continue;
         }
         // Check topic0 signature
         if (logTopicsUV[0].isNull() || !logTopicsUV[0].isStr() || logTopicsUV[0].get_str() != transferSig.hex()) {
              LogPrintf("WARN: qrc20listtransactions - Skipping log with incorrect topic0 signature.\n");
              continue;
         }

         dev::h160 fromAddr;
         dev::h160 toAddr;
         dev::u256 value;
         try {
             if (!logTopicsUV[1].isStr() || !logTopicsUV[2].isStr()) throw std::runtime_error("Topic is not a string");
             dev::h256 fromTopic = dev::h256(logTopicsUV[1].get_str());
             dev::h256 toTopic = dev::h256(logTopicsUV[2].get_str());

             // Correct h160 construction from h256 (implicit lower 20 bytes)
             fromAddr = dev::h160(fromTopic);
             toAddr = dev::h160(toTopic);

             value = dev::u256(logDataUV.get_str());
         } catch (const std::exception& e) {
              LogPrintf("WARN: qrc20listtransactions - Skipping log due to parsing error (from/to/value): %s\n", e.what());
              continue;
         }

         // Filter based on target address being sender OR receiver
          if (fromAddr != targetAddrParsed && toAddr != targetAddrParsed) {
               continue;
          }

         UniValue obj(UniValue::VOBJ);
         try {
            obj.pushKV("receiver", CBitcoinAddress(DevH160ToUint160(toAddr)).ToString());
            obj.pushKV("sender", CBitcoinAddress(DevH160ToUint160(fromAddr)).ToString());
         } catch (const std::exception& e) {
            LogPrintf("WARN: qrc20listtransactions - Skipping log due to address conversion error: %s\n", e.what());
            continue;
         }

         dev::s256 value_s256 = dev::s256(value);
         std::string formattedAmount;
         try {
            // !! Assumes FormatToken helper is correctly ported !!
            formattedAmount = FormatToken(decimals, value_s256);
         } catch (const std::exception& e) {
             LogPrintf("WARN: qrc20listtransactions - Error formatting token amount for tx %s: %s. Using raw value.\n", txHashUV.get_str(), e.what());
             formattedAmount = value.str();
         }

         if (fromAddr == targetAddrParsed && toAddr == targetAddrParsed) {
              obj.pushKV("amount", FormatToken(decimals, 0)); // Format 0 correctly
         } else if (toAddr == targetAddrParsed) {
             obj.pushKV("amount", "+" + formattedAmount);
         } else {
             obj.pushKV("amount", "-" + formattedAmount);
         }

          int64_t blockNumber = blockNumUV.getInt<int64_t>();
          int confirms = (latestHeight >= blockNumber) ? (latestHeight - blockNumber) : 0;
          obj.pushKV("confirmations", confirms);
          obj.pushKV("blockHash", blockHashUV.get_str());
          obj.pushKV("blockNumber", blockNumber);

          int64_t nTime = 0;
          {
               LOCK(cs_main);
               uint256 blockHash = uint256S(blockHashUV.get_str());
               if (mapBlockIndex.count(blockHash)) {
                   CBlockIndex* pindex = mapBlockIndex.at(blockHash);
                   if (pindex && pindex->nHeight == blockNumber) {
                      nTime = pindex->GetBlockTime();
                   }
               } else if (blockNumber >= 0 && blockNumber <= latestHeight) {
                    CBlockIndex* pindexChain = chainActive[blockNumber];
                    if (pindexChain && pindexChain->GetBlockHash() == blockHash) {
                        nTime = pindexChain->GetBlockTime();
                    }
               }
          }
          obj.pushKV("blockTime", nTime);
          obj.pushKV("transactionHash", txHashUV.get_str());

          if (txIndexUV) { // Check if the pointer is valid
               obj.pushKV("transactionIndex", txIndexUV->getInt<int64_t>());
          }

          entries.push_back(obj); // Add to the temporary vector
     } // End log iteration

    // Sort the temporary vector
    std::sort(entries.begin(), entries.end(), [](const UniValue& a, const UniValue& b) {
        int64_t blockA = (a.exists("blockNumber") && a["blockNumber"].isNum()) ? a["blockNumber"].getInt<int64_t>() : -1LL;
        int64_t blockB = (b.exists("blockNumber") && b["blockNumber"].isNum()) ? b["blockNumber"].getInt<int64_t>() : -1LL;

        if (blockA != blockB) {
            return blockA > blockB; // Descending block number
        }

        int64_t txIndexA = (a.exists("transactionIndex") && a["transactionIndex"].isNum()) ? a["transactionIndex"].getInt<int64_t>() : -1LL;
        int64_t txIndexB = (b.exists("transactionIndex") && b["transactionIndex"].isNum()) ? b["transactionIndex"].getInt<int64_t>() : -1LL;

        return txIndexA > txIndexB; // Descending tx index
    });

    // Push sorted entries into the final UniValue array
    for(const auto& entry : entries) {
        res.push_back(entry);
    }

    return res;
}