// Copyright (c) 2018-2021 The Dash Core developers
// Copyright (c) 2021 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "evo/deterministicmns.h"

#include "chainparams.h"
#include "consensus/upgrades.h"
#include "core_io.h"
#include "evo/specialtx.h"
#include "key_io.h"
#include "guiinterface.h"
#include "masternode.h" // for MasternodeCollateralMinConf
#include "masternodeman.h" // for mnodeman (!TODO: remove)
#include "script/standard.h"
#include "spork.h"
#include "sync.h"
#include "validation.h"

#include <univalue.h>

static const std::string DB_LIST_SNAPSHOT = "dmn_S";
static const std::string DB_LIST_DIFF = "dmn_D";

std::unique_ptr<CDeterministicMNManager> deterministicMNManager;

std::string CDeterministicMNState::ToString() const
{
    CTxDestination dest;
    std::string payoutAddress = "unknown";
    std::string operatorPayoutAddress = "none";
    if (ExtractDestination(scriptPayout, dest)) {
        payoutAddress = EncodeDestination(dest);
    }
    if (ExtractDestination(scriptOperatorPayout, dest)) {
        operatorPayoutAddress = EncodeDestination(dest);
    }

    return strprintf("CDeterministicMNState(nRegisteredHeight=%d, nLastPaidHeight=%d, nPoSePenalty=%d, nPoSeRevivedHeight=%d, nPoSeBanHeight=%d, nRevocationReason=%d, ownerAddress=%s, operatorPubKey=%s, votingAddress=%s, addr=%s, payoutAddress=%s, operatorPayoutAddress=%s)",
        nRegisteredHeight, nLastPaidHeight, nPoSePenalty, nPoSeRevivedHeight, nPoSeBanHeight, nRevocationReason,
        EncodeDestination(keyIDOwner), pubKeyOperator.Get().ToString(), EncodeDestination(keyIDVoting), addr.ToStringIPPort(), payoutAddress, operatorPayoutAddress);
}

void CDeterministicMNState::ToJson(UniValue& obj) const
{
    obj.clear();
    obj.setObject();
    obj.pushKV("service", addr.ToStringIPPort());
    obj.pushKV("registeredHeight", nRegisteredHeight);
    obj.pushKV("lastPaidHeight", nLastPaidHeight);
    obj.pushKV("PoSePenalty", nPoSePenalty);
    obj.pushKV("PoSeRevivedHeight", nPoSeRevivedHeight);
    obj.pushKV("PoSeBanHeight", nPoSeBanHeight);
    obj.pushKV("revocationReason", nRevocationReason);
    obj.pushKV("ownerAddress", EncodeDestination(keyIDOwner));
    obj.pushKV("operatorPubKey", pubKeyOperator.Get().ToString());
    obj.pushKV("votingAddress", EncodeDestination(keyIDVoting));

    CTxDestination dest1;
    if (ExtractDestination(scriptPayout, dest1)) {
        obj.pushKV("payoutAddress", EncodeDestination(dest1));
    }
    CTxDestination dest2;
    if (ExtractDestination(scriptOperatorPayout, dest2)) {
        obj.pushKV("operatorPayoutAddress", EncodeDestination(dest2));
    }
}

uint64_t CDeterministicMN::GetInternalId() const
{
    // can't get it if it wasn't set yet
    assert(internalId != std::numeric_limits<uint64_t>::max());
    return internalId;
}

std::string CDeterministicMN::ToString() const
{
    return strprintf("CDeterministicMN(proTxHash=%s, collateralOutpoint=%s, nOperatorReward=%f, state=%s", proTxHash.ToString(), collateralOutpoint.ToStringShort(), (double)nOperatorReward / 100, pdmnState->ToString());
}

void CDeterministicMN::ToJson(UniValue& obj) const
{
    obj.clear();
    obj.setObject();

    UniValue stateObj;
    pdmnState->ToJson(stateObj);

    obj.pushKV("proTxHash", proTxHash.ToString());
    obj.pushKV("collateralHash", collateralOutpoint.hash.ToString());
    obj.pushKV("collateralIndex", (int)collateralOutpoint.n);

    std::string collateralAddressStr = "";
    Coin coin;
    if (GetUTXOCoin(collateralOutpoint, coin)) {
        CTxDestination dest;
        if (ExtractDestination(coin.out.scriptPubKey, dest)) {
            collateralAddressStr = EncodeDestination(dest);
        }
    }
    obj.pushKV("collateralAddress", collateralAddressStr);
    obj.pushKV("operatorReward", (double)nOperatorReward / 100);
    obj.pushKV("dmnstate", stateObj);
}

CDeterministicMNCPtr CDeterministicMNList::GetMN(const uint256& proTxHash) const
{
    auto p = mnMap.find(proTxHash);
    if (p == nullptr) {
        return nullptr;
    }
    return *p;
}

CDeterministicMNCPtr CDeterministicMNList::GetValidMN(const uint256& proTxHash) const
{
    auto dmn = GetMN(proTxHash);
    if (dmn && dmn->IsPoSeBanned()) {
        return nullptr;
    }
    return dmn;
}

CDeterministicMNCPtr CDeterministicMNList::GetMNByOperatorKey(const CBLSPublicKey& pubKey)
{
    for (const auto& p : mnMap) {
        if (p.second->pdmnState->pubKeyOperator.Get() == pubKey) {
            return p.second;
        }
    }
    return nullptr;
}

CDeterministicMNCPtr CDeterministicMNList::GetMNByCollateral(const COutPoint& collateralOutpoint) const
{
    return GetUniquePropertyMN(collateralOutpoint);
}

CDeterministicMNCPtr CDeterministicMNList::GetValidMNByCollateral(const COutPoint& collateralOutpoint) const
{
    auto dmn = GetMNByCollateral(collateralOutpoint);
    if (dmn && dmn->IsPoSeBanned()) {
        return nullptr;
    }
    return dmn;
}

CDeterministicMNCPtr CDeterministicMNList::GetMNByService(const CService& service) const
{
    return GetUniquePropertyMN(service);
}

CDeterministicMNCPtr CDeterministicMNList::GetMNByInternalId(uint64_t internalId) const
{
    auto proTxHash = mnInternalIdMap.find(internalId);
    if (!proTxHash) {
        return nullptr;
    }
    return GetMN(*proTxHash);
}

static int CompareByLastPaidGetHeight(const CDeterministicMN& dmn)
{
    int height = dmn.pdmnState->nLastPaidHeight;
    if (dmn.pdmnState->nPoSeRevivedHeight != -1 && dmn.pdmnState->nPoSeRevivedHeight > height) {
        height = dmn.pdmnState->nPoSeRevivedHeight;
    } else if (height == 0) {
        height = dmn.pdmnState->nRegisteredHeight;
    }
    return height;
}

static bool CompareByLastPaid(const CDeterministicMN& _a, const CDeterministicMN& _b)
{
    int ah = CompareByLastPaidGetHeight(_a);
    int bh = CompareByLastPaidGetHeight(_b);
    if (ah == bh) {
        return _a.proTxHash < _b.proTxHash;
    } else {
        return ah < bh;
    }
}
static bool CompareByLastPaid(const CDeterministicMNCPtr& _a, const CDeterministicMNCPtr& _b)
{
    return CompareByLastPaid(*_a, *_b);
}

CDeterministicMNCPtr CDeterministicMNList::GetMNPayee() const
{
    if (mnMap.size() == 0) {
        return nullptr;
    }

    CDeterministicMNCPtr best;
    ForEachMN(true, [&](const CDeterministicMNCPtr& dmn) {
        if (!best || CompareByLastPaid(dmn, best)) {
            best = dmn;
        }
    });

    return best;
}

std::vector<CDeterministicMNCPtr> CDeterministicMNList::GetProjectedMNPayees(unsigned int nCount) const
{
    if (nCount > GetValidMNsCount()) {
        nCount = GetValidMNsCount();
    }

    std::vector<CDeterministicMNCPtr> result;
    result.reserve(nCount);

    ForEachMN(true, [&](const CDeterministicMNCPtr& dmn) {
        result.emplace_back(dmn);
    });
    std::sort(result.begin(), result.end(), [&](const CDeterministicMNCPtr& a, const CDeterministicMNCPtr& b) {
        return CompareByLastPaid(a, b);
    });

    result.resize(nCount);

    return result;
}

std::vector<CDeterministicMNCPtr> CDeterministicMNList::CalculateQuorum(size_t maxSize, const uint256& modifier) const
{
    auto scores = CalculateScores(modifier);

    // sort is descending order
    std::sort(scores.rbegin(), scores.rend(), [](const std::pair<arith_uint256, CDeterministicMNCPtr>& a, std::pair<arith_uint256, CDeterministicMNCPtr>& b) {
        if (a.first == b.first) {
            // this should actually never happen, but we should stay compatible with how the non deterministic MNs did the sorting
            return a.second->collateralOutpoint < b.second->collateralOutpoint;
        }
        return a.first < b.first;
    });

    // take top maxSize entries and return it
    std::vector<CDeterministicMNCPtr> result;
    result.resize(std::min(maxSize, scores.size()));
    for (size_t i = 0; i < result.size(); i++) {
        result[i] = std::move(scores[i].second);
    }
    return result;
}

std::vector<std::pair<arith_uint256, CDeterministicMNCPtr>> CDeterministicMNList::CalculateScores(const uint256& modifier) const
{
    std::vector<std::pair<arith_uint256, CDeterministicMNCPtr>> scores;
    scores.reserve(GetAllMNsCount());
    ForEachMN(true, [&](const CDeterministicMNCPtr& dmn) {
        if (dmn->pdmnState->confirmedHash.IsNull()) {
            // we only take confirmed MNs into account to avoid hash grinding on the ProRegTxHash to sneak MNs into a
            // future quorums
            return;
        }
        // calculate sha256(sha256(proTxHash, confirmedHash), modifier) per MN
        // Please note that this is not a double-sha256 but a single-sha256
        // The first part is already precalculated (confirmedHashWithProRegTxHash)
        // TODO When https://github.com/bitcoin/bitcoin/pull/13191 gets backported, implement something that is similar but for single-sha256
        uint256 h;
        CSHA256 sha256;
        sha256.Write(dmn->pdmnState->confirmedHashWithProRegTxHash.begin(), dmn->pdmnState->confirmedHashWithProRegTxHash.size());
        sha256.Write(modifier.begin(), modifier.size());
        sha256.Finalize(h.begin());

        scores.emplace_back(UintToArith256(h), dmn);
    });

    return scores;
}

int CDeterministicMNList::CalcMaxPoSePenalty() const
{
    // Maximum PoSe penalty is dynamic and equals the number of registered MNs
    // It's however at least 100.
    // This means that the max penalty is usually equal to a full payment cycle
    return std::max(100, (int)GetAllMNsCount());
}

int CDeterministicMNList::CalcPenalty(int percent) const
{
    assert(percent > 0);
    return (CalcMaxPoSePenalty() * percent) / 100;
}

void CDeterministicMNList::PoSePunish(const uint256& proTxHash, int penalty, bool debugLogs)
{
    assert(penalty > 0);

    auto dmn = GetMN(proTxHash);
    if (!dmn) {
        throw(std::runtime_error(strprintf("%s: Can't find a masternode with proTxHash=%s", __func__, proTxHash.ToString())));
    }

    int maxPenalty = CalcMaxPoSePenalty();

    auto newState = std::make_shared<CDeterministicMNState>(*dmn->pdmnState);
    newState->nPoSePenalty += penalty;
    newState->nPoSePenalty = std::min(maxPenalty, newState->nPoSePenalty);

    if (debugLogs) {
        LogPrintf("CDeterministicMNList::%s -- punished MN %s, penalty %d->%d (max=%d)\n",
                  __func__, proTxHash.ToString(), dmn->pdmnState->nPoSePenalty, newState->nPoSePenalty, maxPenalty);
    }

    if (newState->nPoSePenalty >= maxPenalty && newState->nPoSeBanHeight == -1) {
        newState->nPoSeBanHeight = nHeight;
        if (debugLogs) {
            LogPrintf("CDeterministicMNList::%s -- banned MN %s at height %d\n",
                      __func__, proTxHash.ToString(), nHeight);
        }
    }
    UpdateMN(proTxHash, newState);
}

void CDeterministicMNList::PoSeDecrease(const uint256& proTxHash)
{
    auto dmn = GetMN(proTxHash);
    if (!dmn) {
        throw(std::runtime_error(strprintf("%s: Can't find a masternode with proTxHash=%s", __func__, proTxHash.ToString())));
    }
    assert(dmn->pdmnState->nPoSePenalty > 0 && dmn->pdmnState->nPoSeBanHeight == -1);

    auto newState = std::make_shared<CDeterministicMNState>(*dmn->pdmnState);
    newState->nPoSePenalty--;
    UpdateMN(proTxHash, newState);
}

CDeterministicMNListDiff CDeterministicMNList::BuildDiff(const CDeterministicMNList& to) const
{
    CDeterministicMNListDiff diffRet;

    to.ForEachMN(false, [&](const CDeterministicMNCPtr& toPtr) {
        auto fromPtr = GetMN(toPtr->proTxHash);
        if (fromPtr == nullptr) {
            diffRet.addedMNs.emplace_back(toPtr);
        } else if (fromPtr != toPtr || fromPtr->pdmnState != toPtr->pdmnState) {
            CDeterministicMNStateDiff stateDiff(*fromPtr->pdmnState, *toPtr->pdmnState);
            if (stateDiff.fields) {
                diffRet.updatedMNs.emplace(toPtr->GetInternalId(), std::move(stateDiff));
            }
        }
    });
    ForEachMN(false, [&](const CDeterministicMNCPtr& fromPtr) {
        auto toPtr = to.GetMN(fromPtr->proTxHash);
        if (toPtr == nullptr) {
            diffRet.removedMns.emplace(fromPtr->GetInternalId());
        }
    });

    // added MNs need to be sorted by internalId so that these are added in correct order when the diff is applied later
    // otherwise internalIds will not match with the original list
    std::sort(diffRet.addedMNs.begin(), diffRet.addedMNs.end(), [](const CDeterministicMNCPtr& a, const CDeterministicMNCPtr& b) {
        return a->GetInternalId() < b->GetInternalId();
    });

    return diffRet;
}

CDeterministicMNList CDeterministicMNList::ApplyDiff(const CBlockIndex* pindex, const CDeterministicMNListDiff& diff) const
{
    CDeterministicMNList result = *this;
    result.blockHash = pindex->GetBlockHash();
    result.nHeight = pindex->nHeight;

    for (const auto& id : diff.removedMns) {
        auto dmn = result.GetMNByInternalId(id);
        if (!dmn) {
            throw(std::runtime_error(strprintf("%s: can't find a removed masternode, id=%d", __func__, id)));
        }
        result.RemoveMN(dmn->proTxHash);
    }
    for (const auto& dmn : diff.addedMNs) {
        result.AddMN(dmn);
    }
    for (const auto& p : diff.updatedMNs) {
        auto dmn = result.GetMNByInternalId(p.first);
        result.UpdateMN(dmn, p.second);
    }

    return result;
}

void CDeterministicMNList::AddMN(const CDeterministicMNCPtr& dmn, bool fBumpTotalCount)
{
    assert(dmn != nullptr);

    if (mnMap.find(dmn->proTxHash)) {
        throw(std::runtime_error(strprintf("%s: can't add a duplicate masternode with the same proTxHash=%s", __func__, dmn->proTxHash.ToString())));
    }
    if (mnInternalIdMap.find(dmn->GetInternalId())) {
        throw(std::runtime_error(strprintf("%s: can't add a duplicate masternode with the same internalId=%d", __func__, dmn->GetInternalId())));
    }
    if (HasUniqueProperty(dmn->pdmnState->addr)) {
        throw(std::runtime_error(strprintf("%s: can't add a masternode with a duplicate address %s", __func__, dmn->pdmnState->addr.ToStringIPPort())));
    }
    if (HasUniqueProperty(dmn->pdmnState->keyIDOwner) || HasUniqueProperty(dmn->pdmnState->pubKeyOperator)) {
        throw(std::runtime_error(strprintf("%s: can't add a masternode with a duplicate key (%s or %s)", __func__, EncodeDestination(dmn->pdmnState->keyIDOwner), dmn->pdmnState->pubKeyOperator.Get().ToString())));
    }

    mnMap = mnMap.set(dmn->proTxHash, dmn);
    mnInternalIdMap = mnInternalIdMap.set(dmn->GetInternalId(), dmn->proTxHash);
    AddUniqueProperty(dmn, dmn->collateralOutpoint);
    if (dmn->pdmnState->addr != CService()) {
        AddUniqueProperty(dmn, dmn->pdmnState->addr);
    }
    AddUniqueProperty(dmn, dmn->pdmnState->keyIDOwner);
    AddUniqueProperty(dmn, dmn->pdmnState->pubKeyOperator);

    if (fBumpTotalCount) {
        // nTotalRegisteredCount acts more like a checkpoint, not as a limit,
        nTotalRegisteredCount = std::max(dmn->GetInternalId() + 1, (uint64_t)nTotalRegisteredCount);
    }
}

void CDeterministicMNList::UpdateMN(const CDeterministicMNCPtr& oldDmn, const CDeterministicMNStateCPtr& pdmnState)
{
    assert(oldDmn != nullptr);

    if (HasUniqueProperty(oldDmn->pdmnState->addr) && GetUniquePropertyMN(oldDmn->pdmnState->addr)->proTxHash != oldDmn->proTxHash) {
        throw(std::runtime_error(strprintf("%s: can't update a masternode with a duplicate address %s", __func__, oldDmn->pdmnState->addr.ToStringIPPort())));
    }

    auto dmn = std::make_shared<CDeterministicMN>(*oldDmn);
    auto oldState = dmn->pdmnState;
    dmn->pdmnState = pdmnState;
    mnMap = mnMap.set(oldDmn->proTxHash, dmn);

    UpdateUniqueProperty(dmn, oldState->addr, pdmnState->addr);
    UpdateUniqueProperty(dmn, oldState->keyIDOwner, pdmnState->keyIDOwner);
    UpdateUniqueProperty(dmn, oldState->pubKeyOperator, pdmnState->pubKeyOperator);
}

void CDeterministicMNList::UpdateMN(const uint256& proTxHash, const CDeterministicMNStateCPtr& pdmnState)
{
    auto oldDmn = mnMap.find(proTxHash);
    if (!oldDmn) {
        throw(std::runtime_error(strprintf("%s: Can't find a masternode with proTxHash=%s", __func__, proTxHash.ToString())));
    }
    UpdateMN(*oldDmn, pdmnState);
}

void CDeterministicMNList::UpdateMN(const CDeterministicMNCPtr& oldDmn, const CDeterministicMNStateDiff& stateDiff)
{
    assert(oldDmn != nullptr);
    auto oldState = oldDmn->pdmnState;
    auto newState = std::make_shared<CDeterministicMNState>(*oldState);
    stateDiff.ApplyToState(*newState);
    UpdateMN(oldDmn, newState);
}

void CDeterministicMNList::RemoveMN(const uint256& proTxHash)
{
    auto dmn = GetMN(proTxHash);
    if (!dmn) {
        throw(std::runtime_error(strprintf("%s: Can't find a masternode with proTxHash=%s", __func__, proTxHash.ToString())));
    }
    DeleteUniqueProperty(dmn, dmn->collateralOutpoint);
    if (dmn->pdmnState->addr != CService()) {
        DeleteUniqueProperty(dmn, dmn->pdmnState->addr);
    }
    DeleteUniqueProperty(dmn, dmn->pdmnState->keyIDOwner);
    DeleteUniqueProperty(dmn, dmn->pdmnState->pubKeyOperator);

    mnMap = mnMap.erase(proTxHash);
    mnInternalIdMap = mnInternalIdMap.erase(dmn->GetInternalId());
}

CDeterministicMNManager::CDeterministicMNManager(CEvoDB& _evoDb) :
    evoDb(_evoDb)
{
}

bool CDeterministicMNManager::ProcessBlock(const CBlock& block, const CBlockIndex* pindex, CValidationState& _state, bool fJustCheck)
{
    int nHeight = pindex->nHeight;
    if (!IsDIP3Enforced(nHeight)) {
        // nothing to do
        return true;
    }

    CDeterministicMNList oldList, newList;
    CDeterministicMNListDiff diff;

    try {
        LOCK(cs);

        if (!BuildNewListFromBlock(block, pindex->pprev, _state, newList, true)) {
            // pass the state returned by the function above
            return false;
        }

        if (fJustCheck) {
            return true;
        }

        if (newList.GetHeight() == -1) {
            newList.SetHeight(nHeight);
        }

        newList.SetBlockHash(block.GetHash());

        oldList = GetListForBlock(pindex->pprev);
        diff = oldList.BuildDiff(newList);

        evoDb.Write(std::make_pair(DB_LIST_DIFF, newList.GetBlockHash()), diff);
        if ((nHeight % DISK_SNAPSHOT_PERIOD) == 0 || oldList.GetHeight() == -1) {
            evoDb.Write(std::make_pair(DB_LIST_SNAPSHOT, newList.GetBlockHash()), newList);
            mnListsCache.emplace(newList.GetBlockHash(), newList);
            LogPrintf("CDeterministicMNManager::%s -- Wrote snapshot. nHeight=%d, mapCurMNs.allMNsCount=%d\n",
                __func__, nHeight, newList.GetAllMNsCount());
        }

        diff.nHeight = pindex->nHeight;
        mnListDiffsCache.emplace(pindex->GetBlockHash(), diff);
    } catch (const std::exception& e) {
        LogPrintf("CDeterministicMNManager::%s -- internal error: %s\n", __func__, e.what());
        return _state.DoS(100, false, REJECT_INVALID, "failed-dmn-block");
    }

    // Don't hold cs while calling signals
    if (diff.HasChanges()) {
        GetMainSignals().NotifyMasternodeListChanged(false, oldList, diff);
        uiInterface.NotifyMasternodeListChanged(newList);
    }

    LOCK(cs);
    CleanupCache(nHeight);

    return true;
}

bool CDeterministicMNManager::UndoBlock(const CBlock& block, const CBlockIndex* pindex)
{
    if (!IsDIP3Enforced(pindex->nHeight)) {
        // nothing to do
        return true;
    }

    const uint256& blockHash = block.GetHash();

    CDeterministicMNList curList;
    CDeterministicMNList prevList;
    CDeterministicMNListDiff diff;
    {
        LOCK(cs);
        evoDb.Read(std::make_pair(DB_LIST_DIFF, blockHash), diff);

        if (diff.HasChanges()) {
            // need to call this before erasing
            curList = GetListForBlock(pindex);
            prevList = GetListForBlock(pindex->pprev);
        }

        mnListsCache.erase(blockHash);
        mnListDiffsCache.erase(blockHash);
    }

    if (diff.HasChanges()) {
        auto inversedDiff = curList.BuildDiff(prevList);
        GetMainSignals().NotifyMasternodeListChanged(true, curList, inversedDiff);
        uiInterface.NotifyMasternodeListChanged(prevList);
    }

    return true;
}

void CDeterministicMNManager::UpdatedBlockTip(const CBlockIndex* pindex)
{
    LOCK(cs);
    tipIndex = pindex;
}

bool CDeterministicMNManager::BuildNewListFromBlock(const CBlock& block, const CBlockIndex* pindexPrev, CValidationState& _state, CDeterministicMNList& mnListRet, bool debugLogs)
{
    AssertLockHeld(cs);

    int nHeight = pindexPrev->nHeight + 1;

    CDeterministicMNList oldList = GetListForBlock(pindexPrev);
    CDeterministicMNList newList = oldList;
    newList.SetBlockHash(UINT256_ZERO); // we can't know the final block hash, so better not return a (invalid) block hash
    newList.SetHeight(nHeight);

    auto payee = oldList.GetMNPayee();

    // we iterate the oldList here and update the newList
    // this is only valid as long these have not diverged at this point, which is the case as long as we don't add
    // code above this loop that modifies newList
    oldList.ForEachMN(false, [&](const CDeterministicMNCPtr& dmn) {
        if (!dmn->pdmnState->confirmedHash.IsNull()) {
            // already confirmed
            return;
        }
        // this works on the previous block, so confirmation will happen one block after nMasternodeMinimumConfirmations
        // has been reached, but the block hash will then point to the block at nMasternodeMinimumConfirmations
        int nConfirmations = pindexPrev->nHeight - dmn->pdmnState->nRegisteredHeight;
        if (nConfirmations >= MasternodeCollateralMinConf()) {
            auto newState = std::make_shared<CDeterministicMNState>(*dmn->pdmnState);
            newState->UpdateConfirmedHash(dmn->proTxHash, pindexPrev->GetBlockHash());
            newList.UpdateMN(dmn->proTxHash, newState);
        }
    });

    DecreasePoSePenalties(newList);

    // we skip the coinbase
    for (int i = 1; i < (int)block.vtx.size(); i++) {
        const CTransaction& tx = *block.vtx[i];

        if (tx.nType == CTransaction::TxType::PROREG) {
            ProRegPL pl;
            if (!GetTxPayload(tx, pl)) {
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-payload");
            }

            auto dmn = std::make_shared<CDeterministicMN>(newList.GetTotalRegisteredCount());
            dmn->proTxHash = tx.GetHash();

            // collateralOutpoint is either pointing to an external collateral or to the ProRegTx itself
            dmn->collateralOutpoint = pl.collateralOutpoint.hash.IsNull() ? COutPoint(tx.GetHash(), pl.collateralOutpoint.n)
                                                                          : pl.collateralOutpoint;

            // if the collateral outpoint appears in the legacy masternode list, remove the old node
            // !TODO: remove this when the transition to DMN is complete
            CMasternode* old_mn = mnodeman.Find(dmn->collateralOutpoint);
            if (old_mn) {
                old_mn->SetSpent();
                mnodeman.CheckAndRemove();
            }

            Coin coin;
            const CAmount collAmt = Params().GetConsensus().nMNCollateralAmt;
            if (!pl.collateralOutpoint.hash.IsNull() && (!GetUTXOCoin(pl.collateralOutpoint, coin) || coin.out.nValue != collAmt)) {
                // should actually never get to this point as CheckProRegTx should have handled this case.
                // We do this additional check nevertheless to be 100% sure
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-collateral");
            }

            auto replacedDmn = newList.GetMNByCollateral(dmn->collateralOutpoint);
            if (replacedDmn != nullptr) {
                // This might only happen with a ProRegTx that refers an external collateral
                // In that case the new ProRegTx will replace the old one. This means the old one is removed
                // and the new one is added like a completely fresh one, which is also at the bottom of the payment list
                newList.RemoveMN(replacedDmn->proTxHash);
                if (debugLogs) {
                    LogPrintf("CDeterministicMNManager::%s -- MN %s removed from list because collateral was used for a new ProRegTx. collateralOutpoint=%s, nHeight=%d, mapCurMNs.allMNsCount=%d\n",
                              __func__, replacedDmn->proTxHash.ToString(), dmn->collateralOutpoint.ToStringShort(), nHeight, newList.GetAllMNsCount());
                }
            }

            if (newList.HasUniqueProperty(pl.addr)) {
                return _state.DoS(100, false, REJECT_DUPLICATE, "bad-protx-dup-IP-address");
            }
            if (newList.HasUniqueProperty(pl.keyIDOwner)) {
                return _state.DoS(100, false, REJECT_DUPLICATE, "bad-protx-dup-owner-key");
            }
            if (newList.HasUniqueProperty(pl.pubKeyOperator)) {
                return _state.DoS(100, false, REJECT_DUPLICATE, "bad-protx-dup-operator-key");
            }

            dmn->nOperatorReward = pl.nOperatorReward;

            auto dmnState = std::make_shared<CDeterministicMNState>(pl);
            dmnState->nRegisteredHeight = nHeight;
            if (pl.addr == CService()) {
                // start in banned pdmnState as we need to wait for a ProUpServTx
                dmnState->nPoSeBanHeight = nHeight;
            }
            dmn->pdmnState = dmnState;

            newList.AddMN(dmn);

            if (debugLogs) {
                LogPrintf("CDeterministicMNManager::%s -- MN %s added at height %d: %s\n",
                    __func__, tx.GetHash().ToString(), nHeight, pl.ToString());
            }

        } else if (tx.nType == CTransaction::TxType::PROUPSERV) {
            ProUpServPL pl;
            if (!GetTxPayload(tx, pl)) {
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-payload");
            }

            if (newList.HasUniqueProperty(pl.addr) && newList.GetUniquePropertyMN(pl.addr)->proTxHash != pl.proTxHash) {
                return _state.DoS(100, false, REJECT_DUPLICATE, "bad-protx-dup-addr");
            }

            CDeterministicMNCPtr dmn = newList.GetMN(pl.proTxHash);
            if (!dmn) {
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-hash");
            }
            if (dmn->nOperatorReward == 0 && !pl.scriptOperatorPayout.empty()) {
                // operator payout address can not be set if the operator reward is 0
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-operator-payee");
            }
            auto newState = std::make_shared<CDeterministicMNState>(*dmn->pdmnState);
            newState->addr = pl.addr;
            newState->scriptOperatorPayout = pl.scriptOperatorPayout;

            if (newState->nPoSeBanHeight != -1) {
                // only revive when all keys are set
                if (newState->pubKeyOperator.Get().IsValid() && !newState->keyIDVoting.IsNull() && !newState->keyIDOwner.IsNull()) {
                    newState->nPoSePenalty = 0;
                    newState->nPoSeBanHeight = -1;
                    newState->nPoSeRevivedHeight = nHeight;

                    if (debugLogs) {
                        LogPrintf("CDeterministicMNManager::%s -- MN %s revived at height %d\n",
                            __func__, pl.proTxHash.ToString(), nHeight);
                    }
                }
            }

            newList.UpdateMN(pl.proTxHash, newState);
            if (debugLogs) {
                LogPrintf("CDeterministicMNManager::%s -- MN %s updated at height %d: %s\n",
                    __func__, pl.proTxHash.ToString(), nHeight, pl.ToString());
            }

        } else if (tx.nType == CTransaction::TxType::PROUPREG) {
            ProUpRegPL pl;
            if (!GetTxPayload(tx, pl)) {
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-payload");
            }

            CDeterministicMNCPtr dmn = newList.GetMN(pl.proTxHash);
            if (!dmn) {
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-hash");
            }
            if (newList.HasUniqueProperty(pl.pubKeyOperator) && newList.GetUniquePropertyMN(pl.pubKeyOperator)->proTxHash != pl.proTxHash) {
                return _state.DoS(100, false, REJECT_DUPLICATE, "bad-protx-dup-operator-key");
            }
            auto newState = std::make_shared<CDeterministicMNState>(*dmn->pdmnState);
            if (newState->pubKeyOperator.Get() != pl.pubKeyOperator) {
                // reset all operator related fields and put MN into PoSe-banned state in case the operator key changes
                newState->ResetOperatorFields();
                newState->BanIfNotBanned(nHeight);
            }
            newState->pubKeyOperator.Set(pl.pubKeyOperator);
            newState->keyIDVoting = pl.keyIDVoting;
            newState->scriptPayout = pl.scriptPayout;

            newList.UpdateMN(pl.proTxHash, newState);

            if (debugLogs) {
                LogPrintf("CDeterministicMNManager::%s -- MN %s updated at height %d: %s\n",
                    __func__, pl.proTxHash.ToString(), nHeight, pl.ToString());
            }

        } else if (tx.nType == CTransaction::TxType::PROUPREV) {
            ProUpRevPL pl;
            if (!GetTxPayload(tx, pl)) {
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-payload");
            }

            CDeterministicMNCPtr dmn = newList.GetMN(pl.proTxHash);
            if (!dmn) {
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-hash");
            }
            auto newState = std::make_shared<CDeterministicMNState>(*dmn->pdmnState);
            newState->ResetOperatorFields();
            newState->BanIfNotBanned(nHeight);
            newState->nRevocationReason = pl.nReason;

            newList.UpdateMN(pl.proTxHash, newState);

            if (debugLogs) {
                LogPrintf("CDeterministicMNManager::%s -- MN %s updated at height %d: %s\n",
                    __func__, pl.proTxHash.ToString(), nHeight, pl.ToString());
            }
        }

    }

    // check if any existing MN collateral is spent by this transaction
    // we skip the coinbase
    for (int i = 1; i < (int)block.vtx.size(); i++) {
        const CTransaction& tx = *block.vtx[i];
        for (const auto& in : tx.vin) {
            auto dmn = newList.GetMNByCollateral(in.prevout);
            if (dmn && dmn->collateralOutpoint == in.prevout) {
                newList.RemoveMN(dmn->proTxHash);
                if (debugLogs) {
                    LogPrintf("CDeterministicMNManager::%s -- MN %s removed from list because collateral was spent. collateralOutpoint=%s, nHeight=%d, mapCurMNs.allMNsCount=%d\n",
                              __func__, dmn->proTxHash.ToString(), dmn->collateralOutpoint.ToStringShort(), nHeight, newList.GetAllMNsCount());
                }
            }
        }
    }

    // The payee for the current block was determined by the previous block's list but it might have disappeared in the
    // current block. We still pay that MN one last time however.
    if (payee && newList.HasMN(payee->proTxHash)) {
        auto newState = std::make_shared<CDeterministicMNState>(*newList.GetMN(payee->proTxHash)->pdmnState);
        newState->nLastPaidHeight = nHeight;
        newList.UpdateMN(payee->proTxHash, newState);
    }

    mnListRet = std::move(newList);

    return true;
}

void CDeterministicMNManager::DecreasePoSePenalties(CDeterministicMNList& mnList)
{
    std::vector<uint256> toDecrease;
    toDecrease.reserve(mnList.GetValidMNsCount() / 10);
    // only iterate and decrease for valid ones (not PoSe banned yet)
    // if a MN ever reaches the maximum, it stays in PoSe banned state until revived
    mnList.ForEachMN(true, [&](const CDeterministicMNCPtr& dmn) {
        if (dmn->pdmnState->nPoSePenalty > 0 && dmn->pdmnState->nPoSeBanHeight == -1) {
            toDecrease.emplace_back(dmn->proTxHash);
        }
    });

    for (const auto& proTxHash : toDecrease) {
        mnList.PoSeDecrease(proTxHash);
    }
}

CDeterministicMNList CDeterministicMNManager::GetListForBlock(const CBlockIndex* pindex)
{
    LOCK(cs);

    // Return early before enforcement
    if (!IsDIP3Enforced(pindex->nHeight)) {
        return {};
    }

    CDeterministicMNList snapshot;
    std::list<const CBlockIndex*> listDiffIndexes;

    while (true) {
        // try using cache before reading from disk
        auto itLists = mnListsCache.find(pindex->GetBlockHash());
        if (itLists != mnListsCache.end()) {
            snapshot = itLists->second;
            break;
        }

        if (evoDb.Read(std::make_pair(DB_LIST_SNAPSHOT, pindex->GetBlockHash()), snapshot)) {
            mnListsCache.emplace(pindex->GetBlockHash(), snapshot);
            break;
        }

        // no snapshot found yet, check diffs
        auto itDiffs = mnListDiffsCache.find(pindex->GetBlockHash());
        if (itDiffs != mnListDiffsCache.end()) {
            listDiffIndexes.emplace_front(pindex);
            pindex = pindex->pprev;
            continue;
        }

        CDeterministicMNListDiff diff;
        if (!evoDb.Read(std::make_pair(DB_LIST_DIFF, pindex->GetBlockHash()), diff)) {
            // no snapshot and no diff on disk means that it's initial snapshot (empty list)
            // If we get here, then this must be the block before the enforcement of DIP3.
            if (!IsActivationHeight(pindex->nHeight + 1, Params().GetConsensus(), Consensus::UPGRADE_VNEXT)) {
                std::string err = strprintf("No masternode list data found for block %s at height %d. "
                                            "Possible corrupt database.", pindex->GetBlockHash().ToString(), pindex->nHeight);
                throw std::runtime_error(err);
            }
            snapshot = CDeterministicMNList(pindex->GetBlockHash(), -1, 0);
            mnListsCache.emplace(pindex->GetBlockHash(), snapshot);
            break;
        }

        diff.nHeight = pindex->nHeight;
        mnListDiffsCache.emplace(pindex->GetBlockHash(), std::move(diff));
        listDiffIndexes.emplace_front(pindex);
        pindex = pindex->pprev;
    }

    for (const auto& diffIndex : listDiffIndexes) {
        const auto& diff = mnListDiffsCache.at(diffIndex->GetBlockHash());
        if (diff.HasChanges()) {
            snapshot = snapshot.ApplyDiff(diffIndex, diff);
        } else {
            snapshot.SetBlockHash(diffIndex->GetBlockHash());
            snapshot.SetHeight(diffIndex->nHeight);
        }
    }

    if (tipIndex) {
        // always keep a snapshot for the tip
        if (snapshot.GetBlockHash() == tipIndex->GetBlockHash()) {
            mnListsCache.emplace(snapshot.GetBlockHash(), snapshot);
        } else {
            // !TODO: keep snapshots for yet alive quorums
        }
    }

    return snapshot;
}

CDeterministicMNList CDeterministicMNManager::GetListAtChainTip()
{
    LOCK(cs);
    if (!tipIndex) {
        return {};
    }
    return GetListForBlock(tipIndex);
}

bool CDeterministicMNManager::IsDIP3Enforced(int nHeight) const
{
    return Params().GetConsensus().NetworkUpgradeActive(nHeight, Consensus::UPGRADE_VNEXT);
}

bool CDeterministicMNManager::IsDIP3Enforced() const
{
    int tipHeight = WITH_LOCK(cs, return tipIndex ? tipIndex->nHeight : -1;);
    return IsDIP3Enforced(tipHeight);
}

bool CDeterministicMNManager::LegacyMNObsolete(int nHeight) const
{
    return nHeight > sporkManager.GetSporkValue(SPORK_21_LEGACY_MNS_MAX_HEIGHT);
}

bool CDeterministicMNManager::LegacyMNObsolete() const
{
    int tipHeight = WITH_LOCK(cs, return tipIndex ? tipIndex->nHeight : -1;);
    return LegacyMNObsolete(tipHeight);
}

void CDeterministicMNManager::CleanupCache(int nHeight)
{
    AssertLockHeld(cs);

    std::vector<uint256> toDeleteLists;
    std::vector<uint256> toDeleteDiffs;
    for (const auto& p : mnListsCache) {
        if (p.second.GetHeight() + LIST_DIFFS_CACHE_SIZE < nHeight) {
            toDeleteLists.emplace_back(p.first);
            continue;
        }
        // !TODO: llmq cache cleanup
    }
    for (const auto& h : toDeleteLists) {
        mnListsCache.erase(h);
    }
    for (const auto& p : mnListDiffsCache) {
        if (p.second.nHeight + LIST_DIFFS_CACHE_SIZE < nHeight) {
            toDeleteDiffs.emplace_back(p.first);
        }
    }
    for (const auto& h : toDeleteDiffs) {
        mnListDiffsCache.erase(h);
    }
}
