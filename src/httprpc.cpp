// Copyright (c) 2015-2017 The Bitcoin Core developers
// Copyright (c) 2017-2019 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "httprpc.h"

#include "base58.h"
#include "chainparams.h"
#include "httpserver.h"
#include "rpc/protocol.h"
#include "rpc/server.h"
#include "random.h"
#include "sync.h"
#include "util.h"
#include "utilstrencodings.h"
#include "guiinterface.h"
#include "univalue.h" // Make sure UniValue is included (it likely is via other headers, but good practice)

#include <boost/algorithm/string.hpp> // boost::trim

/** Simple one-shot callback timer to be used by the RPC mechanism to e.g.
 * re-lock the wellet.
 */
class HTTPRPCTimer : public RPCTimerBase
{
public:
    HTTPRPCTimer(struct event_base* eventBase, boost::function<void(void)>& func, int64_t millis) :
        ev(eventBase, false, func)
    {
        struct timeval tv;
        tv.tv_sec = millis/1000;
        tv.tv_usec = (millis%1000)*1000;
        ev.trigger(&tv);
    }
private:
    HTTPEvent ev;
};

class HTTPRPCTimerInterface : public RPCTimerInterface
{
public:
    HTTPRPCTimerInterface(struct event_base* base) : base(base)
    {
    }
    const char* Name()
    {
        return "HTTP";
    }
    RPCTimerBase* NewTimer(boost::function<void(void)>& func, int64_t millis)
    {
        return new HTTPRPCTimer(base, func, millis);
    }
private:
    struct event_base* base;
};


/* Pre-base64-encoded authentication token */
static std::string strRPCUserColonPass;
/* Stored RPC timer interface (for unregistration) */
static HTTPRPCTimerInterface* httpRPCTimerInterface = 0;

static void JSONErrorReply(HTTPRequest* req, const UniValue& objError, const UniValue& id)
{
    // Send error reply from json-rpc error object
    int nStatus = HTTP_INTERNAL_SERVER_ERROR;
    int code = RPC_INTERNAL_ERROR; // Default value in case lookup fails
    try {
        // Use operator[] to access the value associated with the key "code"
        const UniValue& codeVal = objError["code"];
        if (!codeVal.isNull()) { // Check if the key exists and is not null
             code = codeVal.getInt<int>();
        }
    } catch (const std::exception& e) {
        // Handle potential exceptions if "code" exists but is not an integer,
        // or other UniValue errors. Log the error?
        LogPrintf("JSONErrorReply: Error retrieving 'code' from error object: %s\n", e.what());
        // Keep the default code RPC_INTERNAL_ERROR
    } catch (...) {
        // Catch any other exceptions
        LogPrintf("JSONErrorReply: Unknown error retrieving 'code' from error object.\n");
        // Keep the default code RPC_INTERNAL_ERROR
    }


    if (code == RPC_INVALID_REQUEST)
        nStatus = HTTP_BAD_REQUEST;
    else if (code == RPC_METHOD_NOT_FOUND)
        nStatus = HTTP_NOT_FOUND;
    // Consider adding other mappings here if needed, e.g., RPC_PARSE_ERROR -> HTTP_BAD_REQUEST

    std::string strReply = JSONRPCReply(NullUniValue, objError, id);

    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(nStatus, strReply);
}

static bool RPCAuthorized(const std::string& strAuth)
{
    if (strRPCUserColonPass.empty()) // Belt-and-suspenders measure if InitRPCAuthentication was not called
        return false;
    if (strAuth.substr(0, 6) != "Basic ")
        return false;
    std::string strUserPass64 = strAuth.substr(6);
    boost::trim(strUserPass64);
    std::string strUserPass = DecodeBase64(strUserPass64);
    return TimingResistantEqual(strUserPass, strRPCUserColonPass);
}

static bool HTTPReq_JSONRPC(HTTPRequest* req, const std::string &)
{
    // JSONRPC handles only POST
    if (req->GetRequestMethod() != HTTPRequest::POST) {
        req->WriteReply(HTTP_BAD_METHOD, "JSONRPC server handles only POST requests");
        return false;
    }
    // Check authorization
    std::pair<bool, std::string> authHeader = req->GetHeader("authorization");
    if (!authHeader.first) {
        req->WriteReply(HTTP_UNAUTHORIZED);
        return false;
    }

    if (!RPCAuthorized(authHeader.second)) {
        LogPrintf("ThreadRPCServer incorrect password attempt from %s\n", req->GetPeer().ToString());

        /* Deter brute-forcing
           If this results in a DoS the user really
           shouldn't have their RPC port exposed. */
        MilliSleep(250);

        req->WriteReply(HTTP_UNAUTHORIZED);
        return false;
    }

    JSONRequest jreq; // Default id is NullUniValue
    try {
        // Parse request
        UniValue valRequest;
        if (!valRequest.read(req->ReadBody()))
            throw JSONRPCError(RPC_PARSE_ERROR, "Parse error");

        std::string strReply;
        // singleton request
        if (valRequest.isObject()) {
            jreq.parse(valRequest); // This might throw UniValue error if id is missing/wrong type

            UniValue result = tableRPC.execute(jreq.strMethod, jreq.params);

            // Send reply
            strReply = JSONRPCReply(result, NullUniValue, jreq.id);

        // array of requests
        } else if (valRequest.isArray())
            // Batch request: JSONRPCExecBatch handles parsing and execution internally.
            // It does not use the jreq object declared outside the try block.
            // The reply format for batch errors doesn't depend on a single id.
            strReply = JSONRPCExecBatch(valRequest.get_array());
        else
            throw JSONRPCError(RPC_PARSE_ERROR, "Top-level object parse error");

        req->WriteHeader("Content-Type", "application/json");
        req->WriteReply(HTTP_OK, strReply);
    } catch (const UniValue& objError) {
        // Use the id from jreq if it was parsed (for single requests), otherwise NullUniValue
        JSONErrorReply(req, objError, jreq.id);
        return false;
    } catch (const std::exception& e) {
        // Use the id from jreq if it was parsed (for single requests), otherwise NullUniValue
        JSONErrorReply(req, JSONRPCError(RPC_PARSE_ERROR, e.what()), jreq.id);
        return false;
    }
    return true;
}

static bool InitRPCAuthentication()
{
    if (mapArgs["-rpcpassword"] == "")
    {
        LogPrintf("No rpcpassword set - using random cookie authentication\n");
        if (!GenerateAuthCookie(&strRPCUserColonPass)) {
            uiInterface.ThreadSafeMessageBox(
                _("Error: A fatal internal error occurred, see debug.log for details"), // Same message as AbortNode
                "", CClientUIInterface::MSG_ERROR);
            return false;
        }
    } else {
        LogPrintf("Using rpcuser and rpcpassword authentication\n"); // Added log line
        strRPCUserColonPass = mapArgs["-rpcuser"] + ":" + mapArgs["-rpcpassword"];
    }
    if (strRPCUserColonPass.empty()) { // Extra check
         LogPrintf("Error: RPC authentication information is empty after initialization.\n");
         return false;
    }
    return true;
}

bool StartHTTPRPC()
{
    LogPrint("rpc", "Starting HTTP RPC server\n");
    if (!InitRPCAuthentication())
        return false;

    RegisterHTTPHandler("/", true, HTTPReq_JSONRPC);

    struct event_base* base = EventBase(); // Capture base locally
    if (!base) {
         LogPrintf("Error: Could not obtain event_base for HTTP RPC timer.\n");
         return false; // Or handle error appropriately
    }
    httpRPCTimerInterface = new HTTPRPCTimerInterface(base);
    RPCSetTimerInterface(httpRPCTimerInterface);
    LogPrint("rpc", "HTTP RPC server started.\n"); // Added confirmation
    return true;
}

void InterruptHTTPRPC()
{
    LogPrint("rpc", "Interrupting HTTP RPC server\n");
    // Implementation may depend on httpserver.cpp, often involves signaling the event loop
}

void StopHTTPRPC()
{
    LogPrint("rpc", "Stopping HTTP RPC server\n");
    UnregisterHTTPHandler("/", true);
    if (httpRPCTimerInterface) {
        RPCUnsetTimerInterface(httpRPCTimerInterface);
        delete httpRPCTimerInterface;
        httpRPCTimerInterface = 0;
    }
    LogPrint("rpc", "HTTP RPC server stopped.\n"); // Added confirmation
}