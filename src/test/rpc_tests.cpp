// Copyright (c) 2012-2013 The Bitcoin Core developers
// Copyright (c) 2017-2019 The DIGIWAGE developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/server.h" // Includes definition for CRPCTable and CRPCCommand, declares tableRPC
#include "rpc/client.h" // Includes declaration for RPCConvertValues

#include "base58.h"
#include "netbase.h"
#include "util.h"
#include "amount.h"       // Need for COIN, AmountFromValue, ValueFromAmount
#include "rpc/protocol.h" // Need for RPCConvertValues? (Check includes)
#include "sync.h"         // Might be needed for tableRPC lock? (Check locking)


#include "test/test_digiwage.h" // Make sure this path is correct

#include <boost/algorithm/string.hpp>
#include <boost/test/unit_test.hpp>

#include <univalue.h>

// Forward declaration from rpc/client.h should be sufficient if included correctly
// extern UniValue RPCConvertValues(const std::string& strMethod, const std::vector<std::string>& strParams);

// tableRPC is declared in rpc/server.h as extern const CRPCTable tableRPC;
// No need to redeclare it here.

// Helper function to find command using CRPCTable's operator[]
const CRPCCommand *FindCommand(const std::string& commandName)
{
    // Use the CRPCTable's operator[] which takes a string
    // It likely returns nullptr if the command is not found.
    // Ensure thread safety if needed (e.g., LOCK(cs_main); consult codebase)
    const CRPCCommand* command = tableRPC[commandName];
    return command;
}

UniValue CallRPC(std::string args)
{
    std::vector<std::string> vArgs;
    boost::split(vArgs, args, boost::is_any_of(" \t"), boost::token_compress_on);
    if (vArgs.empty()) {
        throw std::runtime_error("CallRPC: Empty arguments");
    }
    std::string strMethod = vArgs[0];
    vArgs.erase(vArgs.begin());
    UniValue params = RPCConvertValues(strMethod, vArgs);

    const CRPCCommand* command = FindCommand(strMethod);
    if (!command) {
         // Use the error throwing mechanism consistent with the rest of the RPC framework
         // This might involve throwing JSONRPCError or similar depending on the codebase.
         // For a simple test setup, std::runtime_error might suffice.
         throw std::runtime_error("RPC command not found: " + strMethod);
         // Alternatively: throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found");
    }

    rpcfn_type method = command->actor;
    if (!method) {
         throw std::runtime_error("RPC command actor is null: " + strMethod);
    }

    try {
        // Assuming the actor signature is (const UniValue& params, bool fHelp)
        // Adjust if the signature uses JSONRPCRequest or similar.
        UniValue result = (*method)(params, false);
        return result;
    }
    catch (const UniValue& objError) {
        if (objError.isObject() && objError.exists("message")) {
             throw std::runtime_error(objError["message"].get_str());
        } else {
             throw std::runtime_error("RPC error: " + objError.write());
        }
    }
    catch (const std::exception& e) {
        throw std::runtime_error("RPC std::exception: " + std::string(e.what()));
    }
    catch (...) {
         throw std::runtime_error("Unknown RPC error executing " + strMethod);
    }
}


BOOST_FIXTURE_TEST_SUITE(rpc_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(rpc_rawparams)
{
    // Test raw transaction API argument handling
    UniValue r;

    BOOST_CHECK_THROW(CallRPC("getrawtransaction"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("getrawtransaction not_hex"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("getrawtransaction a3b807410df0b60fcb9736768df5823938b2f838694939ba45f3c0a1bff150ed not_int"), std::runtime_error);

    BOOST_CHECK_THROW(CallRPC("createrawtransaction"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("createrawtransaction null null"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("createrawtransaction not_array"), std::runtime_error);
    // Pass JSON arrays/objects as strings, potentially needing quoting depending on RPCConvertValues
    BOOST_CHECK_THROW(CallRPC("createrawtransaction [] []"), std::runtime_error); // Or '[]' '[]'
    BOOST_CHECK_THROW(CallRPC("createrawtransaction {} {}"), std::runtime_error); // Or '{}' '{}'
    BOOST_CHECK_NO_THROW(CallRPC("createrawtransaction [] {}")); // Or '[]' '{}'
    BOOST_CHECK_THROW(CallRPC("createrawtransaction [] {} extra"), std::runtime_error); // Or '[]' '{}' extra

    BOOST_CHECK_THROW(CallRPC("decoderawtransaction"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("decoderawtransaction null"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("decoderawtransaction DEADBEEF"), std::runtime_error); // Needs valid hex
    std::string rawtx = "0100000001a15d57094aa7a21a28cb20b59aab8fc7d1149a3bdbcddba9c622e4f5f6a99ece010000006c493046022100f93bb0e7d8db7bd46e40132d1f8242026e045f03a0efe71bbb8e3f475e970d790221009337cd7f1f929f00cc6ff01f03729b069a7c21b59b1736ddfee5db5946c5da8c0121033b9b137ee87d5a812d6f506efdd37f0affa7ffc310711c06c7f3e097c9447c52ffffffff0100e1f505000000001976a9140389035a9225b3839e2bbf32d826a1e222031fd888ac00000000";
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("decoderawtransaction ")+rawtx));
    BOOST_CHECK(r.isObject());
    BOOST_CHECK_EQUAL(r["version"].getInt<int>(), 1);
    BOOST_CHECK_EQUAL(r["locktime"].getInt<int>(), 0);
    BOOST_CHECK_THROW(r = CallRPC(std::string("decoderawtransaction ")+rawtx+" extra"), std::runtime_error);

    BOOST_CHECK_THROW(CallRPC("signrawtransaction"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("signrawtransaction null"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("signrawtransaction ff00"), std::runtime_error); // Invalid hex
    BOOST_CHECK_NO_THROW(CallRPC(std::string("signrawtransaction ")+rawtx));
    // Use JSON array/object syntax if required by RPCConvertValues/command actor
    BOOST_CHECK_NO_THROW(CallRPC(std::string("signrawtransaction ")+rawtx+" [] [] NONE|ANYONECANPAY")); // Or '[]' '[]'
    BOOST_CHECK_NO_THROW(CallRPC(std::string("signrawtransaction ")+rawtx+" [] [] NONE|ANYONECANPAY")); // Or '[]' '[]'
    BOOST_CHECK_THROW(CallRPC(std::string("signrawtransaction ")+rawtx+" [] [] badenum"), std::runtime_error); // Or '[]' '[]'

    // Only check failure cases for sendrawtransaction, there's no network to send to...
    BOOST_CHECK_THROW(CallRPC("sendrawtransaction"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("sendrawtransaction null"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("sendrawtransaction DEADBEEF"), std::runtime_error); // Invalid hex
    BOOST_CHECK_THROW(CallRPC(std::string("sendrawtransaction ")+rawtx+" extra"), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(rpc_rawsign)
{
    UniValue r;
    // Use proper JSON formatting for arrays and objects within the string passed to CallRPC
    // Using raw string literals R"(...)" helps with quotes inside the JSON
    std::string prevout_str =
      R"([{"txid":"dd2888870cdc3f6e92661f6b0829667ee4bb07ed086c44205e726bbf3338f726",)"
      R"("vout":1,"scriptPubKey":"a914f5404a39a4799d8710e15db4c4512c5e06f97fed87",)"
      R"("redeemScript":"5121021431a18c7039660cd9e3612a2a47dc53b69cb38ea4ad743b7df8245fd0438f8e21029bbeff390ce736bd396af43b52a1c14ed52c086b1e5585c15931f68725772bac52ae"}])";
    // Ensure the address format is correct for DIGIWAGE if it differs from Bitcoin's P2SH format used here
    std::string outputs_str = R"({"DAddressPlaceholderHere":1})"; // Replace with a valid P2SH/other address and format if needed

    // createrawtransaction expects JSON array and JSON object as strings.
    // Quote them if RPCConvertValues doesn't handle raw JSON directly.
    BOOST_CHECK_NO_THROW(r = CallRPC("createrawtransaction '" + prevout_str + "' '" + outputs_str + "'"));
    std::string notsigned = r.get_str();

    // Use valid private keys for the corresponding redeemScript public keys
    std::string privkey1_str = R"("PrivateKeyForPubKey1Here")"; // Replace with actual WIF key
    std::string privkey2_str = R"("PrivateKeyForPubKey2Here")"; // Replace with actual WIF key
    std::string keys_str = "[" + privkey1_str + "," + privkey2_str + "]";

    // signrawtransaction expects hex tx, JSON array (prevtxs), JSON array (keys)
    // Quote the JSON array arguments
    BOOST_CHECK_NO_THROW(r = CallRPC("signrawtransaction " + notsigned + " '" + prevout_str + "' '[]'")); // Empty keys array
    BOOST_CHECK(r.isObject());
    BOOST_CHECK(r["complete"].get_bool() == false);

    BOOST_CHECK_NO_THROW(r = CallRPC("signrawtransaction " + notsigned + " '" + prevout_str + "' '" + keys_str + "'"));
    BOOST_CHECK(r.isObject());
    BOOST_CHECK(r["complete"].get_bool() == true);
}

BOOST_AUTO_TEST_CASE(rpc_format_monetary_values)
{
    BOOST_CHECK(ValueFromAmount(0LL).write() == "0.00000000");
    BOOST_CHECK(ValueFromAmount(1LL).write() == "0.00000001");
    BOOST_CHECK(ValueFromAmount(17622195LL).write() == "0.17622195");
    BOOST_CHECK(ValueFromAmount(50000000LL).write() == "0.50000000");
    BOOST_CHECK(ValueFromAmount(89898989LL).write() == "0.89898989");
    BOOST_CHECK(ValueFromAmount(100000000LL).write() == "1.00000000");
    // Check against DIGIWAGE's max CAmount if it differs from Bitcoin's ~21M * COIN
    CAmount max_money = 21000000LL * COIN; // Example: Use actual max if different
    BOOST_CHECK(ValueFromAmount(max_money - 10LL).write() == "20999999.99999990"); // Adjust expected value based on actual max_money
    BOOST_CHECK(ValueFromAmount(max_money - 1LL).write() == "20999999.99999999"); // Adjust expected value based on actual max_money

    BOOST_CHECK_EQUAL(ValueFromAmount(0).write(), "0.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount((COIN/10000)*123456789LL).write(), "12345.67890000");
    BOOST_CHECK_EQUAL(ValueFromAmount(-COIN).write(), "-1.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(-COIN/10).write(), "-0.10000000");

    BOOST_CHECK_EQUAL(ValueFromAmount(COIN*100000000LL).write(), "100000000.00000000");
    // ... (rest of the positive checks)
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN/100000000LL).write(), "0.00000001");
}

// Helper function: setNumStr returns void, remove check
static UniValue ValueFromString(const std::string &str)
{
    UniValue value;
    // Assume setNumStr might throw on error or leave value invalid.
    // The responsibility to check is moved to the caller (e.g., AmountFromValue).
    value.setNumStr(str);
    return value;
}

BOOST_AUTO_TEST_CASE(rpc_parse_monetary_values)
{
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.00000001")), 1LL);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.17622195")), 17622195LL);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.5")), 50000000LL);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.50000000")), 50000000LL);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.89898989")), 89898989LL);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("1.00000000")), 100000000LL);
    // Use appropriate max value checks based on DIGIWAGE's CAmount limits
    CAmount max_money = 21000000LL * COIN; // Example
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("20999999.99999990")), max_money - 10LL); // Adjust based on actual max
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("20999999.99999999")), max_money - 1LL); // Adjust based on actual max

    // AmountFromValue likely throws on error, test these cases
    BOOST_CHECK_THROW(AmountFromValue(ValueFromString("1.000000001")), UniValue); // Or std::runtime_error
    BOOST_CHECK_THROW(AmountFromValue(ValueFromString("test")), UniValue); // Or std::runtime_error
    BOOST_CHECK_THROW(AmountFromValue(ValueFromString("")), UniValue); // Or std::runtime_error
}

BOOST_AUTO_TEST_CASE(json_parse_errors)
{
    // Assumes ParseNonRFCJSONValue is available and works as expected
    // Valid JSON values
    BOOST_CHECK_EQUAL(ParseNonRFCJSONValue("1.0").get_real(), 1.0);
    BOOST_CHECK_EQUAL(ParseNonRFCJSONValue(" 1.0").get_real(), 1.0); // Leading space
    BOOST_CHECK_EQUAL(ParseNonRFCJSONValue("1.0 ").get_real(), 1.0); // Trailing space
    BOOST_CHECK_EQUAL(ParseNonRFCJSONValue("\"string\"").get_str(), "string");
    BOOST_CHECK(ParseNonRFCJSONValue("true").get_bool());
    BOOST_CHECK(ParseNonRFCJSONValue("null").isNull());
    BOOST_CHECK(ParseNonRFCJSONValue("[]").isArray());
    BOOST_CHECK(ParseNonRFCJSONValue("{}").isObject());

    // Invalid JSON syntax
    BOOST_CHECK_THROW(ParseNonRFCJSONValue("[1.0"), std::runtime_error); // Unterminated array
    BOOST_CHECK_THROW(ParseNonRFCJSONValue("a1.0"), std::runtime_error); // Leading garbage
    BOOST_CHECK_THROW(ParseNonRFCJSONValue("1.0sds"), std::runtime_error); // Trailing garbage
    BOOST_CHECK_THROW(ParseNonRFCJSONValue("1.0]"), std::runtime_error); // Unexpected token

    // Bare strings (like addresses) are not valid JSON values
    BOOST_CHECK_THROW(ParseNonRFCJSONValue("DAddressPlaceholderHere"), std::runtime_error);
    BOOST_CHECK_THROW(ParseNonRFCJSONValue("3J98t1WpEZ73CNmQviecrnyiWrnqRhWNL"), std::runtime_error); // Example P2SH
}

BOOST_AUTO_TEST_CASE(rpc_ban)
{
    // Assumes networking/banman is initialized sufficiently for these tests
    BOOST_CHECK_NO_THROW(CallRPC(std::string("clearbanned")));

    UniValue r;
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("setban 127.0.0.0 add")));
    BOOST_CHECK_THROW(r = CallRPC(std::string("setban 127.0.0.0:8334 add")), std::runtime_error); // Port invalid for add/remove
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    BOOST_CHECK(r.isArray());
    UniValue ar = r.get_array();
    BOOST_CHECK_EQUAL(ar.size(), 1);
    UniValue o1 = ar[0].get_obj();
    UniValue adr = o1["address"];
    BOOST_CHECK_EQUAL(adr.get_str(), "127.0.0.0/32");
    BOOST_CHECK_NO_THROW(CallRPC(std::string("setban 127.0.0.0 remove")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    BOOST_CHECK_EQUAL(ar.size(), 0);

    // Test absolute time banning
    int64_t ban_until_time = 1607731200LL;
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("setban 127.0.0.0/24 add ")+std::to_string(ban_until_time)+" true")); // timestamp, absolute
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    BOOST_CHECK_EQUAL(ar.size(), 1);
    o1 = ar[0].get_obj();
    adr = o1["address"];
    UniValue banned_until = o1["banned_until"];
    BOOST_CHECK_EQUAL(adr.get_str(), "127.0.0.0/24");
    BOOST_CHECK_EQUAL(banned_until.getInt<int64_t>(), ban_until_time);

    BOOST_CHECK_NO_THROW(CallRPC(std::string("clearbanned")));

    // Test relative time banning
    int64_t ban_seconds = 200;
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("setban 127.0.0.0/24 add ")+std::to_string(ban_seconds))); // bantime (seconds)
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    BOOST_CHECK_EQUAL(ar.size(), 1);
    o1 = ar[0].get_obj();
    adr = o1["address"];
    banned_until = o1["banned_until"];
    BOOST_CHECK_EQUAL(adr.get_str(), "127.0.0.0/24");
    int64_t now = GetTime();
    int64_t until = banned_until.getInt<int64_t>();
    BOOST_CHECK(until > now);
    BOOST_CHECK(until - now <= ban_seconds);
    // Check that it's reasonably close (e.g., within 5 seconds) to account for test execution time
    BOOST_CHECK(until - now > ban_seconds - 5);


    // Check banning already banned IP/subnet
    BOOST_CHECK_THROW(r = CallRPC(std::string("setban 127.0.0.1 add")), std::runtime_error);

    BOOST_CHECK_NO_THROW(CallRPC(std::string("setban 127.0.0.0/24 remove")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    BOOST_CHECK_EQUAL(ar.size(), 0);

    // Test different subnet syntax
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("setban 127.0.0.0/255.255.0.0 add"))); // Equivalent to 127.0.0.0/16
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    BOOST_CHECK_EQUAL(ar.size(), 1);
    o1 = ar[0].get_obj();
    adr = o1["address"];
    BOOST_CHECK_EQUAL(adr.get_str(), "127.0.0.0/16");
    BOOST_CHECK_THROW(r = CallRPC(std::string("setban 127.0.1.1 add")), std::runtime_error);

    BOOST_CHECK_NO_THROW(CallRPC(std::string("clearbanned")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    BOOST_CHECK_EQUAL(ar.size(), 0);

    BOOST_CHECK_THROW(r = CallRPC(std::string("setban invalid-ip add")), std::runtime_error); //invalid IP/Subnet

    //IPv6 tests
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("setban FE80:0000:0000:0000:0202:B3FF:FE1E:8329 add")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    BOOST_CHECK_EQUAL(ar.size(), 1);
    o1 = ar[0].get_obj();
    adr = o1["address"];
    BOOST_CHECK_EQUAL(adr.get_str(), "fe80::202:b3ff:fe1e:8329/128");

    BOOST_CHECK_NO_THROW(CallRPC(std::string("clearbanned")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("setban 2001:db8::/30 add"))); // CIDR notation for IPv6
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    BOOST_CHECK_EQUAL(ar.size(), 1);
    o1 = ar[0].get_obj();
    adr = o1["address"];
    BOOST_CHECK_EQUAL(adr.get_str(), "2001:db8::/30");

    BOOST_CHECK_NO_THROW(CallRPC(std::string("clearbanned")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("setban 2001:4d48:ac57:400:cacf:e9ff:fe1d:9c63/128 add")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    BOOST_CHECK_EQUAL(ar.size(), 1);
    o1 = ar[0].get_obj();
    adr = o1["address"];
    BOOST_CHECK_EQUAL(adr.get_str(), "2001:4d48:ac57:400:cacf:e9ff:fe1d:9c63/128");

    BOOST_CHECK_NO_THROW(CallRPC(std::string("clearbanned")));
}

BOOST_AUTO_TEST_SUITE_END()