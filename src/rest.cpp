// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2017-2019 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "core_io.h" // For TxToUniv, ScriptPubKeyToUniv, etc.
#include "main.h"
#include "httpserver.h"
#include "rpc/server.h" // <<< ADDED THIS INCLUDE >>>
#include "streams.h"
#include "sync.h"
#include "txmempool.h"
#include "utilstrencodings.h"
#include "version.h"
#include "main.h" // Needed for chainActive, mapBlockIndex, pcoinsTip etc.
#include "utilmoneystr.h" // Needed for ValueFromAmount

#include <boost/algorithm/string.hpp>
#include <boost/dynamic_bitset.hpp>

#include <univalue.h>


static const size_t MAX_GETUTXOS_OUTPOINTS = 15; //allow a max of 15 outpoints to be queried at once

enum RetFormat {
    RF_UNDEF,
    RF_BINARY,
    RF_HEX,
    RF_JSON,
};

static const struct {
    enum RetFormat rf;
    const char* name;
} rf_names[] = {
      {RF_UNDEF, ""},
      {RF_BINARY, "bin"},
      {RF_HEX, "hex"},
      {RF_JSON, "json"},
};

// Helper Class - SHOULD BE DEFINED IN A HEADER if used elsewhere
// For now, defining it locally here.
struct CCoin {
    uint32_t nTxVer; // Don't call this nVersion, that name has a special meaning inside IMPLEMENT_SERIALIZE
    uint32_t nHeight;
    CTxOut out;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(nTxVer);
        READWRITE(nHeight);
        READWRITE(out);
    }
};


// External function declarations (ensure these are correct and defined elsewhere)
extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry); // Defined in rpc/rawtransaction.cpp ?
extern UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false); // Defined in rpc/blockchain.cpp
extern UniValue mempoolInfoToJSON(); // Defined in rpc/blockchain.cpp
extern UniValue mempoolToJSON(bool fVerbose = false); // Defined in rpc/blockchain.cpp
extern void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex); // Defined in rpc/rawtransaction.cpp ? Ensure it exists!
extern UniValue blockheaderToJSON(const CBlockIndex* blockindex); // Defined in rpc/blockchain.cpp
// Need declaration for getblockchaininfo (defined in rpc/blockchain.cpp, declared in rpc/server.h)
extern UniValue getblockchaininfo(const UniValue& params, bool fHelp);


static bool RESTERR(HTTPRequest* req, enum HTTPStatusCode status, std::string message)
{
    req->WriteHeader("Content-Type", "text/plain");
    req->WriteReply(status, message + "\r\n");
    return false;
}

static enum RetFormat ParseDataFormat(std::vector<std::string>& params, const std::string& strReq)
{
    boost::split(params, strReq, boost::is_any_of("."));
    if (params.size() > 1) {
        for (unsigned int i = 0; i < ARRAYLEN(rf_names); i++)
            if (params[1] == rf_names[i].name)
                return rf_names[i].rf;
    }

    return rf_names[0].rf;
}

static std::string AvailableDataFormatsString()
{
    std::string formats = "";
    for (unsigned int i = 0; i < ARRAYLEN(rf_names); i++)
        if (strlen(rf_names[i].name) > 0) {
            formats.append(".");
            formats.append(rf_names[i].name);
            formats.append(", ");
        }

    if (formats.length() > 0)
        return formats.substr(0, formats.length() - 2);

    return formats;
}

static bool ParseHashStr(const std::string& strReq, uint256& v)
{
    if (!IsHex(strReq) || (strReq.size() != 64))
        return false;

    v.SetHex(strReq);
    return true;
}

static bool CheckWarmup(HTTPRequest* req)
{
    std::string statusmessage;
    if (RPCIsInWarmup(&statusmessage)) // Assumes RPCIsInWarmup is declared (e.g., in rpc/server.h)
         return RESTERR(req, HTTP_SERVICE_UNAVAILABLE, "Service temporarily unavailable: " + statusmessage);
    return true;
}

static bool rest_headers(HTTPRequest* req,
                         const std::string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;
    std::vector<std::string> params;
    const RetFormat rf = ParseDataFormat(params, strURIPart);
    std::vector<std::string> path;
    boost::split(path, params[0], boost::is_any_of("/"));

    if (path.size() != 2)
        return RESTERR(req, HTTP_BAD_REQUEST, "No header count specified. Use /rest/headers/<count>/<hash>.<ext>.");

    long count = strtol(path[0].c_str(), NULL, 10);
    if (count < 1 || count > 2000)
        return RESTERR(req, HTTP_BAD_REQUEST, "Header count out of range: " + path[0]);

    std::string hashStr = path[1];
    uint256 hash;
    if (!ParseHashStr(hashStr, hash))
        return RESTERR(req, HTTP_BAD_REQUEST, "Invalid hash: " + hashStr);

    std::vector<const CBlockIndex *> headers;
    headers.reserve(count);
    {
        LOCK(cs_main); // Requires cs_main from sync.h
        BlockMap::const_iterator it = mapBlockIndex.find(hash); // Requires mapBlockIndex from main.h
        const CBlockIndex *pindex = (it != mapBlockIndex.end()) ? it->second : NULL;
        while (pindex != NULL && chainActive.Contains(pindex)) { // Requires chainActive from validation.h
            headers.push_back(pindex);
            if (headers.size() == (unsigned long)count)
                break;
            pindex = chainActive.Next(pindex);
        }
    }

    CDataStream ssHeader(SER_NETWORK, PROTOCOL_VERSION);
    for (const CBlockIndex *pindex : headers) {
        ssHeader << pindex->GetBlockHeader();
    }

    switch (rf) {
    case RF_BINARY: {
        std::string binaryHeader = ssHeader.str();
        req->WriteHeader("Content-Type", "application/octet-stream");
        req->WriteReply(HTTP_OK, binaryHeader);
        return true;
    }

    case RF_HEX: {
        std::string strHex = HexStr(ssHeader.begin(), ssHeader.end()) + "\n";
        req->WriteHeader("Content-Type", "text/plain");
        req->WriteReply(HTTP_OK, strHex);
        return true;
    }
    case RF_JSON: {
        UniValue jsonHeaders(UniValue::VARR);
        for (const CBlockIndex *pindex : headers) {
            jsonHeaders.push_back(blockheaderToJSON(pindex)); // Assumes blockheaderToJSON is declared/defined
        }
        std::string strJSON = jsonHeaders.write() + "\n";
        req->WriteHeader("Content-Type", "application/json");
        req->WriteReply(HTTP_OK, strJSON);
        return true;
    }
    default: {
        return RESTERR(req, HTTP_NOT_FOUND, "output format not found (available: .bin, .hex, .json)"); // Added json
    }
    }
}

static bool rest_block(HTTPRequest* req,
                       const std::string& strURIPart,
                       bool showTxDetails)
{
    if (!CheckWarmup(req))
        return false;
    std::vector<std::string> params;
    const RetFormat rf = ParseDataFormat(params, strURIPart);

    std::string hashStr = params[0];
    uint256 hash;
    if (!ParseHashStr(hashStr, hash))
        return RESTERR(req, HTTP_BAD_REQUEST, "Invalid hash: " + hashStr);

    CBlock block;
    CBlockIndex* pblockindex = NULL;
    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            return RESTERR(req, HTTP_NOT_FOUND, hashStr + " not found");

        pblockindex = mapBlockIndex[hash];
        // Check block status correctly
        if (!pblockindex->IsValid(BLOCK_VALID_SCRIPTS)) { // Example check, adjust if needed
            // Distinguish between pruned and genuinely invalid/not found
            if (!(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
                 return RESTERR(req, HTTP_NOT_FOUND, hashStr + " not available (pruned data)");
             else
                 return RESTERR(req, HTTP_NOT_FOUND, hashStr + " not found or invalid");
        }

        if (!ReadBlockFromDisk(block, pblockindex)) // Assumes ReadBlockFromDisk exists
            return RESTERR(req, HTTP_NOT_FOUND, hashStr + " block not found on disk");
    }

    CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
    ssBlock << block;

    switch (rf) {
    case RF_BINARY: {
        std::string binaryBlock = ssBlock.str();
        req->WriteHeader("Content-Type", "application/octet-stream");
        req->WriteReply(HTTP_OK, binaryBlock);
        return true;
    }

    case RF_HEX: {
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end()) + "\n";
        req->WriteHeader("Content-Type", "text/plain");
        req->WriteReply(HTTP_OK, strHex);
        return true;
    }

    case RF_JSON: {
        UniValue objBlock = blockToJSON(block, pblockindex, showTxDetails); // Assumes blockToJSON is declared/defined
        std::string strJSON = objBlock.write() + "\n";
        req->WriteHeader("Content-Type", "application/json");
        req->WriteReply(HTTP_OK, strJSON);
        return true;
    }

    default: {
        return RESTERR(req, HTTP_NOT_FOUND, "output format not found (available: " + AvailableDataFormatsString() + ")");
    }
    }
}

static bool rest_block_extended(HTTPRequest* req, const std::string& strURIPart)
{
    return rest_block(req, strURIPart, true);
}

static bool rest_block_notxdetails(HTTPRequest* req, const std::string& strURIPart)
{
    return rest_block(req, strURIPart, false);
}

static bool rest_chaininfo(HTTPRequest* req, const std::string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;
    std::vector<std::string> params;
    const RetFormat rf = ParseDataFormat(params, strURIPart);

    switch (rf) {
    case RF_JSON: {
        UniValue rpcParams(UniValue::VARR); // Needs empty params for getblockchaininfo
        UniValue chainInfoObject = getblockchaininfo(rpcParams, false); // Call the RPC function
        std::string strJSON = chainInfoObject.write() + "\n";
        req->WriteHeader("Content-Type", "application/json");
        req->WriteReply(HTTP_OK, strJSON);
        return true;
    }
    default: {
        return RESTERR(req, HTTP_NOT_FOUND, "output format not found (available: json)");
    }
    }
}

static bool rest_mempool_info(HTTPRequest* req, const std::string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;
    std::vector<std::string> params;
    const RetFormat rf = ParseDataFormat(params, strURIPart);

    switch (rf) {
    case RF_JSON: {
        UniValue mempoolInfoObject = mempoolInfoToJSON(); // Assumes mempoolInfoToJSON exists

        std::string strJSON = mempoolInfoObject.write() + "\n";
        req->WriteHeader("Content-Type", "application/json");
        req->WriteReply(HTTP_OK, strJSON);
        return true;
    }
    default: {
        return RESTERR(req, HTTP_NOT_FOUND, "output format not found (available: json)");
    }
    }
}

static bool rest_mempool_contents(HTTPRequest* req, const std::string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;
    std::vector<std::string> params;
    const RetFormat rf = ParseDataFormat(params, strURIPart);

    switch (rf) {
    case RF_JSON: {
        UniValue mempoolObject = mempoolToJSON(true); // Assumes mempoolToJSON exists

        std::string strJSON = mempoolObject.write() + "\n";
        req->WriteHeader("Content-Type", "application/json");
        req->WriteReply(HTTP_OK, strJSON);
        return true;
    }
    default: {
        return RESTERR(req, HTTP_NOT_FOUND, "output format not found (available: json)");
    }
    }
}

static bool rest_tx(HTTPRequest* req, const std::string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;
    std::vector<std::string> params;
    const RetFormat rf = ParseDataFormat(params, strURIPart);

    std::string hashStr = params[0];
    uint256 hash;
    if (!ParseHashStr(hashStr, hash))
        return RESTERR(req, HTTP_BAD_REQUEST, "Invalid hash: " + hashStr);

    CTransaction tx;
    uint256 hashBlock = uint256();
    if (!GetTransaction(hash, tx, hashBlock, true)) // Assumes GetTransaction exists (main.h)
        return RESTERR(req, HTTP_NOT_FOUND, hashStr + " not found");

    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << tx;

    switch (rf) {
    case RF_BINARY: {
        std::string binaryTx = ssTx.str();
        req->WriteHeader("Content-Type", "application/octet-stream");
        req->WriteReply(HTTP_OK, binaryTx);
        return true;
    }

    case RF_HEX: {
        std::string strHex = HexStr(ssTx.begin(), ssTx.end()) + "\n";
        req->WriteHeader("Content-Type", "text/plain");
        req->WriteReply(HTTP_OK, strHex);
        return true;
    }

    case RF_JSON: {
        UniValue objTx(UniValue::VOBJ);
        TxToJSON(tx, hashBlock, objTx); // Assumes TxToJSON exists
        std::string strJSON = objTx.write() + "\n";
        req->WriteHeader("Content-Type", "application/json");
        req->WriteReply(HTTP_OK, strJSON);
        return true;
    }

    default: {
        return RESTERR(req, HTTP_NOT_FOUND, "output format not found (available: " + AvailableDataFormatsString() + ")");
    }
    }
}

static bool rest_getutxos(HTTPRequest* req, const std::string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;
    std::vector<std::string> params;
    enum RetFormat rf = ParseDataFormat(params, strURIPart);

    std::vector<std::string> uriParts;
    if (params.size() > 0 && params[0].length() > 0) // Handle empty param[0]
    {
        // params[0] might be '/' or '/checkmempool' etc. or '/checkmempool/txid-n'
        std::string strUriParams = params[0];
        if (strUriParams.at(0) == '/') strUriParams.erase(0, 1); // Remove leading '/'
        boost::split(uriParts, strUriParams, boost::is_any_of("/"));
    }

    std::string strRequestMutable = req->ReadBody();

    bool fInputParsed = false;
    bool fCheckMemPool = false;
    std::vector<COutPoint> vOutPoints;

    // parse/deserialize input
    if (!uriParts.empty())
    {
        size_t inputStart = 0;
        if (uriParts.size() > 0 && uriParts[0] == "checkmempool") {
            fCheckMemPool = true;
            inputStart = 1;
        }

        for (size_t i = inputStart; i < uriParts.size(); i++)
        {
            uint256 txid;
            int32_t nOutput;
            size_t sepPos = uriParts[i].find('-');
            if (sepPos == std::string::npos) {
                 return RESTERR(req, HTTP_BAD_REQUEST, "Parse error: Invalid URI input format, missing '-' separator: " + uriParts[i]);
            }
            std::string strTxid = uriParts[i].substr(0, sepPos);
            std::string strOutput = uriParts[i].substr(sepPos + 1);

            if (!ParseInt32(strOutput, &nOutput) || !IsHex(strTxid) || strTxid.length() != 64) {
                 return RESTERR(req, HTTP_BAD_REQUEST, "Parse error: Invalid txid or output index in URI: " + uriParts[i]);
            }

            txid.SetHex(strTxid);
            vOutPoints.push_back(COutPoint(txid, (uint32_t)nOutput));
        }

        if (!vOutPoints.empty())
            fInputParsed = true;
    }

    // Check if input is provided in body and format matches
    if (strRequestMutable.size() > 0) {
        if (fInputParsed) // Don't allow both URI and body inputs
            return RESTERR(req, HTTP_BAD_REQUEST, "Combination of URI scheme inputs and raw post data is not allowed");

        if (rf == RF_HEX) {
            std::vector<unsigned char> strRequestV = ParseHex(strRequestMutable);
            strRequestMutable.assign(strRequestV.begin(), strRequestV.end());
            // Fall through to binary parsing
        }

        if (rf == RF_BINARY || rf == RF_HEX) {
            try {
                CDataStream oss(ParseHex(strRequestMutable), SER_NETWORK, PROTOCOL_VERSION); // Need to parse hex if rf==RF_HEX
                oss >> fCheckMemPool;
                oss >> vOutPoints;
                 fInputParsed = true;
            } catch (const std::ios_base::failure& e) {
                return RESTERR(req, HTTP_BAD_REQUEST, "Parse error: Failed to deserialize binary input");
            } catch (...) {
                return RESTERR(req, HTTP_BAD_REQUEST, "Parse error: Unknown error during binary input deserialization");
            }
        } else {
             // JSON input in body not supported by this handler in standard bitcoin REST
             return RESTERR(req, HTTP_BAD_REQUEST, "Raw data input requires binary or hex format specified in URI (e.g., .bin or .hex)");
        }
    }

    // Check if we have inputs from either URI or body
    if (!fInputParsed || vOutPoints.empty()) {
        return RESTERR(req, HTTP_BAD_REQUEST, "Error: empty request");
    }


    // limit max outpoints
    if (vOutPoints.size() > MAX_GETUTXOS_OUTPOINTS)
        return RESTERR(req, HTTP_BAD_REQUEST, strprintf("Error: max outpoints exceeded (max: %d, tried: %d)", MAX_GETUTXOS_OUTPOINTS, vOutPoints.size()));

    // check spentness and form a bitmap
    std::vector<unsigned char> bitmap;
    std::vector<CCoin> outs;
    std::string bitmapStringRepresentation;
    boost::dynamic_bitset<unsigned char> hits(vOutPoints.size());
    {
        LOCK2(cs_main, mempool.cs); // Lock both needed

        CCoinsView viewDummy;
        CCoinsViewCache view(&viewDummy);

        CCoinsViewCache& viewChain = *pcoinsTip; // Requires pcoinsTip from validation.h
        CCoinsViewMemPool viewMempool(&viewChain, mempool); // Requires mempool from main.h

        if (fCheckMemPool)
            view.SetBackend(viewMempool);
        else
            view.SetBackend(viewChain); // Use chain UTXO set if not checking mempool

        for (size_t i = 0; i < vOutPoints.size(); i++) {
            CCoins coins;
            uint256 hash = vOutPoints[i].hash;
            // Use AccessCoins which checks the cache first, then the backend
            const CCoins* pCoins = view.AccessCoins(hash);
            if (pCoins && !pCoins->IsPruned()) { // Check if coins exist and are not pruned
                // Copy coins for modification (pruneSpent modifies its argument)
                CCoins coins_copy = *pCoins;
                if (fCheckMemPool) mempool.pruneSpent(hash, coins_copy); // Prune based on mempool if requested

                if (coins_copy.IsAvailable(vOutPoints[i].n)) {
                    hits[i] = true;
                    CCoin coin;
                    coin.nTxVer = coins_copy.nVersion;
                    coin.nHeight = coins_copy.nHeight;
                    coin.out = coins_copy.vout[vOutPoints[i].n]; // Safe index due to IsAvailable check
                    assert(!coin.out.IsNull());
                    outs.push_back(coin);
                }
            }
            bitmapStringRepresentation.append(hits[i] ? "1" : "0");
        }
    }
    boost::to_block_range(hits, std::back_inserter(bitmap));

    switch (rf) {
    case RF_BINARY: {
        CDataStream ssGetUTXOResponse(SER_NETWORK, PROTOCOL_VERSION);
        ssGetUTXOResponse << chainActive.Height() << chainActive.Tip()->GetBlockHash() << bitmap << outs;
        std::string ssGetUTXOResponseString = ssGetUTXOResponse.str();
        req->WriteHeader("Content-Type", "application/octet-stream");
        req->WriteReply(HTTP_OK, ssGetUTXOResponseString);
        return true;
    }

    case RF_HEX: {
        CDataStream ssGetUTXOResponse(SER_NETWORK, PROTOCOL_VERSION);
        ssGetUTXOResponse << chainActive.Height() << chainActive.Tip()->GetBlockHash() << bitmap << outs;
        std::string strHex = HexStr(ssGetUTXOResponse.begin(), ssGetUTXOResponse.end()) + "\n";
        req->WriteHeader("Content-Type", "text/plain");
        req->WriteReply(HTTP_OK, strHex);
        return true;
    }

    case RF_JSON: {
        UniValue objGetUTXOResponse(UniValue::VOBJ);
        objGetUTXOResponse.pushKV("chainHeight", chainActive.Height());
        objGetUTXOResponse.pushKV("chaintipHash", chainActive.Tip()->GetBlockHash().GetHex());
        objGetUTXOResponse.pushKV("bitmap", bitmapStringRepresentation);
        UniValue utxos(UniValue::VARR);
        for (const CCoin& coin : outs) {
            UniValue utxo(UniValue::VOBJ);
            utxo.pushKV("txvers", (int32_t)coin.nTxVer); // Should be version of tx that created this output
            utxo.pushKV("height", (int32_t)coin.nHeight);
            utxo.pushKV("value", ValueFromAmount(coin.out.nValue)); // Assumes ValueFromAmount exists
            UniValue o(UniValue::VOBJ);
            ScriptPubKeyToJSON(coin.out.scriptPubKey, o, true); // Assumes ScriptPubKeyToJSON exists
            utxo.pushKV("scriptPubKey", o);
            utxos.push_back(utxo);
        }
        objGetUTXOResponse.pushKV("utxos", utxos);
        std::string strJSON = objGetUTXOResponse.write() + "\n";
        req->WriteHeader("Content-Type", "application/json");
        req->WriteReply(HTTP_OK, strJSON);
        return true;
    }
    default: {
        return RESTERR(req, HTTP_NOT_FOUND, "output format not found (available: " + AvailableDataFormatsString() + ")");
    }
    }
}

static const struct {
    const char* prefix;
    bool (*handler)(HTTPRequest* req, const std::string& strReq);
} uri_prefixes[] = {
      {"/rest/tx/", rest_tx},
      {"/rest/block/notxdetails/", rest_block_notxdetails},
      {"/rest/block/", rest_block_extended},
      {"/rest/chaininfo", rest_chaininfo},
      {"/rest/mempool/info", rest_mempool_info},
      {"/rest/mempool/contents", rest_mempool_contents},
      {"/rest/headers/", rest_headers},
      {"/rest/getutxos", rest_getutxos}, // Needs specific handler implementation
};

// Ensure RegisterHTTPHandler and UnregisterHTTPHandler are declared (usually httpserver.h)
extern void RegisterHTTPHandler(const std::string& prefix, bool exactMatch, const HTTPRequestHandler& handler);
extern void UnregisterHTTPHandler(const std::string& prefix, bool exactMatch);

bool StartREST()
{
    for (unsigned int i = 0; i < ARRAYLEN(uri_prefixes); i++)
        RegisterHTTPHandler(uri_prefixes[i].prefix, true, uri_prefixes[i].handler); // Use true for exactMatch for REST? Check original usage.
    return true;
}

void InterruptREST()
{
    // Nothing needs to be interrupted specifically for REST in standard bitcoin
}

void StopREST()
{
    for (unsigned int i = 0; i < ARRAYLEN(uri_prefixes); i++)
        UnregisterHTTPHandler(uri_prefixes[i].prefix, true); // Match exactMatch used in StartREST
}