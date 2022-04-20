// Copyright (c) 2021 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "budget/budgetutil.h"

#include "budget/budgetmanager.h"
#include "masternodeman.h"
#include "masternodeconfig.h"
#include "util/validation.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h" // future: use interface instead.
#endif


static UniValue packRetStatus(const std::string& nodeType, const std::string& result, const std::string& error)
{
    UniValue statusObj(UniValue::VOBJ);
    statusObj.pushKV("node", nodeType);
    statusObj.pushKV("result", result);
    statusObj.pushKV("error", error);
    return statusObj;
}

static UniValue packErrorRetStatus(const std::string& nodeType, const std::string& error)
{
    return packRetStatus(nodeType, "failed", error);
}

static UniValue packVoteReturnValue(const UniValue& details, int success, int failed)
{
    UniValue returnObj(UniValue::VOBJ);
    returnObj.pushKV("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", success, failed));
    returnObj.pushKV("detail", details);
    return returnObj;
}

// key, alias and collateral outpoint of a masternode. Struct used to sign proposal/budget votes
struct MnKeyData
{
    std::string mnAlias;
    const COutPoint* collateralOut;

    MnKeyData() = delete;
    MnKeyData(const std::string& _mnAlias, const COutPoint* _collateralOut, const CKey& _key):
        mnAlias(_mnAlias),
        collateralOut(_collateralOut),
        key(_key),
        use_bls(false)
    {}
    MnKeyData(const std::string& _mnAlias, const COutPoint* _collateralOut, const CBLSSecretKey& _key):
        mnAlias(_mnAlias),
        collateralOut(_collateralOut),
        blsKey(_key),
        use_bls(true)
    {}

    bool Sign(CSignedMessage* msg) const
    {
        return use_bls ? msg->Sign(blsKey)
                       : msg->Sign(key, key.GetPubKey().GetID());
    }

private:
    CKey key;
    CBLSSecretKey blsKey;
    bool use_bls;   // whether to use a CKey (mbv) or blsKey (fbv, mnw) to sign
};

typedef std::list<MnKeyData> mnKeyList;

static UniValue voteProposal(const uint256& propHash, const CBudgetVote::VoteDirection& nVote,
                             const mnKeyList& mnKeys, UniValue resultsObj, int failed)
{
    int success = 0;
    for (const auto& k : mnKeys) {
        CBudgetVote vote(CTxIn(*k.collateralOut), propHash, nVote);
        if (!k.Sign(&vote)) {
            resultsObj.push_back(packErrorRetStatus(k.mnAlias, "Failure to sign."));
            failed++;
            continue;
        }
        CValidationState state;
        if (!g_budgetman.ProcessProposalVote(vote, nullptr, state)) {
            resultsObj.push_back(packErrorRetStatus(k.mnAlias, FormatStateMessage(state)));
            failed++;
            continue;
        }
        resultsObj.push_back(packRetStatus(k.mnAlias, "success", ""));
        success++;
    }

    return packVoteReturnValue(resultsObj, success, failed);
}

static UniValue voteFinalBudget(const uint256& budgetHash,
                                const mnKeyList& mnKeys, UniValue resultsObj, int failed)
{
    int success = 0;
    for (const auto& k : mnKeys) {
        CFinalizedBudgetVote vote(CTxIn(*k.collateralOut), budgetHash);
        if (!k.Sign(&vote)) {
            resultsObj.push_back(packErrorRetStatus(k.mnAlias, "Failure to sign."));
            failed++;
            continue;
        }
        CValidationState state;
        if (!g_budgetman.ProcessFinalizedBudgetVote(vote, nullptr, state)) {
            resultsObj.push_back(packErrorRetStatus(k.mnAlias, FormatStateMessage(state)));
            failed++;
            continue;
        }
        resultsObj.push_back(packRetStatus(k.mnAlias, "success", ""));
        success++;
    }

    return packVoteReturnValue(resultsObj, success, failed);
}

// Legacy masternodes
static mnKeyList getMNKeys(const Optional<std::string>& mnAliasFilter,
                           UniValue& resultsObj, int& failed)
{
    mnKeyList mnKeys;
    for (const CMasternodeConfig::CMasternodeEntry& mne : masternodeConfig.getEntries()) {
        if (mnAliasFilter && *mnAliasFilter != mne.getAlias()) continue;
        CKey mnKey; CPubKey mnPubKey;
        const std::string& mnAlias = mne.getAlias();
        if (!CMessageSigner::GetKeysFromSecret(mne.getPrivKey(), mnKey, mnPubKey)) {
            resultsObj.push_back(packErrorRetStatus(mnAlias, "Could not get key from masternode.conf"));
            failed++;
            continue;
        }
        CMasternode* pmn = mnodeman.Find(mnPubKey);
        if (!pmn) {
            resultsObj.push_back(packErrorRetStatus(mnAlias, "Can't find masternode by pubkey"));
            failed++;
            continue;
        }
        mnKeys.emplace_back(mnAlias, &pmn->vin.prevout, mnKey);
    }
    return mnKeys;
}

static mnKeyList getMNKeysForActiveMasternode(UniValue& resultsObj)
{
    // local node must be a masternode
    if (!fMasterNode) {
        throw std::runtime_error(_("This is not a masternode. 'local' option disabled."));
    }

    if (activeMasternode.vin == nullopt) {
        throw std::runtime_error(_("Active Masternode not initialized."));
    }

    CKey mnKey; CPubKey mnPubKey;
    activeMasternode.GetKeys(mnKey, mnPubKey);
    CMasternode* pmn = mnodeman.Find(mnPubKey);
    if (!pmn) {
        resultsObj.push_back(packErrorRetStatus("local", "Can't find masternode by pubkey"));
        return mnKeyList();
    }

    return {MnKeyData("local", &pmn->vin.prevout, mnKey)};
}

// Deterministic masternodes
static mnKeyList getDMNVotingKeys(CWallet* const pwallet, const Optional<std::string>& mnAliasFilter, bool fFinal, UniValue& resultsObj, int& failed)
{
    if (!pwallet) {
        throw std::runtime_error( "Wallet (with voting key) not found.");
    }

    auto mnList = deterministicMNManager->GetListAtChainTip();

    CDeterministicMNCPtr mnFilter{nullptr};
    if (mnAliasFilter) {
        // vote with a single masternode (identified by ProTx)
        const uint256& proTxHash = uint256S(*mnAliasFilter);
        mnFilter = mnList.GetValidMN(proTxHash);
        if (!mnFilter) {
            resultsObj.push_back(packErrorRetStatus(*mnAliasFilter, "Invalid or unknown proTxHash"));
            failed++;
            return {};
        }
    }

    mnKeyList mnKeys;
    mnList.ForEachMN(true, [&](const CDeterministicMNCPtr& dmn) {
        bool filtered = mnFilter && dmn->proTxHash == mnFilter->proTxHash;
        if (!mnFilter || filtered) {
            if (fFinal) {
                // We should never get here. BLS operator key (for active mn) is needed.
                throw std::runtime_error("Finalized budget voting is allowed only locally, from the masternode");
            }
            // Get voting key from the wallet
            LOCK(pwallet->cs_wallet);
            CKey mnKey;
            if (pwallet->GetKey(dmn->pdmnState->keyIDVoting, mnKey)) {
                mnKeys.emplace_back(dmn->proTxHash.ToString(), &dmn->collateralOutpoint, mnKey);
            } else if (filtered) {
                resultsObj.push_back(packErrorRetStatus(*mnAliasFilter, strprintf(
                                        "Private key for voting address %s not known by this wallet",
                                        EncodeDestination(dmn->pdmnState->keyIDVoting)))
                                    );
                failed++;
            }
        }
    });

    return mnKeys;
}

static mnKeyList getDMNKeysForActiveMasternode(UniValue& resultsObj)
{
    // local node must be a masternode
    if (!activeMasternodeManager) {
        throw std::runtime_error(_("This is not a deterministic masternode. 'local' option disabled."));
    }

    CBLSSecretKey sk; CDeterministicMNCPtr dmn;
    auto res = activeMasternodeManager->GetOperatorKey(sk, dmn);
    if (!res) {
        resultsObj.push_back(packErrorRetStatus("local", res.getError()));
        return {};
    }

    return {MnKeyData("local", &dmn->collateralOutpoint, sk)};
}

// vote on proposal (finalized budget, if fFinal=true) with all possible keys or a single mn (mnAliasFilter)
// Note: for DMNs only proposal voting is allowed with the voting key
// (finalized budget voting requires the operator BLS key)
UniValue mnBudgetVoteInner(CWallet* const pwallet, bool fLegacyMN, const uint256& budgetHash, bool fFinal,
                                  const CBudgetVote::VoteDirection& nVote, const Optional<std::string>& mnAliasFilter)
{
    if (fFinal && !fLegacyMN) {
        throw std::runtime_error("Finalized budget voting is allowed only locally, from the masternode");
    }
    UniValue resultsObj(UniValue::VARR);
    int failed = 0;

    mnKeyList mnKeys = fLegacyMN ? getMNKeys(mnAliasFilter, resultsObj, failed)
                                 : getDMNVotingKeys(pwallet, mnAliasFilter, fFinal, resultsObj, failed);

    if (mnKeys.empty()) {
        return packVoteReturnValue(resultsObj, 0, failed);
    }

    return (fFinal ? voteFinalBudget(budgetHash, mnKeys, resultsObj, failed)
                   : voteProposal(budgetHash, nVote, mnKeys, resultsObj, failed));
}

// vote on proposal (finalized budget, if fFinal=true) with the active local masternode
// Note: for DMNs only finalized budget voting is allowed with the operator key
// (proposal voting requires the voting key)
UniValue mnLocalBudgetVoteInner(bool fLegacyMN, const uint256& budgetHash, bool fFinal,
                                       const CBudgetVote::VoteDirection& nVote)
{
    UniValue resultsObj(UniValue::VARR);

    mnKeyList mnKeys = fLegacyMN ? getMNKeysForActiveMasternode(resultsObj)
                                 : getDMNKeysForActiveMasternode(resultsObj);

    if (mnKeys.empty()) {
        return packVoteReturnValue(resultsObj, 0, 1);
    }

    return (fFinal ? voteFinalBudget(budgetHash, mnKeys, resultsObj, 0)
                   : voteProposal(budgetHash, nVote, mnKeys, resultsObj, 0));
}