// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The DIGIWAGE developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "budget/budgetproposal.h"

#include "masternodeman.h"

CBudgetProposal::CBudgetProposal():
        nAllotted(0),
        fValid(true),
        strInvalid(""),
        strProposalName("unknown"),
        strURL(""),
        nBlockStart(0),
        nBlockEnd(0),
        address(),
        nAmount(0),
        nFeeTXHash(UINT256_ZERO),
        nTime(0)
{}

CBudgetProposal::CBudgetProposal(const std::string& name,
                                 const std::string& url,
                                 int paycount,
                                 const CScript& payee,
                                 const CAmount& amount,
                                 int blockstart,
                                 const uint256& nfeetxhash):
        nAllotted(0),
        fValid(true),
        strInvalid(""),
        strProposalName(name),
        strURL(url),
        nBlockStart(blockstart),
        address(payee),
        nAmount(amount),
        nFeeTXHash(nfeetxhash),
        nTime(0)
{
    const int nBlocksPerCycle = Params().GetConsensus().nBudgetCycleBlocks;
    // !todo: remove this when v5 rules are enforced (nBlockStart is always = to nCycleStart)
    int nCycleStart = nBlockStart - nBlockStart % nBlocksPerCycle;

    // calculate the expiration block
    nBlockEnd = nCycleStart + (nBlocksPerCycle + 1)  * paycount;
}

// initialize from network broadcast message
bool CBudgetProposal::ParseBroadcast(CDataStream& broadcast)
{
    *this = CBudgetProposal();
    try {
        broadcast >> LIMITED_STRING(strProposalName, 20);
        broadcast >> LIMITED_STRING(strURL, 64);
        broadcast >> nTime;
        broadcast >> nBlockStart;
        broadcast >> nBlockEnd;
        broadcast >> nAmount;
        broadcast >> address;
        broadcast >> nFeeTXHash;
    } catch (std::exception& e) {
        return error("Unable to deserialize proposal broadcast: %s", e.what());
    }
    return true;
}

void CBudgetProposal::SyncVotes(CNode* pfrom, bool fPartial, int& nInvCount) const
{
    for (const auto& it: mapVotes) {
        const CBudgetVote& vote = it.second;
        if (vote.IsValid() && (!fPartial || !vote.IsSynced())) {
            pfrom->PushInventory(CInv(MSG_BUDGET_VOTE, vote.GetHash()));
            nInvCount++;
        }
    }
}

bool CBudgetProposal::IsHeavilyDownvoted(bool fNewRules)
{
    if (GetNays() - GetYeas() > (fNewRules ? 3 : 1) * mnodeman.CountEnabled() / 10) {
        strInvalid = "Heavily Downvoted";
        return true;
    }
    return false;
}

bool CBudgetProposal::CheckStartEnd()
{
    // !TODO: remove (and always use new rules) when all proposals submitted before v5 enforcement are expired.
    bool fNewRules = Params().GetConsensus().NetworkUpgradeActive(nBlockStart, Consensus::UPGRADE_V3_0);

    if (nBlockStart < 0 ||
            // block start must be a superblock
            (fNewRules && (nBlockStart % Params().GetConsensus().nBudgetCycleBlocks) != 0)) {
        strInvalid = "Invalid nBlockStart";
        return false;
    }

    if (nBlockEnd < nBlockStart) {
        strInvalid = "Invalid nBlockEnd (end before start)";
        return false;
    }

    if (fNewRules && GetTotalPaymentCount() > Params().GetConsensus().nMaxProposalPayments) {
        strInvalid = "Invalid payment count";
        return false;
    }

    return true;
}

bool CBudgetProposal::CheckAmount(const CAmount& nTotalBudget)
{
    // check minimum amount
    if (nAmount < PROPOSAL_MIN_AMOUNT) {
        strInvalid = "Invalid nAmount (too low)";
        return false;
    }

    // check maximum amount
    // can only pay out 10% of the possible coins (min value of coins)
    if (nAmount > nTotalBudget) {
        strInvalid = "Invalid nAmount (too high)";
        return false;
    }

    return true;
}

bool CBudgetProposal::CheckAddress()
{
    // !TODO: There might be an issue with multisig in the coinbase on mainnet
    // we will add support for it in a future release.
    if (address.IsPayToScriptHash()) {
        strInvalid = "Multisig is not currently supported.";
        return false;
    }

    // Check address
    CTxDestination dest;
    if (!ExtractDestination(address, dest, false)) {
        strInvalid = "Invalid script";
        return false;
    }
    if (!IsValidDestination(dest)) {
        strInvalid = "Invalid recipient address";
        return false;
    }

    return true;
}

bool CBudgetProposal::IsWellFormed(const CAmount& nTotalBudget)
{
    return CheckStartEnd() && CheckAmount(nTotalBudget) && CheckAddress();
}

bool CBudgetProposal::updateExpired(int nCurrentHeight)
{
    if (IsExpired(nCurrentHeight)) {
        strInvalid = "Proposal expired";
        return true;
    }
    return false;
}

bool CBudgetProposal::UpdateValid(int nCurrentHeight)
{
    fValid = false;

    // !TODO: remove after v5 enforcement and use fixed multiplier (3)
    bool fNewRules = Params().GetConsensus().NetworkUpgradeActive(nCurrentHeight, Consensus::UPGRADE_V3_0);

    // Never kill a proposal before the first superblock
    if (!fNewRules || nCurrentHeight > nBlockStart) {
        if (IsHeavilyDownvoted(fNewRules)) return false;
    }

    if (updateExpired(nCurrentHeight)) {
        return false;
    }

    fValid = true;
    strInvalid.clear();
    return true;
}

bool CBudgetProposal::IsEstablished() const
{
    return nTime < GetAdjustedTime() - Params().GetConsensus().nProposalEstablishmentTime;
}

bool CBudgetProposal::IsPassing(int nBlockStartBudget, int nBlockEndBudget, int mnCount) const
{
    if (!fValid)
        return false;

    if (this->nBlockStart > nBlockStartBudget)
        return false;

    if (this->nBlockEnd < nBlockEndBudget)
        return false;

    if (GetYeas() - GetNays() <= mnCount / 10)
        return false;

    if (!IsEstablished())
        return false;

    return true;
}

bool CBudgetProposal::IsExpired(int nCurrentHeight) const
{
    return nBlockEnd < nCurrentHeight;
}

bool CBudgetProposal::AddOrUpdateVote(const CBudgetVote& vote, std::string& strError)
{
    std::string strAction = "New vote inserted:";
    const COutPoint& mnId = vote.GetVin().prevout;
    const int64_t voteTime = vote.GetTime();

    if (mapVotes.count(mnId)) {
        const int64_t& oldTime = mapVotes[mnId].GetTime();
        if (oldTime > voteTime) {
            strError = strprintf("new vote older than existing vote - %s\n", vote.GetHash().ToString());
            LogPrint(BCLog::MNBUDGET, "%s: %s\n", __func__, strError);
            return false;
        }
        if (voteTime - oldTime < BUDGET_VOTE_UPDATE_MIN) {
            strError = strprintf("time between votes is too soon - %s - %lli sec < %lli sec\n",
                    vote.GetHash().ToString(), voteTime - oldTime, BUDGET_VOTE_UPDATE_MIN);
            LogPrint(BCLog::MNBUDGET, "%s: %s\n", __func__, strError);
            return false;
        }
        strAction = "Existing vote updated:";
    }

    mapVotes[mnId] = vote;
    LogPrint(BCLog::MNBUDGET, "%s: %s %s\n", __func__, strAction.c_str(), vote.GetHash().ToString().c_str());

    return true;
}

UniValue CBudgetProposal::GetVotesArray() const
{
    UniValue ret(UniValue::VARR);
    for (const auto& it: mapVotes) {
        ret.push_back(it.second.ToJSON());
    }
    return ret;
}

void CBudgetProposal::SetSynced(bool synced)
{
    for (auto& it: mapVotes) {
        CBudgetVote& vote = it.second;
        if (synced) {
            if (vote.IsValid()) vote.SetSynced(true);
        } else {
            vote.SetSynced(false);
        }
    }
}

double CBudgetProposal::GetRatio() const
{
    int yeas = GetYeas();
    int nays = GetNays();

    if (yeas + nays == 0) return 0.0f;

    return ((double)(yeas) / (double)(yeas + nays));
}

int CBudgetProposal::GetVoteCount(CBudgetVote::VoteDirection vd) const
{
    int ret = 0;
    for (const auto& it : mapVotes) {
        const CBudgetVote& vote = it.second;
        if (vote.GetDirection() == vd && vote.IsValid())
            ret++;
    }
    return ret;
}

int CBudgetProposal::GetBlockStartCycle() const
{
    //end block is half way through the next cycle (so the proposal will be removed much after the payment is sent)
    return GetBlockCycle(nBlockStart);
}

int CBudgetProposal::GetBlockCycle(int nHeight)
{
    return nHeight - nHeight % Params().GetConsensus().nBudgetCycleBlocks;
}

int CBudgetProposal::GetBlockEndCycle() const
{
    return nBlockEnd;

}

int CBudgetProposal::GetTotalPaymentCount() const
{
    return (GetBlockEndCycle() - GetBlockStartCycle()) / Params().GetConsensus().nBudgetCycleBlocks;
}

int CBudgetProposal::GetRemainingPaymentCount(int nCurrentHeight) const
{
    // If the proposal is already finished (passed the end block cycle), the payments value will be negative
    int nPayments = (GetBlockEndCycle() - GetBlockCycle(nCurrentHeight)) / Params().GetConsensus().nBudgetCycleBlocks - 1;
    // Take the lowest value
    return std::min(nPayments, GetTotalPaymentCount());
}

// return broadcast serialization
CDataStream CBudgetProposal::GetBroadcast() const
{
    CDataStream broadcast(SER_NETWORK, PROTOCOL_VERSION);
    broadcast.reserve(1000);
    broadcast << LIMITED_STRING(strProposalName, 20);
    broadcast << LIMITED_STRING(strURL, 64);
    broadcast << nTime;
    broadcast << nBlockStart;
    broadcast << nBlockEnd;
    broadcast << nAmount;
    broadcast << address;
    broadcast << nFeeTXHash;
    return broadcast;
}

void CBudgetProposal::Relay()
{
    CInv inv(MSG_BUDGET_PROPOSAL, GetHash());
    g_connman->RelayInv(inv);
}

