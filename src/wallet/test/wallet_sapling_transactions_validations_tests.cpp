// Copyright (c) 2020 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "wallet/test/wallet_test_fixture.h"

#include "primitives/block.h"
#include "sapling/transaction_builder.h"
#include "sapling/sapling_operation.h"
#include "wallet/wallet.h"

#include <boost/test/unit_test.hpp>

/*
 * A text fixture with a preloaded 100-blocks regtest chain, with sapling activating at block 101,
 * and a wallet containing the key used for the coinbase outputs.
 */
struct TestSaplingChainSetup: public TestChain100Setup
{
    std::unique_ptr<CWallet> pwalletMain;

    TestSaplingChainSetup() : TestChain100Setup()
    {
        initZKSNARKS(); // init zk-snarks lib

        bool fFirstRun;
        pwalletMain = std::make_unique<CWallet>("testWallet", WalletDatabase::CreateMock());
        pwalletMain->LoadWallet(fFirstRun);
        RegisterValidationInterface(pwalletMain.get());

        int SAPLING_ACTIVATION_HEIGHT = 101;
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V3_0, SAPLING_ACTIVATION_HEIGHT);

        // setup wallet
        {
            LOCK(pwalletMain->cs_wallet);
            pwalletMain->SetMinVersion(FEATURE_SAPLING);
            gArgs.ForceSetArg("-keypool", "5");
            pwalletMain->SetupSPKM(true);

            // import coinbase key used to generate the 100-blocks chain
            BOOST_CHECK(pwalletMain->AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey()));
        }
        WalletRescanReserver reserver(pwalletMain.get());
        BOOST_CHECK(reserver.reserve());
        pwalletMain->RescanFromTime(0, reserver, true /* update */);
    }

    ~TestSaplingChainSetup()
    {
        UnregisterValidationInterface(pwalletMain.get());
    }
};

BOOST_FIXTURE_TEST_SUITE(wallet_sapling_transactions_validations_tests, TestSaplingChainSetup)

static SaplingOperation createOperationAndBuildTx(std::unique_ptr<CWallet>& pwallet,
                                                  std::vector<SendManyRecipient> recipients,
                                                  bool selectTransparentCoins)
{
    // Create the operation
    SaplingOperation operation(Params().GetConsensus(), pwallet.get());
    auto operationResult = operation.setRecipients(recipients)
            ->setSelectTransparentCoins(selectTransparentCoins)
            ->setSelectShieldedCoins(!selectTransparentCoins)
            ->build();
    BOOST_ASSERT_MSG(operationResult, operationResult.getError().c_str());

    CValidationState state;
    BOOST_ASSERT_MSG(
            CheckTransaction(operation.getFinalTx(), state, true),
            "Invalid Sapling transaction");
    return operation;
}

// Test double spend notes in the mempool and in blocks.
BOOST_AUTO_TEST_CASE(test_in_block_and_mempool_notes_double_spend)
{
    auto ret = pwalletMain->getNewAddress("coinbase");
    BOOST_CHECK(ret);
    CTxDestination coinbaseDest = *ret.getObjResult();
    BOOST_ASSERT_MSG(ret, "cannot create address");
    BOOST_ASSERT_MSG(IsValidDestination(coinbaseDest), "invalid destination");
    BOOST_ASSERT_MSG(IsMine(*pwalletMain, coinbaseDest), "destination not from wallet");

    // create the chain
    int tipHeight = WITH_LOCK(cs_main, return chainActive.Tip()->nHeight);
    BOOST_CHECK_EQUAL(tipHeight, 100);
    const CScript& scriptPubKey = GetScriptForDestination(coinbaseDest);
    int nGenerateBlocks = 110;
    for (int i = tipHeight; i < nGenerateBlocks; ++i) {
        CreateAndProcessBlock({}, scriptPubKey);
        SyncWithValidationInterfaceQueue();
    }

    // Verify that we are at block 110
    tipHeight = WITH_LOCK(cs_main, return chainActive.Tip()->nHeight);
    BOOST_CHECK_EQUAL(tipHeight, nGenerateBlocks);
    // Verify that the wallet has all of the coins
    BOOST_CHECK_EQUAL(pwalletMain->GetAvailableBalance(), CAmount(250 * COIN * 10)); // 10 blocks available
    BOOST_CHECK_EQUAL(pwalletMain->GetImmatureBalance(), CAmount(250 * COIN * 100)); // 100 blocks immature

    // Now that we have the chain, let's shield 100 WAGEs
    // single recipient
    std::vector<SendManyRecipient> recipients;
    libzcash::SaplingPaymentAddress pa = pwalletMain->GenerateNewSaplingZKey("sapling1");
    recipients.emplace_back(pa, CAmount(100 * COIN), "", false);

    // Create the operation and build the transaction
    SaplingOperation operation = createOperationAndBuildTx(pwalletMain, recipients, true);
    // broadcast the tx to the network
    std::string retHash;
    BOOST_ASSERT_MSG(operation.send(retHash), "error committing and broadcasting the transaction");

    // Generate a five blocks to fully confirm the tx and test balance
    for (int i = 1; i <= 5; ++i) {
        CreateAndProcessBlock({}, scriptPubKey, false /*fNoMempoolTx*/);
    }
    SyncWithValidationInterfaceQueue();
    BOOST_CHECK_EQUAL(pwalletMain->GetAvailableShieldedBalance(), CAmount(100 * COIN)); // 100 shield WAGEs
    BOOST_CHECK_EQUAL(pwalletMain->GetUnconfirmedShieldedBalance(), CAmount(0)); // 0 shield WAGEs

    // ##############################################
    // Context set!
    // Now let's try to double spend the same note twice in the same block

    // first generate a valid tx spending only one note
    // Create the operation and build the transaction
    auto res = pwalletMain->getNewAddress("receiveValid");
    BOOST_CHECK(res);
    CTxDestination tDest2 = *res.getObjResult();
    std::vector<SendManyRecipient> recipients2;
    recipients2.emplace_back(tDest2, CAmount(90 * COIN), false);
    SaplingOperation operation2 = createOperationAndBuildTx(pwalletMain, recipients2, false);

    // Create a second transaction that spends the same note with a different output now
    res = pwalletMain->getNewAddress("receiveInvalid");
    BOOST_CHECK(res);
    CTxDestination tDest3 = *res.getObjResult();;
    std::vector<SendManyRecipient> recipients3;
    recipients3.emplace_back(tDest3, CAmount(5 * COIN), false);
    SaplingOperation operation3 = createOperationAndBuildTx(pwalletMain, recipients3, false);

    // Now that both transactions were created, broadcast the first one
    std::string retTxHash2;
    BOOST_CHECK(operation2.send(retTxHash2));

    // Now broadcast the second one, this one should fail when tried to enter in the mempool as there is already another note spending the same nullifier
    std::string retTxHash3;
    auto opResult3 = operation3.send(retTxHash3);
    BOOST_CHECK(!opResult3);
    BOOST_CHECK(opResult3.getError().find("bad-txns-nullifier-double-spent"));

    // let's now test it inside a block
    // create the block with the two transactions and test validity
    const CBlock& block = CreateAndProcessBlock({operation3.getFinalTx()}, scriptPubKey, false /*fNoMempoolTx*/);
    SyncWithValidationInterfaceQueue();

    {
        LOCK(cs_main);
        // Same tip as before, no block connection
        BOOST_CHECK(chainActive.Tip()->GetBlockHash() != block.GetHash());
        BOOST_ASSERT_MSG(chainActive.Tip()->nHeight, 115);
    }
}

BOOST_AUTO_TEST_SUITE_END()
