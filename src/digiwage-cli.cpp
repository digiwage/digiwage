// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin developers
// Copyright (c) 2009-2015 The Dash developers
// Copyright (c) 2015-2019 The DIGIWAGE developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparamsbase.h"
#include "clientversion.h"
#include "rpc/client.h"
#include "rpc/protocol.h"
#include "util.h"
#include "utilstrencodings.h"

#include <boost/filesystem/operations.hpp>
#include <stdio.h>
#include <cmath> // For std::abs
#include <memory> // For std::unique_ptr

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/keyvalq_struct.h>

#include <univalue.h>

#define _(x) std::string(x) /* Keep the _() around in case gettext or such will be used later to translate non-UI */


static const int DEFAULT_HTTP_CLIENT_TIMEOUT=900;

// RAII wrapper structures for libevent (placed before usage)
struct BaseDeleter { void operator()(struct event_base* b) const { if(b) event_base_free(b); } };
struct EvConDeleter { void operator()(struct evhttp_connection* c) const { if(c) evhttp_connection_free(c); } };


std::string HelpMessageCli()
{
    std::string strUsage;
    strUsage += HelpMessageGroup(_("Options:"));
    strUsage += HelpMessageOpt("-?", _("This help message"));
    strUsage += HelpMessageOpt("-conf=<file>", strprintf(_("Specify configuration file (default: %s)"), "digiwage.conf"));
    strUsage += HelpMessageOpt("-datadir=<dir>", _("Specify data directory"));
    strUsage += HelpMessageOpt("-testnet", _("Use the test network"));
    strUsage += HelpMessageOpt("-regtest", _("Enter regression test mode, which uses a special chain in which blocks can be "
                                             "solved instantly. This is intended for regression testing tools and app development."));
    strUsage += HelpMessageOpt("-rpcconnect=<ip>", strprintf(_("Send commands to node running on <ip> (default: %s)"), "127.0.0.1"));
    strUsage += HelpMessageOpt("-rpcport=<port>", strprintf(_("Connect to JSON-RPC on <port> (default: %u or testnet: %u)"), 46002, 46006));
    strUsage += HelpMessageOpt("-rpcwait", _("Wait for RPC server to start"));
    strUsage += HelpMessageOpt("-rpcuser=<user>", _("Username for JSON-RPC connections"));
    strUsage += HelpMessageOpt("-rpcpassword=<pw>", _("Password for JSON-RPC connections"));
    strUsage += HelpMessageOpt("-rpcclienttimeout=<n>", strprintf(_("Timeout during HTTP requests (default: %d)"), DEFAULT_HTTP_CLIENT_TIMEOUT));

    return strUsage;
}

//////////////////////////////////////////////////////////////////////////////
//
// Start
//

//
// Exception thrown on connection error.  This error is used to determine
// when to wait if -rpcwait is given.
//
class CConnectionFailed : public std::runtime_error
{
public:
    explicit inline CConnectionFailed(const std::string& msg) : std::runtime_error(msg)
    {
    }
};

static bool AppInitRPC(int argc, char* argv[])
{
    //
    // Parameters
    //
    ParseParameters(argc, argv);
    if (argc < 2 || mapArgs.count("-?") || mapArgs.count("-help") || mapArgs.count("-version")) {
        std::string strUsage = _("DIGIWAGE Core RPC client version") + " " + FormatFullVersionWithCodename() + "\n";
        if (!mapArgs.count("-version")) {
            strUsage += "\n" + _("Usage:") + "\n" +
                        "  digiwage-cli [options] <command> [params]  " + _("Send command to DIGIWAGE Core") + "\n" +
                        "  digiwage-cli [options] help                " + _("List commands") + "\n" +
                        "  digiwage-cli [options] help <command>      " + _("Get help for a command") + "\n";

            strUsage += "\n" + HelpMessageCli();
        }

        fprintf(stdout, "%s", strUsage.c_str());
        return false;
    }
    if (!boost::filesystem::is_directory(GetDataDir(false))) {
        fprintf(stderr, "Error: Specified data directory \"%s\" does not exist.\n", mapArgs["-datadir"].c_str());
        return false;
    }
    try {
        ReadConfigFile(mapArgs, mapMultiArgs);
    } catch (const std::exception& e) {
        fprintf(stderr, "Error reading configuration file: %s\n", e.what());
        return false;
    }
    // Check for -testnet or -regtest parameter (BaseParams() calls are only valid after this clause)
    if (!SelectBaseParamsFromCommandLine()) {
        fprintf(stderr, "Error: Invalid combination of -regtest and -testnet.\n");
        return false;
    }
    if (GetBoolArg("-rpcssl", false))
    {
        fprintf(stderr, "Error: SSL mode for RPC (-rpcssl) is no longer supported.\n");
        return false;
    }
    return true;
}


/** Reply structure for request_done to fill in */
struct HTTPReply
{
    int status;
    std::string body;
};

static void http_request_done(struct evhttp_request *req, void *ctx)
{
    HTTPReply *reply = static_cast<HTTPReply*>(ctx);

    if (req == NULL) {
        /* If req is NULL, it means an error occurred while connecting, but
         * I'm not sure how to find out which one. We also don't really care.
         */
        reply->status = 0;
        return;
    }

    reply->status = evhttp_request_get_response_code(req);

    struct evbuffer *buf = evhttp_request_get_input_buffer(req);
    if (buf)
    {
        size_t size = evbuffer_get_length(buf);
        // Use evbuffer_pullup to get a contiguous block of memory
        const char *data = (const char*)evbuffer_pullup(buf, -1); // -1 reads the whole buffer
        if (data && size > 0) {
            reply->body.assign(data, size); // Use assign for clarity
        } else {
             reply->body.clear(); // Ensure body is empty if no data
        }
        evbuffer_drain(buf, size); // Drain the entire buffer
    }
    else {
         reply->body.clear(); // Ensure body is empty if no buffer
    }
}

UniValue CallRPC(const std::string& strMethod, const UniValue& params)
{
    std::string host = GetArg("-rpcconnect", "127.0.0.1");
    int port = GetArg("-rpcport", BaseParams().RPCPort());

    // Create event base
    struct event_base *base = event_base_new();
    if (!base)
        throw std::runtime_error("cannot create event_base");
    // RAII wrapper for event_base
    std::unique_ptr<event_base, BaseDeleter> base_ptr(base);


    // Synchronously look up hostname
    struct evhttp_connection *evcon = evhttp_connection_base_new(base, NULL, host.c_str(), port);
    if (evcon == NULL)
        throw std::runtime_error("create connection failed");
    // RAII wrapper for evhttp_connection
    std::unique_ptr<evhttp_connection, EvConDeleter> evcon_ptr(evcon);

    evhttp_connection_set_timeout(evcon, GetArg("-rpcclienttimeout", DEFAULT_HTTP_CLIENT_TIMEOUT));

    HTTPReply response;
    // No RAII needed for req as evhttp_make_request takes ownership,
    // and it's freed implicitly when evcon is freed or explicitly on error.
    struct evhttp_request *req = evhttp_request_new(http_request_done, (void*)&response);
    if (req == NULL)
        throw std::runtime_error("create http request failed");

    // Get credentials
    std::string strRPCUserColonPass;
    // Use GetArg for consistency, check if rpcpassword is set
    std::string rpcPassword = GetArg("-rpcpassword", "");
    if (rpcPassword == "") {
        // Try fall back to cookie-based authentication if no password is provided
        if (!GetAuthCookie(&strRPCUserColonPass)) {
            throw std::runtime_error(strprintf(
                 _("Could not locate RPC credentials. No authentication cookie could be found, and -rpcpassword is not set in the configuration file (%s)"),
                    GetConfigFile().string().c_str()));
        }
    } else {
        strRPCUserColonPass = GetArg("-rpcuser", "") + ":" + rpcPassword;
    }


    struct evkeyvalq *output_headers = evhttp_request_get_output_headers(req);
    assert(output_headers); // Should not be NULL if req is valid
    evhttp_add_header(output_headers, "Host", host.c_str());
    evhttp_add_header(output_headers, "Connection", "close");
    evhttp_add_header(output_headers, "Authorization", (std::string("Basic ") + EncodeBase64(strRPCUserColonPass)).c_str());
    evhttp_add_header(output_headers, "Content-Type", "application/json"); // Specify content type

    // Attach request data
    UniValue requestObj = JSONRPCRequestObj(strMethod, params, UniValue(1));
    std::string strRequest = requestObj.write(); // Convert the UniValue object to a JSON string
    struct evbuffer * output_buffer = evhttp_request_get_output_buffer(req);
    assert(output_buffer); // Should not be NULL if req is valid
    evbuffer_add(output_buffer, strRequest.data(), strRequest.size());

    // Make the request - evhttp_make_request takes ownership of req
    int r = evhttp_make_request(evcon, req, EVHTTP_REQ_POST, "/");
    req = nullptr; // Request ownership transferred
    if (r != 0) {
        // evcon_ptr handles cleanup
        // base_ptr handles cleanup
        throw CConnectionFailed("send http request failed");
    }

    // Dispatch events - this blocks until http_request_done is called or timeout/error
    event_base_dispatch(base);

    // RAII wrappers (evcon_ptr, base_ptr) handle cleanup automatically

    if (response.status == 0)
        throw CConnectionFailed("couldn't connect to server (possible timeout or connection refused)");
    else if (response.status == HTTP_UNAUTHORIZED) // Check specific code
        throw std::runtime_error("incorrect rpcuser or rpcpassword (authorization failed)");
    else if (response.status >= 400 && response.status != HTTP_BAD_REQUEST && response.status != HTTP_NOT_FOUND && response.status != HTTP_INTERNAL_SERVER_ERROR) // Check other client/server errors
        throw std::runtime_error(strprintf("server returned HTTP error %d", response.status));
    // FIX: Use HTTP_NOCONTENT instead of HTTP_NO_CONTENT
    else if (response.body.empty() && response.status != HTTP_NOCONTENT) // Allow 204 No Content
        throw std::runtime_error(strprintf("no response body from server (HTTP status %d)", response.status));

    // Parse reply
    UniValue valReply(UniValue::VOBJ); // Expect an object
    if (!response.body.empty()) { // Only parse if there's a body
         if (!valReply.read(response.body))
              throw std::runtime_error("couldn't parse JSON reply from server");
    } else if (response.status >= 400) {
         // If body is empty but status is error, construct a minimal error object
         throw std::runtime_error(strprintf("server returned HTTP error %d with empty body", response.status));
    }
    // If body is empty and status is success (e.g., 204), valReply remains an empty object, which is fine

    // Check for standard JSON-RPC response structure (result, error, id)
    // UniValue requires checking keys exist before accessing
    // FIX: Use exists() and operator[] instead of find_value
    bool hasResult = valReply.exists("result");
    bool hasError = valReply.exists("error");
    // Tolerate responses that might only have one or the other (e.g. error response)
    if (!hasResult && !hasError && !valReply.isNull()) { // Check if it's not just an empty success reply
        // Allow empty successful replies (e.g. from 'stop' command)
        if (!(response.status >= 200 && response.status < 300 && response.body.empty())) {
            throw std::runtime_error("reply from server does not contain result or error key");
        }
    }


    return valReply; // Return the whole parsed object
}

int CommandLineRPC(int argc, char* argv[])
{
    std::string strPrint;
    int nRet = 0;
    try {
        // Skip switches
        while (argc > 1 && IsSwitchChar(argv[1][0])) {
            argc--;
            argv++;
        }

        // Method
        if (argc < 2)
            throw std::runtime_error("too few parameters");
        std::string strMethod = argv[1];

        // Parameters default to strings
        std::vector<std::string> strParams(&argv[2], &argv[argc]);
        UniValue params = RPCConvertValues(strMethod, strParams);

        // Execute and handle connection failures with -rpcwait
        const bool fWait = GetBoolArg("-rpcwait", false);
        do {
            try {
                const UniValue reply = CallRPC(strMethod, params); // CallRPC now returns the whole reply object

                // FIX: Use exists() and operator[] instead of find_value
                const UniValue* error = nullptr;
                if (reply.exists("error") && !reply["error"].isNull()) {
                    error = &reply["error"];
                }

                const UniValue* result = nullptr;
                if (reply.exists("result")) { // Result can be null, so just check existence
                    result = &reply["result"];
                }


                if (error) { // Check if error pointer is non-null
                    // Error
                    const UniValue* codeVal = nullptr;
                    if (error->exists("code")) { // Find code within error object
                         codeVal = &(*error)["code"];
                    }

                    int code = RPC_INTERNAL_ERROR; // Default error code if not found or not int
                    if (codeVal && codeVal->isNum()) { // Check if it's a number (int or double)
                        try {
                            // Use getInt<int> as it's now a template
                            code = codeVal->getInt<int>();
                        } catch (const std::runtime_error& e) {
                             // Handle cases where it might be a double or out of int range if needed
                             fprintf(stderr, "Warning: Could not parse error code as int: %s\n", e.what());
                        } catch (const UniValue::type_error& e) { // Catch UniValue type errors too
                             fprintf(stderr, "Warning: Type error parsing error code: %s\n", e.what());
                        }
                    } else {
                        fprintf(stderr, "Warning: RPC error object lacks 'code' field or it's not a number.\n");
                    }

                    if (fWait && code == RPC_IN_WARMUP)
                        throw CConnectionFailed("server in warmup");

                    // Prefer the 'message' field if available, otherwise serialize the whole error object
                    const UniValue* messageVal = nullptr;
                    if(error->exists("message")) {
                        messageVal = &(*error)["message"];
                    }

                    if (messageVal && messageVal->isStr()) {
                        strPrint = "error code: " + std::to_string(code) + "\n" +
                                   "message: " + messageVal->get_str();
                    } else {
                        strPrint = "error: " + error->write(); // Fallback to full error object
                    }
                    nRet = std::abs(code); // Use std::abs from <cmath>
                    if (nRet == 0 && code != 0) nRet = 1; // Ensure non-zero exit code for non-zero error codes
                } else if (result) { // Check if result pointer is non-null
                    // Result
                    if (result->isNull()) // Handle explicit null result
                        strPrint = "null"; // Represent null explicitly
                    else if (result->isStr())
                        strPrint = result->get_str();
                    else
                        strPrint = result->write(2); // Pretty print other types
                } else {
                     // Success case, but result key is missing or error is missing/null.
                     // This might happen for commands like 'stop' which return success with no result/error.
                     // Check HTTP status to be sure? Or just assume success if no error object.
                     if (reply.isNull() || reply.empty()) { // Check if reply itself is null/empty (e.g. 204 No Content)
                        strPrint = ""; // No output for empty successful reply
                     } else {
                         // If reply is not empty, but lacks both result and error, it's unusual.
                         // Print the whole reply for debugging.
                         fprintf(stderr, "Warning: RPC reply lacks both 'result' and 'error' keys.\n");
                         strPrint = reply.write(2);
                         nRet = EXIT_FAILURE; // Treat as an error case
                     }
                }
                // Connection succeeded, no need to retry.
                break;
            } catch (const CConnectionFailed& e) {
                if (fWait) {
                    fprintf(stderr, "warning: %s\n", e.what()); // Print connection error when waiting
                    MilliSleep(1000);
                }
                else
                    throw; // Re-throw if not waiting
            }
        } while (fWait);
    } catch (const boost::thread_interrupted&) {
        // Allow thread interruptions to propagate.
        throw;
    } catch (const std::exception& e) {
        strPrint = std::string("error: ") + e.what();
        nRet = EXIT_FAILURE;
    } catch (...) {
        PrintExceptionContinue(NULL, "CommandLineRPC()");
        // Set failure code, but allow main to handle final exit
        nRet = EXIT_FAILURE;
        // Avoid throwing from catch(...) if possible, let main handle exit.
        // throw;
    }


    if (!strPrint.empty()) { // Only print if there's something to print
        fprintf((nRet == 0 ? stdout : stderr), "%s\n", strPrint.c_str());
    }
    return nRet;
}

int main(int argc, char* argv[])
{
    SetupEnvironment();
    if (!SetupNetworking()) {
        fprintf(stderr, "Error: Initializing networking failed\n");
        return EXIT_FAILURE; // Use EXIT_FAILURE for consistency
    }

    try {
        if (!AppInitRPC(argc, argv))
            return EXIT_FAILURE;
    } catch (const std::exception& e) {
        PrintExceptionContinue(&e, "AppInitRPC()");
        return EXIT_FAILURE;
    } catch (...) {
        PrintExceptionContinue(NULL, "AppInitRPC()");
        return EXIT_FAILURE;
    }

    int ret = EXIT_FAILURE; // Default to failure
    try {
        ret = CommandLineRPC(argc, argv);
    } catch (const std::exception& e) {
        // Error should have been printed by CommandLineRPC or PrintExceptionContinue
        PrintExceptionContinue(&e, "CommandLineRPC()");
        // ret remains EXIT_FAILURE
    } catch (...) {
        PrintExceptionContinue(NULL, "CommandLineRPC()");
        // ret remains EXIT_FAILURE
    }
    // Cleanup networking (optional, depends on SetupNetworking implementation)
    // ShutdownNetworking();
    return ret;
}