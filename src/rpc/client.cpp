// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The DIGIWAGE developers // Or PIVX depending on exact base
// Copyright (c) 2017-2021 The Qtum Core developers // Added for EVM parts
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/client.h"
#include "rpc/protocol.h" // For RPCErrorCode and JSONRPCError function
#include "util.h"         // For LogPrintf (if needed) AND TrimString (assuming it's here)
#include "utilstrencodings.h" // For general string utilities if TrimString is not in util.h
#include "tinyformat.h"   // For tfm::format used in error messages

#include <set>
#include <stdint.h>
#include <vector>
#include <string>
#include <utility>
#include <stdexcept> // For std::runtime_error

#include <boost/algorithm/string/case_conv.hpp> // For boost::algorithm::to_lower
#include <boost/algorithm/string/trim.hpp> // <<< FIX: Include for boost::trim if TrimString doesn't exist

#include <univalue.h>


class CRPCConvertParam
{
public:
    std::string methodName; //! method whose params want conversion
    int paramIdx;           //! 0-based idx of param to convert
};

// Merged parameter list for conversion from string to JSON value
// Action Required: Review this list carefully against your ACTUAL RPC commands and expected parameter types.
static const CRPCConvertParam vRPCConvertParams[] =
    {
        // --- Existing DigiWage/PIVX entries ---
        {"stop", 0},
        {"setmocktime", 0},
        {"getaddednodeinfo", 0},
        {"setgenerate", 0},
        {"setgenerate", 1},
        {"generate", 0},
        {"getnetworkhashps", 0},
        {"getnetworkhashps", 1},
        {"delegatestake", 1},       // amount (numeric)
        {"delegatestake", 3},       // gasLimit (numeric)
        {"delegatestake", 4},       // gasPrice (numeric or string?) - Parse as JSON
        {"delegatestake", 5},       // broadcast (bool)
        {"rawdelegatestake", 1},    // amount (numeric)
        {"rawdelegatestake", 3},    // gasLimit (numeric)
        {"rawdelegatestake", 4},    // gasPrice (numeric or string?) - Parse as JSON
        {"sendtoaddress", 1},       // amount (numeric)
        {"sendtoaddressix", 1},     // amount (numeric)
        {"settxfee", 0},            // amount (numeric)
        {"burn", 0},                // amount (numeric)
        {"getreceivedbyaddress", 1},// minconf (numeric)
        {"getreceivedbyaccount", 1},// minconf (numeric)
        {"listcoldutxos", 0},       // include_watchonly (bool)
        {"listdelegators", 0},      // include_watchonly (bool)
        {"listreceivedbyaddress", 0},// minconf (numeric)
        {"listreceivedbyaddress", 1},// include_empty (bool)
        {"listreceivedbyaddress", 2},// include_watchonly (bool)
        {"listreceivedbyaccount", 0},// minconf (numeric)
        {"listreceivedbyaccount", 1},// include_empty (bool)
        {"listreceivedbyaccount", 2},// include_watchonly (bool)
        {"getbalance", 1},          // minconf (numeric)
        {"getbalance", 2},          // include_watchonly (bool)
        {"getblockhash", 0},        // height (numeric)
        {"waitforblockheight", 0},  // height (numeric)
        {"waitforblockheight", 1},  // timeout (numeric)
        {"waitforblock", 1},        // timeout (numeric)
        {"waitfornewblock", 0},     // timeout (numeric)
        {"move", 2},                // amount (numeric)
        {"move", 3},                // minconf (numeric)
        {"sendfrom", 2},            // amount (numeric)
        {"sendfrom", 3},            // minconf (numeric)
        {"listtransactions", 1},    // count (numeric)
        {"listtransactions", 2},    // skip (numeric)
        {"listtransactions", 3},    // include_watchonly (bool)
        {"listaccounts", 0},        // minconf (numeric)
        {"listaccounts", 1},        // include_watchonly (bool)
        {"walletpassphrase", 1},    // timeout (numeric)
        {"walletpassphrase", 2},    // stakingonly (bool)
        {"getblocktemplate", 0},    // template_request (JSON object)
        {"listsinceblock", 1},      // target_confirmations (numeric)
        {"listsinceblock", 2},      // include_watchonly (bool)
        {"sendmany", 1},            // amounts (JSON object)
        {"sendmany", 2},            // minconf (numeric)
        {"addmultisigaddress", 0},  // nrequired (numeric)
        {"addmultisigaddress", 1},  // keys (JSON array)
        {"createmultisig", 0},      // nrequired (numeric)
        {"createmultisig", 1},      // keys (JSON array)
        {"listunspent", 0},         // minconf (numeric)
        {"listunspent", 1},         // maxconf (numeric)
        {"listunspent", 2},         // addresses (JSON array)
        {"listunspent", 3},         // include_unsafe (bool)
        {"getblock", 1},            // verbose (bool) / verbosity (numeric) - parse as JSON
        {"getblockheader", 1},      // verbose (bool)
        {"gettransaction", 1},      // include_watchonly (bool)
        {"getrawtransaction", 1},   // verbose (bool) / verbosity (numeric) - parse as JSON
        {"createrawtransaction", 0},// inputs (JSON array)
        {"createrawtransaction", 1},// outputs (JSON object or array) - parse as JSON
        {"createrawtransaction", 2},// locktime (numeric)
        {"signrawtransaction", 1},  // prevtxs (JSON array)
        {"signrawtransaction", 2},  // privkeys (JSON array)
        {"sendrawtransaction", 1},  // allowhighfees / maxfeerate (bool or numeric string) - parse as JSON
        {"gettxout", 1},            // n (numeric)
        {"gettxout", 2},            // include_mempool (bool)
        {"lockunspent", 0},         // unlock (bool)
        {"lockunspent", 1},         // transactions (JSON array)
        {"importprivkey", 2},       // rescan (bool)
        {"importaddress", 2},       // rescan (bool)
        {"verifychain", 0},         // checklevel (numeric)
        {"verifychain", 1},         // nblocks (numeric)
        {"keypoolrefill", 0},       // newsize (numeric)
        {"getrawmempool", 0},       // verbose (bool)
        {"estimatefee", 0},         // nblocks (numeric)
        {"estimatepriority", 0},    // nblocks (numeric)
        {"prioritisetransaction", 1},// priority_delta (numeric) - deprecated? Might take dummy string now. Check server.
        {"prioritisetransaction", 2},// fee_delta (numeric)
        {"setban", 2},              // bantime (numeric)
        {"setban", 3},              // absolute (bool)
        {"spork", 1},               // value (numeric)
        {"preparebudget", 2},       // count (numeric)
        {"preparebudget", 3},       // amount (numeric)
        {"preparebudget", 5},       // time (numeric)
        {"submitbudget", 2},        // count (numeric)
        {"submitbudget", 3},        // amount (numeric)
        {"submitbudget", 5},        // time (numeric)
        {"submitbudget", 7},        // fee (numeric)
        {"mnvoteraw", 1},           // time (numeric)
        {"mnvoteraw", 4},           // vote (numeric)
        {"setstakesplitthreshold", 0},// threshold (numeric)
        {"autocombinerewards", 0},  // enable (bool)
        {"autocombinerewards", 1},  // threshold (numeric)
        {"getblockindexstats", 0},  // height (numeric)
        {"getblockindexstats", 1},  // range (numeric)
        {"getblockindexstats", 2},  // fFeeOnly (bool)
        {"getfeeinfo", 0},          // blocks (numeric)

        // --- ADDED QTUM/EVM Entries ---
        { "getaddresstxids", 0},      // argument (JSON object)
        { "getaddressmempool", 0},    // argument (JSON object)
        { "getaddressdeltas", 0},     // argument (JSON object)
        { "getaddressbalance", 0},   // argument (JSON object)
        { "getaddressutxos", 0},      // argument (JSON object)
        { "getblockhashes", 0},       // high (numeric)
        { "getblockhashes", 1},       // low (numeric)
        { "getblockhashes", 2},       // options (JSON object)
        { "getspentinfo", 0},         // argument (JSON object)
        { "searchlogs", 0},          // fromblock (numeric or string) - Parse to handle numeric case
        { "searchlogs", 1},          // toblock (numeric or string) - Parse to handle numeric case
        { "searchlogs", 2},          // addressFilter (JSON object)
        { "searchlogs", 3},          // topicFilter (JSON object)
        { "searchlogs", 4},          // minconf (numeric)
        { "waitforlogs", 0},          // fromblock (numeric or string) - Parse to handle numeric case
        { "waitforlogs", 1},          // toblock (numeric or string) - Parse to handle numeric case
        { "waitforlogs", 2},          // filter (JSON object)
        { "waitforlogs", 3},          // minconf (numeric)
        { "qrc20listtransactions", 2},// fromBlock (numeric or string) - Parse to handle numeric case
        { "qrc20listtransactions", 3},// minconf (numeric)
        { "createcontract", 1},      // gaslimit (numeric)
        { "createcontract", 2},      // gasprice (numeric or string amount) - Parse as JSON
        { "createcontract", 4},      // broadcast (bool)
        { "createcontract", 5},      // changetosender (bool)
        { "createcontract", 6},      // psbt (bool) - Assuming needed
        { "sendtocontract", 2},      // amount (numeric)
        { "sendtocontract", 3},      // gaslimit (numeric)
        { "sendtocontract", 4},      // gasprice (numeric or string amount) - Parse as JSON
        { "sendtocontract", 6},      // broadcast (bool)
        { "sendtocontract", 7},      // changetosender (bool)
        { "sendtocontract", 8},      // psbt (bool) - Assuming needed
        { "callcontract", 3},        // amount (numeric)
        { "callcontract", 4},        // gaslimit (numeric)
        { "callcontract", 5},        // gasprice (numeric or string amount) - Parse as JSON
        { "qrc20approve", 4},        // gaslimit (numeric)
        { "qrc20approve", 5},        // gasprice (numeric or string amount) - Parse as JSON
        { "qrc20approve", 6},        // checkoutputs (bool)
        { "qrc20transfer", 4},       // gaslimit (numeric)
        { "qrc20transfer", 5},       // gasprice (numeric or string amount) - Parse as JSON
        { "qrc20transfer", 6},       // checkoutputs (bool)
        { "qrc20transferfrom", 5},   // gaslimit (numeric)
        { "qrc20transferfrom", 6},   // gasprice (numeric or string amount) - Parse as JSON
        { "qrc20transferfrom", 7},   // checkoutputs (bool)
        { "qrc20burn", 3},           // gaslimit (numeric)
        { "qrc20burn", 4},           // gasprice (numeric or string amount) - Parse as JSON
        { "qrc20burn", 5},           // checkoutputs (bool)
        { "qrc20burnfrom", 4},       // gaslimit (numeric)
        { "qrc20burnfrom", 5},       // gasprice (numeric or string amount) - Parse as JSON
        { "qrc20burnfrom", 6},       // checkoutputs (bool)
        { "listcontracts", 0},       // start (numeric)
        { "listcontracts", 1},       // maxdisplay (numeric)
        { "getstorage", 1},          // blocknum (numeric or string) - Parse to handle numeric case
        { "getstorage", 2},          // index (numeric)
    };

class CRPCConvertTable
{
private:
    std::set<std::pair<std::string, int> > members;

public:
    CRPCConvertTable();

    bool convert(const std::string& method, int idx)
    {
        // Lookup should be case-insensitive now due to storing lowercase
        return (members.count(std::make_pair(method, idx)) > 0);
    }
};

CRPCConvertTable::CRPCConvertTable()
{
    const unsigned int n_elem =
        (sizeof(vRPCConvertParams) / sizeof(vRPCConvertParams[0]));

    for (unsigned int i = 0; i < n_elem; i++) {
        // Store lowercase method name in the set for case-insensitive lookup
        std::string lower_method = vRPCConvertParams[i].methodName;
        boost::algorithm::to_lower(lower_method);
        members.insert(std::make_pair(lower_method,
            vRPCConvertParams[i].paramIdx));
    }
}

static CRPCConvertTable rpcCvtTable;

/** Non-RFC4627 JSON parser, accepts internal values (such as numbers, true, false, null)
 * as well as objects and arrays. Returns the original string if parsing fails.
 */
UniValue ParseNonRFCJSONValue(const std::string& strVal)
{
    UniValue jVal;
    std::string trimmedVal = strVal; // Copy original string
    boost::algorithm::trim(trimmedVal); // Use boost::algorithm::trim

    if (trimmedVal.empty()) {
        // Return the original (likely empty) string if input was empty/whitespace
        return strVal;
    }

    // Check if it looks like a valid JSON object or array first
    if ((trimmedVal.front() == '{' && trimmedVal.back() == '}') || (trimmedVal.front() == '[' && trimmedVal.back() == ']')) {
        if (jVal.read(trimmedVal)) { // Try reading the trimmed string directly
            return jVal; // Success
        }
        // If direct read fails, it might be malformed JSON, fall through to return string
    }

    // Otherwise, try parsing it as a single value (number, bool, null, string)
    // by wrapping it in brackets.
    if (jVal.read(std::string("[")+trimmedVal+std::string("]")) &&
        jVal.isArray() && jVal.size() == 1) {
        return jVal[0]; // Return the single parsed element
    }

    // If all parsing attempts fail, return the original string literal
    return strVal;
}


/** Convert strings to command-specific RPC representation */
UniValue RPCConvertValues(const std::string &strMethod, const std::vector<std::string> &strParams)
{
    UniValue params(UniValue::VARR);
    std::string lowerMethod = strMethod;
    boost::algorithm::to_lower(lowerMethod); // Convert method name to lowercase once

    for (unsigned int idx = 0; idx < strParams.size(); idx++) {
        const std::string& strVal = strParams[idx];

        // Use the lowercase method name for lookup
        if (!rpcCvtTable.convert(lowerMethod, idx)) {
            // insert string value directly if no conversion needed
            params.push_back(strVal);
        } else {
            // parse string as JSON if needed, insert bool/number/object/etc. value
            // ParseNonRFCJSONValue will return original string if parsing fails
            try {
                 params.push_back(ParseNonRFCJSONValue(strVal));
            } catch (const std::runtime_error& e) {
                // Catch errors from within ParseNonRFCJSONValue if it throws (e.g., internal UniValue errors)
                 throw std::runtime_error(tfm::format("Parameter %d ('%s') for method %s could not be parsed as JSON: %s", idx, strVal, strMethod, e.what()));
            } catch (...) {
                 throw std::runtime_error(tfm::format("Parameter %d ('%s') for method %s could not be parsed as JSON due to an unknown error.", idx, strVal, strMethod));
            }
        }
    }

    return params;
}