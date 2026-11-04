// Copyright (c) 2026 The DigiWage developers
// Distributed under the MIT software license.

#include <test/compatibility/extension_p2pkh_signed.json.h>

#include <clientversion.h>
#include <core_io.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <streams.h>
#include <test/util/json.h>
#include <test/util/script.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

#include <univalue.h>

namespace {

CTransaction DeserializeCompatibilityTransaction(
    const std::string& raw_transaction)
{
    CDataStream stream(
        ParseHex(raw_transaction),
        SER_NETWORK,
        PROTOCOL_VERSION
    );

    return CTransaction(deserialize, stream);
}

struct CompatibilityFixture {
    CTransaction transaction;
    std::map<COutPoint, CScript> scripts;
    std::map<COutPoint, CAmount> amounts;

    explicit CompatibilityFixture(const UniValue& fixture)
        : transaction(
              DeserializeCompatibilityTransaction(fixture[1].get_str()))
    {
        const UniValue& previous_outputs = fixture[0];

        for (unsigned int i = 0; i < previous_outputs.size(); ++i) {
            const UniValue& previous = previous_outputs[i];

            const COutPoint outpoint{
                uint256S(previous[0].get_str()),
                uint32_t(previous[1].getInt<int>())
            };

            scripts.emplace(
                outpoint,
                ([&]() {
                    const auto script_bytes = ParseHex(previous[2].get_str());
                    return CScript(script_bytes.begin(), script_bytes.end());
                }())
            );

            amounts.emplace(
                outpoint,
                previous[3].getInt<int64_t>()
            );
        }
    }
};

bool VerifyInput(
    const CTransaction& tx,
    unsigned int index,
    const CompatibilityFixture& fixture,
    ScriptError& error)
{
    const CTxIn& input = tx.vin.at(index);
    const CScript& previous_script =
        fixture.scripts.at(input.prevout);

    const CAmount amount =
        fixture.amounts.at(input.prevout);

    const PrecomputedTransactionData txdata(tx);

    const unsigned int flags =
        SCRIPT_VERIFY_P2SH |
        SCRIPT_VERIFY_DERSIG |
        SCRIPT_VERIFY_LOW_S |
        SCRIPT_VERIFY_STRICTENC;

    return VerifyScript(
        input.scriptSig,
        previous_script,
        &input.scriptWitness,
        flags,
        TransactionSignatureChecker(
            &tx,
            index,
            amount,
            txdata,
            MissingDataBehavior::ASSERT_FAIL
        ),
        &error
    );
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(
    extension_signature_compatibility_tests,
    BasicTestingSetup
)

BOOST_AUTO_TEST_CASE(core_accepts_extension_signatures)
{
    const UniValue fixture_json = read_json(
        std::string(
            json_tests::extension_p2pkh_signed,
            json_tests::extension_p2pkh_signed +
                sizeof(json_tests::extension_p2pkh_signed)
        )
    );

    BOOST_REQUIRE(fixture_json.isArray());
    BOOST_REQUIRE_EQUAL(fixture_json.size(), 3U);
    BOOST_REQUIRE(fixture_json[0].isArray());
    BOOST_REQUIRE(fixture_json[1].isStr());

    const CompatibilityFixture fixture(fixture_json);
    const CTransaction& tx = fixture.transaction;

    BOOST_REQUIRE_EQUAL(tx.vin.size(), 2U);
    BOOST_REQUIRE_EQUAL(tx.vout.size(), 1U);
    BOOST_REQUIRE_EQUAL(fixture.scripts.size(), 2U);
    BOOST_REQUIRE_EQUAL(fixture.amounts.size(), 2U);

    for (unsigned int i = 0; i < tx.vin.size(); ++i) {
        ScriptError error = SCRIPT_ERR_UNKNOWN_ERROR;

        const bool accepted =
            VerifyInput(tx, i, fixture, error);

        BOOST_CHECK_MESSAGE(
            accepted,
            "Core rejected extension signature in input " << i
        );

        BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);
    }

    for (unsigned int corrupted_index = 0;
         corrupted_index < tx.vin.size();
         ++corrupted_index) {

        CMutableTransaction modified(tx);

        CScript& script =
            modified.vin[corrupted_index].scriptSig;

        BOOST_REQUIRE_GT(script.size(), 10U);

        // Confirm the mutation target is inside the DER R integer.
        BOOST_REQUIRE_EQUAL(script[1], 0x30);
        BOOST_REQUIRE_EQUAL(script[3], 0x02);

        const size_t r_length = script[4];

        BOOST_REQUIRE_GT(r_length, 5U);
        BOOST_REQUIRE_LT(10U, 5U + r_length);

        // Modify a non-leading R byte. This preserves DER integer
        // structure while invalidating the original signature.
        script[10] ^= 0x01;

        const CTransaction corrupted_tx(modified);

        ScriptError error = SCRIPT_ERR_UNKNOWN_ERROR;

        const bool accepted = VerifyInput(
            corrupted_tx,
            corrupted_index,
            fixture,
            error
        );

        BOOST_CHECK_MESSAGE(
            !accepted,
            "Core accepted corrupted signature in input "
                << corrupted_index
        );

        BOOST_CHECK(error != SCRIPT_ERR_OK);
    }
}

BOOST_AUTO_TEST_SUITE_END()
