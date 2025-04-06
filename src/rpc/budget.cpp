// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The DIGIWAGE developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "chainparams.h"
#include "db.h"
#include "init.h"
#include "main.h"
#include "masternode-budget.h"
#include "masternode-payments.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "messagesigner.h"
#include "rpc/server.h"
#include "utilmoneystr.h"
#include "util.h" // For SanitizeString, validateURL (assuming they are here)

#include <univalue.h>

#include <fstream>

// Helper function to convert proposal to JSON object
void budgetToJSON(CBudgetProposal* pbudgetProposal, UniValue& bObj)
{
    CTxDestination address1;
    ExtractDestination(pbudgetProposal->GetPayee(), address1);
    CBitcoinAddress address2(address1);

    bObj.pushKV("Name", pbudgetProposal->GetName());
    bObj.pushKV("URL", pbudgetProposal->GetURL());
    bObj.pushKV("Hash", pbudgetProposal->GetHash().ToString());
    bObj.pushKV("FeeHash", pbudgetProposal->nFeeTXHash.ToString());
    bObj.pushKV("BlockStart", (int64_t)pbudgetProposal->GetBlockStart());
    bObj.pushKV("BlockEnd", (int64_t)pbudgetProposal->GetBlockEnd());
    bObj.pushKV("TotalPaymentCount", (int64_t)pbudgetProposal->GetTotalPaymentCount());
    bObj.pushKV("RemainingPaymentCount", (int64_t)pbudgetProposal->GetRemainingPaymentCount());
    bObj.pushKV("PaymentAddress", address2.ToString());
    bObj.pushKV("Ratio", pbudgetProposal->GetRatio());
    bObj.pushKV("Yeas", (int64_t)pbudgetProposal->GetYeas());
    bObj.pushKV("Nays", (int64_t)pbudgetProposal->GetNays());
    bObj.pushKV("Abstains", (int64_t)pbudgetProposal->GetAbstains());
    bObj.pushKV("TotalPayment", ValueFromAmount(pbudgetProposal->GetAmount() * pbudgetProposal->GetTotalPaymentCount()));
    bObj.pushKV("MonthlyPayment", ValueFromAmount(pbudgetProposal->GetAmount()));
    bObj.pushKV("IsEstablished", pbudgetProposal->IsEstablished());

    std::string strError = "";
    bObj.pushKV("IsValid", pbudgetProposal->IsValid(strError));
    bObj.pushKV("IsValidReason", strError.c_str());
    bObj.pushKV("fValid", pbudgetProposal->fValid);
}

// Helper function to check budget inputs from RPC
void checkBudgetInputs(const UniValue& params, std::string &strProposalName, std::string &strURL,
                       int &nPaymentCount, int &nBlockStart, CBitcoinAddress &address, CAmount &nAmount)
{
    strProposalName = SanitizeString(params[0].get_str());
    if (strProposalName.size() > 20)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid proposal name, limit of 20 characters.");

    strURL = SanitizeString(params[1].get_str());
    std::string strErr;
    if (!validateURL(strURL, strErr)) // Ensure validateURL is available
        throw JSONRPCError(RPC_INVALID_PARAMETER, strErr);

    nPaymentCount = params[2].getInt<int>();
    if (nPaymentCount < 1)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid payment count, must be more than zero.");

    CBlockIndex* pindexPrev = nullptr;
    {
        LOCK(cs_main);
        pindexPrev = chainActive.Tip();
    }
    if (!pindexPrev)
        throw JSONRPCError(RPC_IN_WARMUP, "Try again after active chain is loaded");

    const int budgetCycleBlocks = Params().GetConsensus().nBudgetCycleBlocks;
    int pHeight = pindexPrev->nHeight;
    int nBlockMin = pHeight - (pHeight % budgetCycleBlocks) + budgetCycleBlocks;

    nBlockStart = params[3].getInt<int>();
    if ((nBlockStart < nBlockMin) || ((nBlockStart % budgetCycleBlocks) != 0))
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid block start %d - must be a budget cycle block. Next valid block: %d", nBlockStart, nBlockMin));

    address = CBitcoinAddress(params[4].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DIGIWAGE address");

    nAmount = AmountFromValue(params[5]);
    if (nAmount < 10 * COIN)
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid amount - Payment of %s is less than minimum 10 WAGE allowed", FormatMoney(nAmount)));

    CAmount nTotalBudget = budget.GetTotalBudget(nBlockStart);
    if (nAmount > nTotalBudget)
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid amount - Payment of %s more than max budget %s for block %d", FormatMoney(nAmount), FormatMoney(nTotalBudget), nBlockStart));
}

UniValue preparebudget(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 6)
        throw std::runtime_error(
            "preparebudget \"proposal-name\" \"url\" payment-count block-start \"digiwage-address\" monthy-payment\n"
            "\nPrepare proposal for network by signing and creating tx\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. \"proposal-name\":  (string, required) Desired proposal name (20 character limit)\n"
            "2. \"url\":            (string, required) URL of proposal details (64 character limit)\n"
            "3. payment-count:    (numeric, required) Total number of monthly payments\n"
            "4. block-start:      (numeric, required) Starting super block height\n"
            "5. \"digiwage-address\":   (string, required) DIGIWAGE address to send payments to\n"
            "6. monthly-payment:  (numeric, required) Monthly payment amount\n"

            "\nResult:\n"
            "\"xxxx\"       (string) proposal fee hash (if successful) or error message (if failed)\n"

            "\nExamples:\n" +
            HelpExampleCli("preparebudget", "\"test-proposal\" \"https://forum.digiwage.org/t/test-proposal\" 2 820800 \"D9oc6C3dttUbv8zd7zGNq1qKBGf4ZQ1XEE\" 500") +
            HelpExampleRpc("preparebudget", "\"test-proposal\", \"https://forum.digiwage.org/t/test-proposal\", 2, 820800, \"D9oc6C3dttUbv8zd7zGNq1qKBGf4ZQ1XEE\", 500"));

    if (!pwalletMain) {
        // *** FIX: Use RPC_WALLET_ERROR instead of RPC_WALLET_NOT_FOUND ***
        throw JSONRPCError(RPC_WALLET_ERROR, "This command requires a wallet, but wallet is not loaded.");
    }

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    std::string strProposalName;
    std::string strURL;
    int nPaymentCount;
    int nBlockStart;
    CBitcoinAddress address;
    CAmount nAmount;

    checkBudgetInputs(params, strProposalName, strURL, nPaymentCount, nBlockStart, address, nAmount);

    CScript scriptPubKey = GetScriptForDestination(address.Get());

    CBudgetProposalBroadcast budgetProposalBroadcast(strProposalName, strURL, nPaymentCount, scriptPubKey, nAmount, nBlockStart, UINT256_ZERO);

    std::string strError = "";
    if (!budgetProposalBroadcast.IsValid(strError, false))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Proposal data is not valid - " + strError);

    bool useIX = false;

    CWalletTx wtx;
    if (!pwalletMain->GetBudgetSystemCollateralTX(wtx, budgetProposalBroadcast.GetHash(), useIX)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error making collateral transaction for proposal. Check wallet balance and funds availability.");
    }

    CReserveKey reservekey(pwalletMain);

    if (!pwalletMain->CommitTransaction(wtx, reservekey, useIX ? "ix" : "tx")) {
         throw JSONRPCError(RPC_WALLET_ERROR, "Error committing collateral transaction");
    }

    return wtx.GetHash().ToString();
}

UniValue submitbudget(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 7)
        throw std::runtime_error(
            "submitbudget \"proposal-name\" \"url\" payment-count block-start \"digiwage-address\" monthly-payment \"fee-tx\"\n"
            "\nSubmit proposal to the network\n"

            "\nArguments:\n"
            "1. \"proposal-name\":  (string, required) Desired proposal name (20 character limit)\n"
            "2. \"url\":            (string, required) URL of proposal details (64 character limit)\n"
            "3. payment-count:    (numeric, required) Total number of monthly payments\n"
            "4. block-start:      (numeric, required) Starting super block height\n"
            "5. \"digiwage-address\":   (string, required) DIGIWAGE address to send payments to\n"
            "6. monthly-payment:  (numeric, required) Monthly payment amount\n"
            "7. \"fee-tx\":         (string, required) Transaction hash from preparebudget command\n"

            "\nResult:\n"
            "\"xxxx\"       (string) proposal hash (if successful) or error message (if failed)\n"

            "\nExamples:\n" +
            HelpExampleCli("submitbudget", "\"test-proposal\" \"https://forum.digiwage.org/t/test-proposal\" 2 820800 \"D9oc6C3dttUbv8zd7zGNq1qKBGf4ZQ1XEE\" 500 <fee_tx_hash>") +
            HelpExampleRpc("submitbudget", "\"test-proposal\", \"https://forum.digiwage.org/t/test-proposal\", 2, 820800, \"D9oc6C3dttUbv8zd7zGNq1qKBGf4ZQ1XEE\", 500, \"<fee_tx_hash>\""));

    std::string strProposalName;
    std::string strURL;
    int nPaymentCount;
    int nBlockStart;
    CBitcoinAddress address;
    CAmount nAmount;

    checkBudgetInputs(params, strProposalName, strURL, nPaymentCount, nBlockStart, address, nAmount);

    CScript scriptPubKey = GetScriptForDestination(address.Get());

    uint256 hash = ParseHashV(params[6], "parameter 7 (fee-tx)");

    CBudgetProposalBroadcast budgetProposalBroadcast(strProposalName, strURL, nPaymentCount, scriptPubKey, nAmount, nBlockStart, hash);

    std::string strError = "";
    int nConf = 0;
    if (!IsBudgetCollateralValid(hash, budgetProposalBroadcast.GetHash(), strError, budgetProposalBroadcast.nTime, nConf)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Proposal FeeTX is not valid - " + hash.ToString() + " - " + strError);
    }

    if (!masternodeSync.IsBlockchainSynced()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Must wait for client to sync with network. Try again in a minute or so.");
    }

    if (!budgetProposalBroadcast.IsValid(strError, true)) {
         throw JSONRPCError(RPC_INVALID_PARAMETER, "Proposal data is not valid - " + budgetProposalBroadcast.GetHash().ToString() + " - " + strError);
    }

    if(budget.AddProposal(budgetProposalBroadcast)) {
        budget.mapSeenMasternodeBudgetProposals.insert(std::make_pair(budgetProposalBroadcast.GetHash(), budgetProposalBroadcast));
        budgetProposalBroadcast.Relay();
        return budgetProposalBroadcast.GetHash().ToString();
    }

    throw JSONRPCError(RPC_INTERNAL_ERROR, "Invalid proposal submission, see debug.log for details.");
}

UniValue mnbudgetvote(const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 1) {
        strCommand = params[0].get_str();

        if (strCommand == "vote") strCommand = "local";
        if (strCommand == "vote-many") strCommand = "many";
        if (strCommand == "vote-alias") strCommand = "alias";
    }

    if (fHelp || params.size() < 3 || params.size() > 4 ||
        (strCommand != "local" && strCommand != "many" && strCommand != "alias") ||
        (strCommand == "local" && params.size() != 3) ||
        (strCommand == "many" && params.size() != 3) ||
        (strCommand == "alias" && params.size() != 4))
        throw std::runtime_error(
            "mnbudgetvote \"local|many|alias\" \"votehash\" \"yes|no\" ( \"alias\" )\n"
            "\nVote on a budget proposal\n"

            "\nArguments:\n"
            "1. \"mode\"      (string, required) The voting mode. 'local' for voting directly from a masternode, 'many' for voting with a MN controller and casting the same vote for each MN, 'alias' for voting with a MN controller and casting a vote for a single MN\n"
            "2. \"votehash\"  (string, required) The vote hash for the proposal\n"
            "3. \"votecast\"  (string, required) Your vote. 'yes' to vote for the proposal, 'no' to vote against\n"
            "4. \"alias\"     (string, required for 'alias' mode) The MN alias to cast a vote for.\n"

            "\nResult:\n"
            "{\n"
            "  \"overall\": \"xxxx\",      (string) The overall status message for the vote cast\n"
            "  \"detail\": [\n"
            "    {\n"
            "      \"node\": \"xxxx\",      (string) 'local' or the MN alias\n"
            "      \"result\": \"xxxx\",    (string) Either 'success' or 'failed' or 'skipped'\n"
            "      \"error\": \"xxxx\"      (string) Error message, if vote failed or skipped\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("mnbudgetvote", "\"local\" \"ed2f83cedee59a91406f5f47ec4d60bf5a7f9ee6293913c82976bd2d3a658041\" \"yes\"") +
            HelpExampleRpc("mnbudgetvote", "\"local\", \"ed2f83cedee59a91406f5f47ec4d60bf5a7f9ee6293913c82976bd2d3a658041\", \"yes\""));

    uint256 hash = ParseHashV(params[1], "parameter 2 (votehash)");
    std::string strVote = params[2].get_str();

    if (strVote != "yes" && strVote != "no")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "You can only vote 'yes' or 'no'");
    int nVote = VOTE_ABSTAIN;
    if (strVote == "yes") nVote = VOTE_YES;
    if (strVote == "no") nVote = VOTE_NO;

    int success = 0;
    int failed = 0;

    bool fNewSigs = false; // Assuming NewSigsActive is not used or needed

    UniValue resultsObj(UniValue::VARR);

    if (strCommand == "local") {
        if (!fMasterNode) throw std::runtime_error("This command can only be executed on a masternode");
        EnsureWalletIsUnlocked();

        CPubKey pubKeyMasternode;
        CKey keyMasternode;
        UniValue statusObj(UniValue::VOBJ);

        while (true) { // Removed break statements, rely on return/throw
            if (!CMessageSigner::GetKeysFromSecret(strMasterNodePrivKey, keyMasternode, pubKeyMasternode)) {
                failed++;
                statusObj.pushKV("node", "local");
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("error", "Masternode signing error, GetKeysFromSecret failed.");
                resultsObj.push_back(statusObj);
                goto end_local; // Use goto for single exit point
            }

            if (activeMasternode.vin == CTxIn()) {
                 failed++;
                 statusObj.pushKV("node", "local");
                 statusObj.pushKV("result", "failed");
                 statusObj.pushKV("error", "Local Masternode VIN not set.");
                 resultsObj.push_back(statusObj);
                 goto end_local;
            }

            CMasternode* pmn = mnodeman.Find(activeMasternode.vin);
            if (pmn == NULL) {
                failed++;
                statusObj.pushKV("node", "local");
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("error", "Failure to find local masternode in list : " + activeMasternode.vin.ToString());
                resultsObj.push_back(statusObj);
                goto end_local;
            }

            CBudgetVote vote(activeMasternode.vin, hash, nVote);
            if (!vote.Sign(keyMasternode, pubKeyMasternode, fNewSigs)) {
                failed++;
                statusObj.pushKV("node", "local");
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("error", "Failure to sign vote.");
                resultsObj.push_back(statusObj);
                goto end_local;
            }

            std::string strError = "";
            if (budget.UpdateProposal(vote, NULL, strError)) {
                budget.mapSeenMasternodeBudgetVotes.insert(std::make_pair(vote.GetHash(), vote));
                vote.Relay();
                success++;
                statusObj.pushKV("node", "local");
                statusObj.pushKV("result", "success");
                statusObj.pushKV("error", "");
            } else {
                failed++;
                statusObj.pushKV("node", "local");
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("error", "Error voting : " + strError);
            }
            resultsObj.push_back(statusObj);
            goto end_local; // Exit after processing
        }
        end_local:; // Label for goto

        UniValue returnObj(UniValue::VOBJ);
        returnObj.pushKV("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", success, failed));
        returnObj.pushKV("detail", resultsObj);

        return returnObj;
    }

    if (strCommand == "many") {
        EnsureWalletIsUnlocked();

        for (const auto& mne : masternodeConfig.getEntries()) {
            CPubKey pubKeyMasternode;
            CKey keyMasternode;
            UniValue statusObj(UniValue::VOBJ);

            if (!CMessageSigner::GetKeysFromSecret(mne.getPrivKey(), keyMasternode, pubKeyMasternode)) {
                failed++;
                statusObj.pushKV("node", mne.getAlias());
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("error", "Masternode signing error, could not set key correctly.");
                resultsObj.push_back(statusObj);
                continue;
            }

            CMasternode* pmn = mnodeman.Find(pubKeyMasternode);
            if (pmn == NULL) {
                failed++;
                statusObj.pushKV("node", mne.getAlias());
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("error", "Can't find masternode by pubkey. Make sure it's listed and running.");
                resultsObj.push_back(statusObj);
                continue;
            }

            CBudgetVote vote(pmn->vin, hash, nVote);
            if (!vote.Sign(keyMasternode, pubKeyMasternode, fNewSigs)) {
                failed++;
                statusObj.pushKV("node", mne.getAlias());
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("error", "Failure to sign vote.");
                resultsObj.push_back(statusObj);
                continue;
            }

            std::string strError = "";
            if (budget.UpdateProposal(vote, NULL, strError)) {
                budget.mapSeenMasternodeBudgetVotes.insert(std::make_pair(vote.GetHash(), vote));
                vote.Relay();
                success++;
                statusObj.pushKV("node", mne.getAlias());
                statusObj.pushKV("result", "success");
                statusObj.pushKV("error", "");
            } else {
                failed++;
                statusObj.pushKV("node", mne.getAlias());
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("error", strError.c_str());
            }

            resultsObj.push_back(statusObj);
        }

        UniValue returnObj(UniValue::VOBJ);
        returnObj.pushKV("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", success, failed));
        returnObj.pushKV("detail", resultsObj);

        return returnObj;
    }

    if (strCommand == "alias") {
        EnsureWalletIsUnlocked();
        std::string strAlias = params[3].get_str();

        bool fFound = false;
        for (const auto& mne : masternodeConfig.getEntries()) {

            if( strAlias != mne.getAlias()) continue;
            fFound = true;

            CPubKey pubKeyMasternode;
            CKey keyMasternode;
            UniValue statusObj(UniValue::VOBJ);

            if(!CMessageSigner::GetKeysFromSecret(mne.getPrivKey(), keyMasternode, pubKeyMasternode)){
                failed++;
                statusObj.pushKV("node", mne.getAlias());
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("error", "Masternode signing error, could not set key correctly.");
                resultsObj.push_back(statusObj);
                continue;
            }

            CMasternode* pmn = mnodeman.Find(pubKeyMasternode);
            if(pmn == NULL)
            {
                failed++;
                statusObj.pushKV("node", mne.getAlias());
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("error", "Can't find masternode by pubkey. Make sure it's listed and running.");
                resultsObj.push_back(statusObj);
                continue;
            }

            CBudgetVote vote(pmn->vin, hash, nVote);
            if(!vote.Sign(keyMasternode, pubKeyMasternode, fNewSigs)){
                failed++;
                statusObj.pushKV("node", mne.getAlias());
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("error", "Failure to sign vote.");
                resultsObj.push_back(statusObj);
                continue;
            }

            std::string strError = "";
            if(budget.UpdateProposal(vote, NULL, strError)) {
                budget.mapSeenMasternodeBudgetVotes.insert(std::make_pair(vote.GetHash(), vote));
                vote.Relay();
                success++;
                statusObj.pushKV("node", mne.getAlias());
                statusObj.pushKV("result", "success");
                statusObj.pushKV("error", "");
            } else {
                failed++;
                statusObj.pushKV("node", mne.getAlias());
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("error", strError.c_str());
            }
            resultsObj.push_back(statusObj);
            break; // Found and processed alias
        }

        if (!fFound) {
             UniValue statusObj(UniValue::VOBJ);
             statusObj.pushKV("node", strAlias);
             statusObj.pushKV("result", "failed");
             statusObj.pushKV("error", "Alias not found in masternode.conf");
             resultsObj.push_back(statusObj);
             failed++;
        }

        UniValue returnObj(UniValue::VOBJ);
        returnObj.pushKV("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", success, failed));
        returnObj.pushKV("detail", resultsObj);

        return returnObj;
    }

    return NullUniValue;
}

UniValue getbudgetvotes(const UniValue& params, bool fHelp)
{
    if (params.size() != 1)
        throw std::runtime_error(
            "getbudgetvotes \"proposal-name\"\n"
            "\nPrint vote information for a budget proposal\n"

            "\nArguments:\n"
            "1. \"proposal-name\":      (string, required) Name of the proposal\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"mnId\": \"xxxx\",        (string) Hash of the masternode's collateral transaction\n"
            "    \"nHash\": \"xxxx\",       (string) Hash of the vote\n"
            "    \"Vote\": \"YES|NO\",      (string) Vote cast ('YES' or 'NO')\n"
            "    \"nTime\": xxxx,         (numeric) Time in seconds since epoch the vote was cast\n"
            "    \"fValid\": true|false   (boolean) 'true' if the vote is valid, 'false' otherwise\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getbudgetvotes", "\"test-proposal\"") + HelpExampleRpc("getbudgetvotes", "\"test-proposal\""));

    std::string strProposalName = SanitizeString(params[0].get_str());

    UniValue ret(UniValue::VARR);

    CBudgetProposal* pbudgetProposal = budget.FindProposal(strProposalName);

    if (pbudgetProposal == NULL) throw std::runtime_error("Unknown proposal name: " + strProposalName);

    // *** FIX: Removed const from vote to allow calling non-const GetVoteString() ***
    for (auto& voteEntry : pbudgetProposal->mapVotes) { // Use range-based for with auto&
        UniValue bObj(UniValue::VOBJ);
        CBudgetVote& vote = voteEntry.second; // Get reference to vote
        bObj.pushKV("mnId", vote.vin.prevout.hash.ToString());
        bObj.pushKV("nHash", voteEntry.first.ToString());
        // *** FIX: Call GetVoteString() on non-const vote ***
        bObj.pushKV("Vote", vote.GetVoteString());
        bObj.pushKV("nTime", (int64_t)vote.nTime);
        bObj.pushKV("fValid", vote.fValid);

        ret.push_back(bObj);
    }

    return ret;
}

UniValue getnextsuperblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "getnextsuperblock\n"
            "\nPrint the next super block height\n"

            "\nResult:\n"
            "n      (numeric) Block height of the next super block\n"

            "\nExamples:\n" +
            HelpExampleCli("getnextsuperblock", "") + HelpExampleRpc("getnextsuperblock", ""));

    CBlockIndex* pindexPrev = nullptr;
    {
        LOCK(cs_main);
        pindexPrev = chainActive.Tip();
    }

    if (!pindexPrev) throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Chain not loaded or synced yet.");

    const int nBlocksPerCycle = Params().GetConsensus().nBudgetCycleBlocks;
    int nNext = pindexPrev->nHeight - pindexPrev->nHeight % nBlocksPerCycle + nBlocksPerCycle;
    return nNext;
}

UniValue getbudgetprojection(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "getbudgetprojection\n"
            "\nShow the projection of which proposals will be paid the next cycle\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"Name\": \"xxxx\",               (string) Proposal Name\n"
            "    \"URL\": \"xxxx\",                (string) Proposal URL\n"
            "    \"Hash\": \"xxxx\",               (string) Proposal vote hash\n"
            "    \"FeeHash\": \"xxxx\",            (string) Proposal fee hash\n"
            "    \"BlockStart\": n,              (numeric) Proposal starting block\n"
            "    \"BlockEnd\": n,                (numeric) Proposal ending block\n"
            "    \"TotalPaymentCount\": n,       (numeric) Number of payments\n"
            "    \"RemainingPaymentCount\": n,   (numeric) Number of remaining payments\n"
            "    \"PaymentAddress\": \"xxxx\",     (string) DIGIWAGE address of payment\n"
            "    \"Ratio\": x.xxx,               (numeric) Ratio of yeas vs nays\n"
            "    \"Yeas\": n,                    (numeric) Number of yea votes\n"
            "    \"Nays\": n,                    (numeric) Number of nay votes\n"
            "    \"Abstains\": n,                (numeric) Number of abstains\n"
            "    \"TotalPayment\": xxx.xxx,      (numeric) Total payment amount\n"
            "    \"MonthlyPayment\": xxx.xxx,    (numeric) Monthly payment amount\n"
            "    \"IsEstablished\": true|false,  (boolean) Established (true) or (false)\n"
            "    \"IsValid\": true|false,        (boolean) Valid (true) or Invalid (false)\n"
            "    \"IsValidReason\": \"xxxx\",      (string) Error message, if any\n"
            "    \"fValid\": true|false,         (boolean) Valid (true) or Invalid (false)\n"
            "    \"Allotted\": xxx.xxx,          (numeric) Amount allotted in current period\n"
            "    \"TotalBudgetAllotted\": xxx.xxx (numeric) Total alloted so far in projection\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getbudgetprojection", "") + HelpExampleRpc("getbudgetprojection", ""));

    UniValue ret(UniValue::VARR);
    CAmount nTotalAllotted = 0;

    std::vector<CBudgetProposal*> winningProps = budget.GetBudget();
    for (CBudgetProposal* pbudgetProposal : winningProps) {
        CAmount nAllotted = pbudgetProposal->GetAllotted();
        nTotalAllotted += nAllotted;

        UniValue bObj(UniValue::VOBJ);
        budgetToJSON(pbudgetProposal, bObj);
        bObj.pushKV("Alloted", ValueFromAmount(nAllotted));
        bObj.pushKV("TotalBudgetAllotted", ValueFromAmount(nTotalAllotted));

        ret.push_back(bObj);
    }

    return ret;
}

UniValue getbudgetinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "getbudgetinfo ( \"proposal\" )\n"
            "\nShow current masternode budgets\n"

            "\nArguments:\n"
            "1. \"proposal\"    (string, optional) Proposal name to show details for (if omitted, shows all valid proposals)\n"

            "\nResult (all proposals):\n"
            "[\n"
            "  { ... proposal info ... }\n"
            "  ,...\n"
            "]\n"
            "\nResult (single proposal):\n"
            "{ ... proposal info ... }\n"

            "\nProposal Info Structure:\n"
            "{\n"
            "    \"Name\": \"xxxx\",               (string) Proposal Name\n"
            "    \"URL\": \"xxxx\",                (string) Proposal URL\n"
            "    \"Hash\": \"xxxx\",               (string) Proposal vote hash\n"
            "    \"FeeHash\": \"xxxx\",            (string) Proposal fee hash\n"
            "    \"BlockStart\": n,              (numeric) Proposal starting block\n"
            "    \"BlockEnd\": n,                (numeric) Proposal ending block\n"
            "    \"TotalPaymentCount\": n,       (numeric) Number of payments\n"
            "    \"RemainingPaymentCount\": n,   (numeric) Number of remaining payments\n"
            "    \"PaymentAddress\": \"xxxx\",     (string) DIGIWAGE address of payment\n"
            "    \"Ratio\": x.xxx,               (numeric) Ratio of yeas vs nays\n"
            "    \"Yeas\": n,                    (numeric) Number of yea votes\n"
            "    \"Nays\": n,                    (numeric) Number of nay votes\n"
            "    \"Abstains\": n,                (numeric) Number of abstains\n"
            "    \"TotalPayment\": xxx.xxx,      (numeric) Total payment amount\n"
            "    \"MonthlyPayment\": xxx.xxx,    (numeric) Monthly payment amount\n"
            "    \"IsEstablished\": true|false,  (boolean) Established (true) or (false)\n"
            "    \"IsValid\": true|false,        (boolean) Valid (true) or Invalid (false)\n"
            "    \"IsValidReason\": \"xxxx\",      (string) Error message, if any\n"
            "    \"fValid\": true|false          (boolean) Valid (true) or Invalid (false)\n"
            "}\n"


            "\nExamples:\n" +
            HelpExampleCli("getbudgetinfo", "") + HelpExampleRpc("getbudgetinfo", "") +
            HelpExampleCli("getbudgetinfo", "\"test-proposal\"") + HelpExampleRpc("getbudgetinfo", "\"test-proposal\""));

    if (params.size() == 1) {
        std::string strProposalName = SanitizeString(params[0].get_str());
        CBudgetProposal* pbudgetProposal = budget.FindProposal(strProposalName);
        if (pbudgetProposal == NULL) throw std::runtime_error("Unknown proposal name: " + strProposalName);
        UniValue bObj(UniValue::VOBJ);
        budgetToJSON(pbudgetProposal, bObj);
        return bObj;
    } else {
        UniValue ret(UniValue::VARR);
        std::vector<CBudgetProposal*> winningProps = budget.GetAllProposals();
        for (CBudgetProposal* pbudgetProposal : winningProps) {
            UniValue bObj(UniValue::VOBJ);
            budgetToJSON(pbudgetProposal, bObj);
            ret.push_back(bObj);
        }
        return ret;
    }
}

UniValue mnbudgetrawvote(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 6)
        throw std::runtime_error(
            "mnbudgetrawvote \"masternode-tx-hash\" masternode-tx-index \"proposal-hash\" \"yes|no\" time \"vote-sig\"\n"
            "\nCompile and relay a proposal vote with provided external signature instead of signing vote internally\n"

            "\nArguments:\n"
            "1. \"masternode-tx-hash\"  (string, required) Transaction hash for the masternode\n"
            "2. masternode-tx-index   (numeric, required) Output index for the masternode\n"
            "3. \"proposal-hash\"       (string, required) Proposal vote hash\n"
            "4. \"yes|no\"              (string, required) Vote to cast ('yes' or 'no')\n"
            "5. time                  (numeric, required) Time since epoch in seconds\n"
            "6. \"vote-sig\"            (string, required) External base64-encoded signature\n"

            "\nResult:\n"
            "\"status\"     (string) Vote status ('Voted successfully') or error message\n"

            "\nExamples:\n" +
            HelpExampleCli("mnbudgetrawvote", "\"<tx_hash>\" 0 \"<prop_hash>\" \"yes\" 1588888888 \"<sig>\"") +
            HelpExampleRpc("mnbudgetrawvote", "\"<tx_hash>\", 0, \"<prop_hash>\", \"yes\", 1588888888, \"<sig>\""));

    uint256 hashMnTx = ParseHashV(params[0], "parameter 1 (masternode-tx-hash)");
    int nMnTxIndex = params[1].getInt<int>();
    CTxIn vin = CTxIn(hashMnTx, nMnTxIndex);

    uint256 hashProposal = ParseHashV(params[2], "parameter 3 (proposal-hash)");
    std::string strVote = params[3].get_str();

    if (strVote != "yes" && strVote != "no")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Vote must be 'yes' or 'no'");
    int nVote = VOTE_ABSTAIN;
    if (strVote == "yes") nVote = VOTE_YES;
    if (strVote == "no") nVote = VOTE_NO;

    int64_t nTime = params[4].getInt<int64_t>();
    std::string strSig = params[5].get_str();
    bool fInvalid = false;
    std::vector<unsigned char> vchSig = DecodeBase64(strSig.c_str(), &fInvalid);

    if (fInvalid)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Malformed base64 encoding for signature");

    CMasternode* pmn = mnodeman.Find(vin);
    if (pmn == NULL) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Failure to find masternode in list : " + vin.ToString());
    }

    CBudgetVote vote(vin, hashProposal, nVote);
    vote.nTime = nTime;
    vote.SetVchSig(vchSig);

    if (!vote.CheckSignature(pmn->pubKeyMasternode)) {
         // try old message version if applicable?
         // vote.nMessVersion = MessageVersion::MESS_VER_STRMESS;
         // if (!vote.CheckSignature(pmn->pubKeyMasternode))
            throw JSONRPCError(RPC_INVALID_PARAMETER,"Failure to verify signature.");
    }

    std::string strError = "";
    if (budget.UpdateProposal(vote, NULL, strError)) {
        budget.mapSeenMasternodeBudgetVotes.insert(std::make_pair(vote.GetHash(), vote));
        vote.Relay();
        return "Voted successfully";
    } else {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Error voting : " + strError);
    }
}

UniValue mnfinalbudget(const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    if (fHelp || params.size() < 1 ||
        (strCommand != "suggest" && strCommand != "vote-many" && strCommand != "vote" && strCommand != "show" && strCommand != "getvotes") ||
        (strCommand == "vote-many" && params.size() != 2) ||
        (strCommand == "vote" && params.size() != 2) ||
        (strCommand == "show" && params.size() != 1) ||
        (strCommand == "getvotes" && params.size() != 2) ||
        (strCommand == "suggest" && params.size() != 1)
       )
        throw std::runtime_error(
            "mnfinalbudget \"command\" ( \"hash\" )\n"
            "\nVote, show, or suggest finalized budgets\n"

            "\nAvailable commands:\n"
            "  suggest     - Show the suggested proposals for the next finalized budget.\n"
            "  vote-many BUDGET_HASH - Vote on a finalized budget with all masternodes from masternode.conf.\n"
            "  vote BUDGET_HASH      - Vote on a finalized budget with the local masternode.\n"
            "  show                  - Show all existing finalized budgets.\n"
            "  getvotes BUDGET_HASH  - Get vote information for a finalized budget.\n"
            + HelpRequiringPassphrase());

    bool fNewSigs = false;

    if (strCommand == "suggest") {
        // Placeholder - Needs implementation
        return "Suggest command not yet implemented";
    }

    if (strCommand == "vote-many") {
        EnsureWalletIsUnlocked();

        std::string strHash = params[1].get_str();
        uint256 hash(uint256S(strHash));

        int success = 0;
        int failed = 0;

        UniValue resultsObj(UniValue::VOBJ);

        for (const auto& mne : masternodeConfig.getEntries()) {
            CPubKey pubKeyMasternode;
            CKey keyMasternode;
            UniValue statusObj(UniValue::VOBJ);

            if (!CMessageSigner::GetKeysFromSecret(mne.getPrivKey(), keyMasternode, pubKeyMasternode)) {
                failed++;
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("error", "Masternode signing error, could not set key correctly.");
                resultsObj.pushKV(mne.getAlias(), statusObj);
                continue;
            }

            CMasternode* pmn = mnodeman.Find(pubKeyMasternode);
            if (pmn == NULL) {
                failed++;
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("error", "Can't find masternode by pubkey.");
                resultsObj.pushKV(mne.getAlias(), statusObj);
                continue;
            }


            CFinalizedBudgetVote vote(pmn->vin, hash);
            if (!vote.Sign(keyMasternode, pubKeyMasternode, fNewSigs)) {
                failed++;
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("error", "Failure to sign.");
                resultsObj.pushKV(mne.getAlias(), statusObj);
                continue;
            }

            std::string strError = "";
            if (budget.UpdateFinalizedBudget(vote, NULL, strError)) {
                budget.mapSeenFinalizedBudgetVotes.insert(std::make_pair(vote.GetHash(), vote));
                vote.Relay();
                success++;
                statusObj.pushKV("result", "success");
            } else {
                failed++;
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("error", strError);
            }
            resultsObj.pushKV(mne.getAlias(), statusObj);
        }

        UniValue returnObj(UniValue::VOBJ);
        returnObj.pushKV("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", success, failed));
        returnObj.pushKV("detail", resultsObj);

        return returnObj;
    }

    if (strCommand == "vote") {
         if (!fMasterNode) throw std::runtime_error("This command can only be executed on a masternode");
         EnsureWalletIsUnlocked();

        std::string strHash = params[1].get_str();
        uint256 hash(uint256S(strHash));

        CPubKey pubKeyMasternode;
        CKey keyMasternode;

        if (!CMessageSigner::GetKeysFromSecret(strMasterNodePrivKey, keyMasternode, pubKeyMasternode))
             throw JSONRPCError(RPC_INTERNAL_ERROR, "Error upon calling GetKeysFromSecret");

        if (activeMasternode.vin == CTxIn()) {
             throw JSONRPCError(RPC_INTERNAL_ERROR, "Local Masternode VIN not set.");
        }

        CMasternode* pmn = mnodeman.Find(activeMasternode.vin);
        if (pmn == NULL) {
            throw JSONRPCError(RPC_INTERNAL_ERROR,"Failure to find local masternode in list : " + activeMasternode.vin.ToString());
        }

        CFinalizedBudgetVote vote(activeMasternode.vin, hash);
        if (!vote.Sign(keyMasternode, pubKeyMasternode, fNewSigs)) {
            throw JSONRPCError(RPC_INTERNAL_ERROR,"Failure to sign vote.");
        }

        std::string strError = "";
        if (budget.UpdateFinalizedBudget(vote, NULL, strError)) {
            budget.mapSeenFinalizedBudgetVotes.insert(std::make_pair(vote.GetHash(), vote));
            vote.Relay();
            return "success";
        } else {
            throw JSONRPCError(RPC_INTERNAL_ERROR,"Error voting : " + strError);
        }
    }

    if (strCommand == "show") {
        UniValue resultObj(UniValue::VOBJ);

        std::vector<CFinalizedBudget*> winningFbs = budget.GetFinalizedBudgets();
        for (CFinalizedBudget* finalizedBudget : winningFbs) {
            UniValue bObj(UniValue::VOBJ);
            bObj.pushKV("FeeTX", finalizedBudget->nFeeTXHash.ToString());
            bObj.pushKV("Hash", finalizedBudget->GetHash().ToString());
            bObj.pushKV("BlockStart", (int64_t)finalizedBudget->GetBlockStart());
            bObj.pushKV("BlockEnd", (int64_t)finalizedBudget->GetBlockEnd());

            UniValue props(UniValue::VARR);
            // *** FIX: Correct loop for GetProposals() which returns std::vector<uint256> ***
             for(const uint256& propHash : finalizedBudget->GetProposals()){
                 props.push_back(propHash.ToString()); // Call ToString() on uint256
             }
            bObj.pushKV("Proposals", props);
            bObj.pushKV("VoteCount", (int64_t)finalizedBudget->GetVoteCount());
            // *** FIX: Use GetStatus() instead of GetStatusString() ***
            bObj.pushKV("Status", finalizedBudget->GetStatus()); // Use integer status code

            std::string strError = "";
            bObj.pushKV("IsValid", finalizedBudget->IsValid(strError));
            bObj.pushKV("IsValidReason", strError.c_str());

            resultObj.pushKV(finalizedBudget->GetName(), bObj);
        }

        return resultObj;
    }

    if (strCommand == "getvotes") {
        std::string strHash = params[1].get_str();
        uint256 hash(uint256S(strHash));

        UniValue ret(UniValue::VARR);

        CFinalizedBudget* pfinalBudget = budget.FindFinalizedBudget(hash);

        if (pfinalBudget == NULL) throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown finalized budget hash");

        for (const auto& voteEntry : pfinalBudget->mapVotes) {
            UniValue bObj(UniValue::VOBJ);
            const CFinalizedBudgetVote& vote = voteEntry.second;
            bObj.pushKV("mnId", vote.vin.prevout.hash.ToString());
            bObj.pushKV("nHash", voteEntry.first.ToString());
            bObj.pushKV("nTime", (int64_t)vote.nTime);
            bObj.pushKV("fValid", vote.fValid);

            ret.push_back(bObj);
        }

        return ret;
    }

    return NullUniValue;
}

UniValue checkbudgets(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "checkbudgets\n"
            "\nInitiates a budget check cycle manually (used for testing or diagnostics)\n"

            "\nResult:\n"
            "null\n"

            "\nExamples:\n" +
            HelpExampleCli("checkbudgets", "") + HelpExampleRpc("checkbudgets", ""));

    budget.CheckAndRemove();

    return NullUniValue;
}