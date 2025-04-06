// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2017-2019 The DIGIWAGE developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"

#include "data/base58_encode_decode.json.h"
#include "data/base58_keys_invalid.json.h"
#include "data/base58_keys_valid.json.h"

#include "key.h"
#include "script/script.h"
#include "uint256.h"
#include "util.h"
#include "utilstrencodings.h"
#include "test/test_digiwage.h" // Make sure this path is correct for your project structure
#include "chainparams.h"      // Needed for SelectParams and CBaseChainParams

#include <boost/test/unit_test.hpp>

#include <univalue.h>

// Assuming read_json is defined elsewhere (e.g., in test/test_digiwage.cpp or a utility file)
extern UniValue read_json(const std::string& jsondata);

BOOST_FIXTURE_TEST_SUITE(base58_tests, BasicTestingSetup)

// Goal: test low-level base58 encoding functionality
BOOST_AUTO_TEST_CASE(base58_EncodeBase58)
{
    UniValue tests = read_json(std::string(json_tests::base58_encode_decode, json_tests::base58_encode_decode + sizeof(json_tests::base58_encode_decode)));
    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 2) // Allow for extra stuff (useful for comments)
        {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        std::vector<unsigned char> sourcedata = ParseHex(test[0].get_str());
        std::string base58string = test[1].get_str();
        BOOST_CHECK_MESSAGE(
                    EncodeBase58(sourcedata.data(), sourcedata.data() + sourcedata.size()) == base58string,
                    strTest);
    }
}

// Goal: test low-level base58 decoding functionality
BOOST_AUTO_TEST_CASE(base58_DecodeBase58)
{
    UniValue tests = read_json(std::string(json_tests::base58_encode_decode, json_tests::base58_encode_decode + sizeof(json_tests::base58_encode_decode)));
    std::vector<unsigned char> result;

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 2) // Allow for extra stuff (useful for comments)
        {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        std::vector<unsigned char> expected = ParseHex(test[0].get_str());
        std::string base58string = test[1].get_str();
        BOOST_CHECK_MESSAGE(DecodeBase58(base58string, result), strTest);
        BOOST_CHECK_MESSAGE(result.size() == expected.size() && std::equal(result.begin(), result.end(), expected.begin()), strTest);
    }

    BOOST_CHECK(!DecodeBase58("invalid", result));

    // check that DecodeBase58 skips whitespace, but still fails with unexpected non-whitespace at the end.
    BOOST_CHECK(!DecodeBase58(" \t\n\v\f\r skip \r\f\v\n\t a", result));
    BOOST_CHECK( DecodeBase58(" \t\n\v\f\r skip \r\f\v\n\t ", result));
    // Re-declare expected or clear result before this check if necessary, otherwise this reuses the last 'expected' from the loop
    std::vector<unsigned char> expectedWhitespace = ParseHex("971a55"); // Hex for "skip"
    BOOST_CHECK_EQUAL_COLLECTIONS(result.begin(), result.end(), expectedWhitespace.begin(), expectedWhitespace.end());
}

// Visitor to check address type
class TestAddrTypeVisitor : public boost::static_visitor<bool>
{
private:
    std::string exp_addrType;
public:
    TestAddrTypeVisitor(const std::string &exp_addrType) : exp_addrType(exp_addrType) { }
    bool operator()(const CKeyID &id) const
    {
        return (exp_addrType == "pubkey");
    }
    bool operator()(const CScriptID &id) const
    {
        return (exp_addrType == "script");
    }
    bool operator()(const CNoDestination &no) const
    {
        return (exp_addrType == "none");
    }
};

// Visitor to check address payload - Note: CBitcoinAddress doesn't store payload directly,
// Get() returns CTxDestination which holds CKeyID/CScriptID containing uint160.
// This visitor is useful if you extract the destination first.
class TestPayloadVisitor : public boost::static_visitor<bool>
{
private:
    std::vector<unsigned char> exp_payload;
public:
    TestPayloadVisitor(std::vector<unsigned char> &exp_payload) : exp_payload(exp_payload) { }
    bool operator()(const CKeyID &id) const
    {
        // Ensure payload is correct size for uint160 before constructing
        if (exp_payload.size() != sizeof(uint160)) return false;
        uint160 exp_key(exp_payload);
        return exp_key == id;
    }
    bool operator()(const CScriptID &id) const
    {
        // Ensure payload is correct size for uint160 before constructing
        if (exp_payload.size() != sizeof(uint160)) return false;
        uint160 exp_key(exp_payload);
        return exp_key == id;
    }
    bool operator()(const CNoDestination &no) const
    {
        return exp_payload.size() == 0;
    }
};


// Goal: check that parsed keys match test payload
BOOST_AUTO_TEST_CASE(base58_keys_valid_parse)
{
    UniValue tests = read_json(std::string(json_tests::base58_keys_valid, json_tests::base58_keys_valid + sizeof(json_tests::base58_keys_valid)));
    CBitcoinSecret secret;
    CBitcoinAddress addr;
    SelectParams(CBaseChainParams::MAIN); // Start with MAIN

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 3) // Allow for extra stuff (useful for comments)
        {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        std::string exp_base58string = test[0].get_str();
        std::vector<unsigned char> exp_payload = ParseHex(test[1].get_str());
        const UniValue &metadata = test[2].get_obj();

        // Use operator[] to access UniValue object members
        bool isPrivkey = metadata["isPrivkey"].get_bool();
        bool isTestnet = metadata["isTestnet"].get_bool();

        if (isTestnet)
            SelectParams(CBaseChainParams::TESTNET);
        else
            SelectParams(CBaseChainParams::MAIN);

        if(isPrivkey)
        {
            // Use operator[] for isCompressed
            bool isCompressed = metadata["isCompressed"].get_bool();

            // Must be valid private key
            // Note: CBitcoinSecret::SetString tests isValid, whereas CBitcoinAddress does not!
            BOOST_CHECK_MESSAGE(secret.SetString(exp_base58string), "!SetString:"+ strTest);
            BOOST_CHECK_MESSAGE(secret.IsValid(), "!IsValid:" + strTest);
            CKey privkey = secret.GetKey();
            BOOST_CHECK_MESSAGE(privkey.IsCompressed() == isCompressed, "compressed mismatch:" + strTest);
            BOOST_CHECK_MESSAGE(privkey.size() == exp_payload.size() && std::equal(privkey.begin(), privkey.end(), exp_payload.begin()), "key mismatch:" + strTest);

            // Private key must be invalid public key address
            addr.SetString(exp_base58string);
            BOOST_CHECK_MESSAGE(!addr.IsValid(), "IsValid privkey as pubkey:" + strTest);
        }
        else // is Public Key Address
        {
            // Use operator[] for addrType
            std::string exp_addrType = metadata["addrType"].get_str(); // "script" or "pubkey"

            // Must be valid public key address
            BOOST_CHECK_MESSAGE(addr.SetString(exp_base58string), "SetString:" + strTest);
            BOOST_CHECK_MESSAGE(addr.IsValid(), "!IsValid:" + strTest);
            BOOST_CHECK_MESSAGE(addr.IsScript() == (exp_addrType == "script"), "isScript mismatch" + strTest);
            CTxDestination dest = addr.Get();
            BOOST_CHECK_MESSAGE(boost::apply_visitor(TestAddrTypeVisitor(exp_addrType), dest), "addrType mismatch" + strTest);

            // Check payload matches
            // Need to extract the hash from the destination
            if (exp_addrType == "pubkey") {
                const CKeyID *keyID = boost::get<CKeyID>(&dest);
                BOOST_CHECK(keyID != nullptr);
                if (keyID) {
                     BOOST_CHECK_MESSAGE(std::vector<unsigned char>(keyID->begin(), keyID->end()) == exp_payload, "pubkey payload mismatch: " + strTest);
                }
            } else if (exp_addrType == "script") {
                 const CScriptID *scriptID = boost::get<CScriptID>(&dest);
                 BOOST_CHECK(scriptID != nullptr);
                 if (scriptID) {
                     BOOST_CHECK_MESSAGE(std::vector<unsigned char>(scriptID->begin(), scriptID->end()) == exp_payload, "script payload mismatch: " + strTest);
                 }
            }

            // Public key address must be invalid private key
            secret.SetString(exp_base58string);
            BOOST_CHECK_MESSAGE(!secret.IsValid(), "IsValid pubkey as privkey:" + strTest);
        }
    }
    SelectParams(CBaseChainParams::MAIN); // Reset to MAIN
}

// Goal: check that generated keys match test vectors
BOOST_AUTO_TEST_CASE(base58_keys_valid_gen)
{
    UniValue tests = read_json(std::string(json_tests::base58_keys_valid, json_tests::base58_keys_valid + sizeof(json_tests::base58_keys_valid)));
    SelectParams(CBaseChainParams::MAIN); // Start with MAIN

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 3) // Allow for extra stuff (useful for comments)
        {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        std::string exp_base58string = test[0].get_str();
        std::vector<unsigned char> exp_payload = ParseHex(test[1].get_str());
        const UniValue &metadata = test[2].get_obj();

        // Use operator[] to access UniValue object members
        bool isPrivkey = metadata["isPrivkey"].get_bool();
        bool isTestnet = metadata["isTestnet"].get_bool();

        if (isTestnet)
            SelectParams(CBaseChainParams::TESTNET);
        else
            SelectParams(CBaseChainParams::MAIN);

        if(isPrivkey)
        {
            // Use operator[] for isCompressed
            bool isCompressed = metadata["isCompressed"].get_bool();
            CKey key;
            // Ensure payload is correct size before Set
            if (exp_payload.size() == 32) { // Size of a private key
               key.Set(exp_payload.begin(), exp_payload.end(), isCompressed);
               BOOST_CHECK_MESSAGE(key.IsValid(), "key.Set failed: " + strTest);
               if(key.IsValid()) {
                  CBitcoinSecret secret;
                  secret.SetKey(key);
                  BOOST_CHECK_MESSAGE(secret.ToString() == exp_base58string, "result mismatch: " + strTest + " got " + secret.ToString());
               }
            } else {
                 BOOST_ERROR("Invalid private key payload size: " << strTest);
            }
        }
        else // is Public Key Address
        {
             // Use operator[] for addrType
            std::string exp_addrType = metadata["addrType"].get_str();
            CTxDestination dest;
            // Ensure payload is correct size for uint160 before constructing CKeyID/CScriptID
            if (exp_payload.size() == 20) { // Size of a hash160
                if(exp_addrType == "pubkey")
                {
                    dest = CKeyID(uint160(exp_payload));
                }
                else if(exp_addrType == "script")
                {
                    dest = CScriptID(uint160(exp_payload));
                }
                else if(exp_addrType == "none")
                {
                     // This case might not be hit if json only contains pubkey/script types
                    dest = CNoDestination();
                }
                else
                {
                    BOOST_ERROR("Bad addrtype: " << strTest);
                    continue;
                }

                CBitcoinAddress addrOut;
                // CNoDestination is not encodable as a CBitcoinAddress
                if (exp_addrType != "none") {
                   BOOST_CHECK_MESSAGE(addrOut.Set(dest), "encode dest: " + strTest);
                   BOOST_CHECK_MESSAGE(addrOut.ToString() == exp_base58string, "mismatch: " + strTest + " got " + addrOut.ToString());
                } else {
                    // Perhaps check that payload is empty for "none"?
                     BOOST_CHECK_MESSAGE(exp_payload.empty(), "Payload should be empty for addrType 'none': " + strTest);
                }
            } else if (exp_addrType == "none" && exp_payload.empty()) {
                 // Handle case where "none" type correctly has empty payload
                 dest = CNoDestination();
                 CBitcoinAddress addrOut;
                 BOOST_CHECK_MESSAGE(!addrOut.Set(dest), "Setting CNoDestination should fail: " + strTest);
            }
            else
            {
                 BOOST_ERROR("Invalid address payload size or type: " << strTest);
                 continue;
            }
        }
    }

    // Visiting a CNoDestination must fail
    CBitcoinAddress dummyAddr;
    CTxDestination nodest = CNoDestination();
    BOOST_CHECK(!dummyAddr.Set(nodest));

    SelectParams(CBaseChainParams::MAIN); // Reset to MAIN
}


// Goal: check that base58 parsing code is robust against a variety of corrupted data
BOOST_AUTO_TEST_CASE(base58_keys_invalid)
{
    UniValue tests = read_json(std::string(json_tests::base58_keys_invalid, json_tests::base58_keys_invalid + sizeof(json_tests::base58_keys_invalid))); // Negative testcases
    CBitcoinSecret secret;
    CBitcoinAddress addr;
    SelectParams(CBaseChainParams::MAIN); // Use mainnet params for parsing typically

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        std::string strTest = test.write();
        if (!test.isArray() || test.size() < 1) // Ensure it's an array and has at least one element
        {
            BOOST_ERROR("Bad test setup: " << strTest);
            continue;
        }
        std::string exp_base58string = test[0].get_str();

        // Test parsing with both mainnet and testnet parameters
        // Invalid keys should be invalid regardless of network parameters

        // Test with MAIN
        SelectParams(CBaseChainParams::MAIN);
        addr.SetString(exp_base58string);
        BOOST_CHECK_MESSAGE(!addr.IsValid(), "IsValid pubkey (MAIN):" + strTest);
        secret.SetString(exp_base58string);
        BOOST_CHECK_MESSAGE(!secret.IsValid(), "IsValid privkey (MAIN):" + strTest);

        // Test with TESTNET
        SelectParams(CBaseChainParams::TESTNET);
        addr.SetString(exp_base58string);
        BOOST_CHECK_MESSAGE(!addr.IsValid(), "IsValid pubkey (TESTNET):" + strTest);
        secret.SetString(exp_base58string);
        BOOST_CHECK_MESSAGE(!secret.IsValid(), "IsValid privkey (TESTNET):" + strTest);

    }
    SelectParams(CBaseChainParams::MAIN); // Reset to MAIN
}


BOOST_AUTO_TEST_SUITE_END()