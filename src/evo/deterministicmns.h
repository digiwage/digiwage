// Copyright (c) 2018-2021 The Dash Core developers
// Copyright (c) 2021 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIWAGE_DETERMINISTICMNS_H
#define DIGIWAGE_DETERMINISTICMNS_H

#include "arith_uint256.h"
#include "bls/bls_wrapper.h"
#include "dbwrapper.h"
#include "evo/evodb.h"
#include "evo/providertx.h"
#include "saltedhasher.h"
#include "sync.h"

#include <immer/map.hpp>
#include <immer/map_transient.hpp>

#include <unordered_map>

class CBlock;
class CBlockIndex;
class CValidationState;

class CDeterministicMNState
{
public:
    int nRegisteredHeight{-1};
    int nLastPaidHeight{0};
    int nPoSePenalty{0};
    int nPoSeRevivedHeight{-1};
    int nPoSeBanHeight{-1};
    uint16_t nRevocationReason{ProUpRevPL::REASON_NOT_SPECIFIED};

    // the block hash X blocks after registration, used in quorum calculations
    uint256 confirmedHash;
    // sha256(proTxHash, confirmedHash) to speed up quorum calculations
    // please note that this is NOT a double-sha256 hash
    uint256 confirmedHashWithProRegTxHash;

    CKeyID keyIDOwner;
    CBLSLazyPublicKey pubKeyOperator;
    CKeyID keyIDVoting;
    CService addr;
    CScript scriptPayout;
    CScript scriptOperatorPayout;

public:
    CDeterministicMNState() {}
    explicit CDeterministicMNState(const ProRegPL& pl)
    {
        keyIDOwner = pl.keyIDOwner;
        pubKeyOperator.Set(pl.pubKeyOperator);
        keyIDVoting = pl.keyIDVoting;
        addr = pl.addr;
        scriptPayout = pl.scriptPayout;
        scriptOperatorPayout = pl.scriptOperatorPayout;
    }
    template <typename Stream>
    CDeterministicMNState(deserialize_type, Stream& s) { s >> *this; }

    SERIALIZE_METHODS(CDeterministicMNState, obj)
    {
        READWRITE(obj.nRegisteredHeight);
        READWRITE(obj.nLastPaidHeight);
        READWRITE(obj.nPoSePenalty);
        READWRITE(obj.nPoSeRevivedHeight);
        READWRITE(obj.nPoSeBanHeight);
        READWRITE(obj.nRevocationReason);
        READWRITE(obj.confirmedHash);
        READWRITE(obj.confirmedHashWithProRegTxHash);
        READWRITE(obj.keyIDOwner);
        READWRITE(obj.pubKeyOperator);
        READWRITE(obj.keyIDVoting);
        READWRITE(obj.addr);
        READWRITE(obj.scriptPayout);
        READWRITE(obj.scriptOperatorPayout);
    }

    void ResetOperatorFields()
    {
        pubKeyOperator.Set(CBLSPublicKey());
        addr = CService();
        scriptOperatorPayout = CScript();
        nRevocationReason = ProUpRevPL::REASON_NOT_SPECIFIED;
    }
    void BanIfNotBanned(int height)
    {
        if (nPoSeBanHeight == -1) {
            nPoSeBanHeight = height;
        }
    }
    void UpdateConfirmedHash(const uint256& _proTxHash, const uint256& _confirmedHash)
    {
        confirmedHash = _confirmedHash;
        CSHA256 h;
        h.Write(_proTxHash.begin(), _proTxHash.size());
        h.Write(_confirmedHash.begin(), _confirmedHash.size());
        h.Finalize(confirmedHashWithProRegTxHash.begin());
    }

public:
    std::string ToString() const;
    void ToJson(UniValue& obj) const;
};
typedef std::shared_ptr<CDeterministicMNState> CDeterministicMNStatePtr;
typedef std::shared_ptr<const CDeterministicMNState> CDeterministicMNStateCPtr;

class CDeterministicMNStateDiff
{
public:
    enum Field : uint32_t {
        Field_nRegisteredHeight                 = 0x0001,
        Field_nLastPaidHeight                   = 0x0002,
        Field_nPoSePenalty                      = 0x0004,
        Field_nPoSeRevivedHeight                = 0x0008,
        Field_nPoSeBanHeight                    = 0x0010,
        Field_nRevocationReason                 = 0x0020,
        Field_confirmedHash                     = 0x0040,
        Field_confirmedHashWithProRegTxHash     = 0x0080,
        Field_keyIDOwner                        = 0x0100,
        Field_pubKeyOperator                     = 0x0200,
        Field_keyIDVoting                       = 0x0400,
        Field_addr                              = 0x0800,
        Field_scriptPayout                      = 0x1000,
        Field_scriptOperatorPayout              = 0x2000,
    };

#define DMN_STATE_DIFF_ALL_FIELDS \
    DMN_STATE_DIFF_LINE(nRegisteredHeight) \
    DMN_STATE_DIFF_LINE(nLastPaidHeight) \
    DMN_STATE_DIFF_LINE(nPoSePenalty) \
    DMN_STATE_DIFF_LINE(nPoSeRevivedHeight) \
    DMN_STATE_DIFF_LINE(nPoSeBanHeight) \
    DMN_STATE_DIFF_LINE(nRevocationReason) \
    DMN_STATE_DIFF_LINE(confirmedHash) \
    DMN_STATE_DIFF_LINE(confirmedHashWithProRegTxHash) \
    DMN_STATE_DIFF_LINE(keyIDOwner) \
    DMN_STATE_DIFF_LINE(pubKeyOperator) \
    DMN_STATE_DIFF_LINE(keyIDVoting) \
    DMN_STATE_DIFF_LINE(addr) \
    DMN_STATE_DIFF_LINE(scriptPayout) \
    DMN_STATE_DIFF_LINE(scriptOperatorPayout)

public:
    uint32_t fields{0};
    // we reuse the state class, but only the members as noted by fields are valid
    CDeterministicMNState state;

public:
    CDeterministicMNStateDiff() {}
    CDeterministicMNStateDiff(const CDeterministicMNState& a, const CDeterministicMNState& b)
    {
#define DMN_STATE_DIFF_LINE(f) if (a.f != b.f) { state.f = b.f; fields |= Field_##f; }
        DMN_STATE_DIFF_ALL_FIELDS
#undef DMN_STATE_DIFF_LINE
    }

    SERIALIZE_METHODS(CDeterministicMNStateDiff, obj)
    {
        READWRITE(VARINT(obj.fields));
#define DMN_STATE_DIFF_LINE(f) if (obj.fields & Field_##f) READWRITE(obj.state.f);
        DMN_STATE_DIFF_ALL_FIELDS
#undef DMN_STATE_DIFF_LINE
    }

    void ApplyToState(CDeterministicMNState& target) const
    {
#define DMN_STATE_DIFF_LINE(f) if (fields & Field_##f) target.f = state.f;
        DMN_STATE_DIFF_ALL_FIELDS
#undef DMN_STATE_DIFF_LINE
    }
};

class CDeterministicMN
{
private:
    uint64_t internalId{std::numeric_limits<uint64_t>::max()};

public:
    CDeterministicMN() = delete; // no default constructor, must specify internalId
    CDeterministicMN(uint64_t _internalId) : internalId(_internalId)
    {
        // only non-initial values
        assert(_internalId != std::numeric_limits<uint64_t>::max());
    }
    // TODO: can be removed in a future version
    CDeterministicMN(const CDeterministicMN& mn, uint64_t _internalId) : CDeterministicMN(mn) {
        // only non-initial values
        assert(_internalId != std::numeric_limits<uint64_t>::max());
        internalId = _internalId;
    }

    template <typename Stream>
    CDeterministicMN(deserialize_type, Stream& s)
    {
        s >> *this;
    }

    uint256 proTxHash;
    COutPoint collateralOutpoint;
    uint16_t nOperatorReward;
    CDeterministicMNStateCPtr pdmnState;

public:
    SERIALIZE_METHODS(CDeterministicMN, obj)
    {
        READWRITE(obj.proTxHash);
        READWRITE(VARINT(obj.internalId));
        READWRITE(obj.collateralOutpoint);
        READWRITE(obj.nOperatorReward);
        READWRITE(obj.pdmnState);
    }

    uint64_t GetInternalId() const;
    bool IsPoSeBanned() const { return pdmnState->nPoSeBanHeight != -1; }

    std::string ToString() const;
    void ToJson(UniValue& obj) const;
};

typedef std::shared_ptr<const CDeterministicMN> CDeterministicMNCPtr;

class CDeterministicMNListDiff;

template <typename Stream, typename K, typename T, typename Hash, typename Equal>
void SerializeImmerMap(Stream& os, const immer::map<K, T, Hash, Equal>& m)
{
    WriteCompactSize(os, m.size());
    for (typename immer::map<K, T, Hash, Equal>::const_iterator mi = m.begin(); mi != m.end(); ++mi)
        Serialize(os, (*mi));
}

template <typename Stream, typename K, typename T, typename Hash, typename Equal>
void UnserializeImmerMap(Stream& is, immer::map<K, T, Hash, Equal>& m)
{
    m = immer::map<K, T, Hash, Equal>();
    unsigned int nSize = ReadCompactSize(is);
    for (unsigned int i = 0; i < nSize; i++) {
        std::pair<K, T> item;
        Unserialize(is, item);
        m = m.set(item.first, item.second);
    }
}

// For some reason the compiler is not able to choose the correct Serialize/Deserialize methods without a specialized
// version of SerReadWrite. It otherwise always chooses the version that calls a.Serialize()
template<typename Stream, typename K, typename T, typename Hash, typename Equal>
inline void SerReadWrite(Stream& s, const immer::map<K, T, Hash, Equal>& m, CSerActionSerialize ser_action)
{
    ::SerializeImmerMap(s, m);
}

template<typename Stream, typename K, typename T, typename Hash, typename Equal>
inline void SerReadWrite(Stream& s, immer::map<K, T, Hash, Equal>& obj, CSerActionUnserialize ser_action)
{
    ::UnserializeImmerMap(s, obj);
}

class CDeterministicMNList
{
public:
    typedef immer::map<uint256, CDeterministicMNCPtr> MnMap;
    typedef immer::map<uint64_t, uint256> MnInternalIdMap;
    typedef immer::map<uint256, std::pair<uint256, uint32_t> > MnUniquePropertyMap;

private:
    uint256 blockHash;
    int nHeight{-1};
    uint32_t nTotalRegisteredCount{0};
    MnMap mnMap;
    MnInternalIdMap mnInternalIdMap;

    // map of unique properties like address and keys
    // we keep track of this as checking for duplicates would otherwise be painfully slow
    MnUniquePropertyMap mnUniquePropertyMap;

public:
    CDeterministicMNList() {}
    explicit CDeterministicMNList(const uint256& _blockHash, int _height, uint32_t _totalRegisteredCount) :
        blockHash(_blockHash),
        nHeight(_height),
        nTotalRegisteredCount(_totalRegisteredCount)
    {
    }

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        s << blockHash;
        s << nHeight;
        s << nTotalRegisteredCount;
        // Serialize the map as a vector
        WriteCompactSize(s, mnMap.size());
        for (const auto& p : mnMap) {
            s << *p.second;
        }
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        mnMap = MnMap();
        mnUniquePropertyMap = MnUniquePropertyMap();
        mnInternalIdMap = MnInternalIdMap();

        s >> blockHash;
        s >> nHeight;
        s >> nTotalRegisteredCount;
        size_t cnt = ReadCompactSize(s);
        for (size_t i = 0; i < cnt; i++) {
            AddMN(std::make_shared<CDeterministicMN>(deserialize, s), false);
        }
    }

public:
    size_t GetAllMNsCount() const
    {
        return mnMap.size();
    }

    size_t GetValidMNsCount() const
    {
        size_t count = 0;
        for (const auto& p : mnMap) {
            if (!p.second->IsPoSeBanned()) {
                count++;
            }
        }
        return count;
    }

    template <typename Callback>
    void ForEachMN(bool onlyValid, Callback&& cb) const
    {
        for (const auto& p : mnMap) {
            if (!onlyValid || !p.second->IsPoSeBanned()) {
                cb(p.second);
            }
        }
    }

public:
    const uint256& GetBlockHash() const      { return blockHash; }
    int GetHeight() const                    { return nHeight; }
    uint32_t GetTotalRegisteredCount() const { return nTotalRegisteredCount; }
    void SetHeight(int _height)                  { nHeight = _height; }
    void SetBlockHash(const uint256& _blockHash) { blockHash = _blockHash; }

    bool HasMN(const uint256& proTxHash) const
    {
        return GetMN(proTxHash) != nullptr;
    }
    bool HasMNByCollateral(const COutPoint& collateralOutpoint) const
    {
        return GetMNByCollateral(collateralOutpoint) != nullptr;
    }
    bool HasValidMNByCollateral(const COutPoint& collateralOutpoint) const
    {
        return GetValidMNByCollateral(collateralOutpoint) != nullptr;
    }
    CDeterministicMNCPtr GetMN(const uint256& proTxHash) const;
    CDeterministicMNCPtr GetValidMN(const uint256& proTxHash) const;
    CDeterministicMNCPtr GetMNByOperatorKey(const CBLSPublicKey& pubKey);
    CDeterministicMNCPtr GetMNByCollateral(const COutPoint& collateralOutpoint) const;
    CDeterministicMNCPtr GetValidMNByCollateral(const COutPoint& collateralOutpoint) const;
    CDeterministicMNCPtr GetMNByService(const CService& service) const;
    CDeterministicMNCPtr GetMNByInternalId(uint64_t internalId) const;
    CDeterministicMNCPtr GetMNPayee() const;

    /**
     * Calculates the projected MN payees for the next *count* blocks. The result is not guaranteed to be correct
     * as PoSe banning might occur later
     * @param count
     * @return
     */
    std::vector<CDeterministicMNCPtr> GetProjectedMNPayees(unsigned int nCount) const;

    /**
     * Calculate a quorum based on the modifier. The resulting list is deterministically sorted by score
     * @param maxSize
     * @param modifier
     * @return
     */
    std::vector<CDeterministicMNCPtr> CalculateQuorum(size_t maxSize, const uint256& modifier) const;
    std::vector<std::pair<arith_uint256, CDeterministicMNCPtr>> CalculateScores(const uint256& modifier) const;

    /**
     * Calculates the maximum penalty which is allowed at the height of this MN list. It is dynamic and might change
     * for every block.
     * @return
     */
    int CalcMaxPoSePenalty() const;

    /**
     * Returns a the given percentage from the max penalty for this MN list. Always use this method to calculate the
     * value later passed to PoSePunish. The percentage should be high enough to take per-block penalty decreasing for MNs
     * into account. This means, if you want to accept 2 failures per payment cycle, you should choose a percentage that
     * is higher then 50%, e.g. 66%.
     * @param percent
     * @return
     */
    int CalcPenalty(int percent) const;

    /**
     * Punishes a MN for misbehavior. If the resulting penalty score of the MN reaches the max penalty, it is banned.
     * Penalty scores are only increased when the MN is not already banned, which means that after banning the penalty
     * might appear lower then the current max penalty, while the MN is still banned.
     * @param proTxHash
     * @param penalty
     */
    void PoSePunish(const uint256& proTxHash, int penalty, bool debugLogs);

    /**
     * Decrease penalty score of MN by 1.
     * Only allowed on non-banned MNs.
     * @param proTxHash
     */
    void PoSeDecrease(const uint256& proTxHash);

    CDeterministicMNListDiff BuildDiff(const CDeterministicMNList& to) const;
    CDeterministicMNList ApplyDiff(const CBlockIndex* pindex, const CDeterministicMNListDiff& diff) const;

    void AddMN(const CDeterministicMNCPtr& dmn, bool fBumpTotalCount = true);
    void UpdateMN(const CDeterministicMNCPtr& oldDmn, const CDeterministicMNStateCPtr& pdmnState);
    void UpdateMN(const uint256& proTxHash, const CDeterministicMNStateCPtr& pdmnState);
    void UpdateMN(const CDeterministicMNCPtr& oldDmn, const CDeterministicMNStateDiff& stateDiff);
    void RemoveMN(const uint256& proTxHash);

    template <typename T>
    bool HasUniqueProperty(const T& v) const
    {
        return mnUniquePropertyMap.count(::SerializeHash(v)) != 0;
    }
    template <typename T>
    CDeterministicMNCPtr GetUniquePropertyMN(const T& v) const
    {
        auto p = mnUniquePropertyMap.find(::SerializeHash(v));
        if (!p) {
            return nullptr;
        }
        return GetMN(p->first);
    }

private:
    template <typename T>
    void AddUniqueProperty(const CDeterministicMNCPtr& dmn, const T& v)
    {
        static const T nullValue;
        assert(v != nullValue);

        auto hash = ::SerializeHash(v);
        auto oldEntry = mnUniquePropertyMap.find(hash);
        assert(!oldEntry || oldEntry->first == dmn->proTxHash);
        std::pair<uint256, uint32_t> newEntry(dmn->proTxHash, 1);
        if (oldEntry) {
            newEntry.second = oldEntry->second + 1;
        }
        mnUniquePropertyMap = mnUniquePropertyMap.set(hash, newEntry);
    }
    template <typename T>
    void DeleteUniqueProperty(const CDeterministicMNCPtr& dmn, const T& oldValue)
    {
        static const T nullValue;
        assert(oldValue != nullValue);

        auto oldHash = ::SerializeHash(oldValue);
        auto p = mnUniquePropertyMap.find(oldHash);
        assert(p && p->first == dmn->proTxHash);
        if (p->second == 1) {
            mnUniquePropertyMap = mnUniquePropertyMap.erase(oldHash);
        } else {
            mnUniquePropertyMap = mnUniquePropertyMap.set(oldHash, std::make_pair(dmn->proTxHash, p->second - 1));
        }
    }
    template <typename T>
    void UpdateUniqueProperty(const CDeterministicMNCPtr& dmn, const T& oldValue, const T& newValue)
    {
        if (oldValue == newValue) {
            return;
        }
        static const T nullValue;

        if (oldValue != nullValue) {
            DeleteUniqueProperty(dmn, oldValue);
        }

        if (newValue != nullValue) {
            AddUniqueProperty(dmn, newValue);
        }
    }
};

class CDeterministicMNListDiff
{
public:
    int nHeight{-1}; //memory only

    std::vector<CDeterministicMNCPtr> addedMNs;
    // keys are all relating to the internalId of MNs
    std::map<uint64_t, CDeterministicMNStateDiff> updatedMNs;
    std::set<uint64_t> removedMns;

public:
    template<typename Stream>
    void Serialize(Stream& s) const
    {
        s << addedMNs;
        WriteCompactSize(s, updatedMNs.size());
        for (const auto& p : updatedMNs) {
            s << VARINT(p.first);
            s << p.second;
        }
        WriteCompactSize(s, removedMns.size());
        for (const auto& p : removedMns) {
            s << VARINT(p);
        }
    }

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        updatedMNs.clear();
        removedMns.clear();

        size_t tmp;
        uint64_t tmp2;
        s >> addedMNs;
        tmp = ReadCompactSize(s);
        for (size_t i = 0; i < tmp; i++) {
            CDeterministicMNStateDiff diff;
            s >> VARINT(tmp2);
            s >> diff;
            updatedMNs.emplace(tmp2, std::move(diff));
        }
        tmp = ReadCompactSize(s);
        for (size_t i = 0; i < tmp; i++) {
            s >> VARINT(tmp2);
            removedMns.emplace(tmp2);
        }
    }

public:
    bool HasChanges() const
    {
        return !addedMNs.empty() || !updatedMNs.empty() || !removedMns.empty();
    }
};

class CDeterministicMNManager
{
    static const int DISK_SNAPSHOT_PERIOD = 1440; // once per day
    static const int DISK_SNAPSHOTS = 3; // keep cache for 3 disk snapshots to have 2 full days covered
    static const int LIST_DIFFS_CACHE_SIZE = DISK_SNAPSHOT_PERIOD * DISK_SNAPSHOTS;

public:
    mutable RecursiveMutex cs;

private:
    CEvoDB& evoDb;

    std::unordered_map<uint256, CDeterministicMNList, StaticSaltedHasher> mnListsCache;
    std::unordered_map<uint256, CDeterministicMNListDiff, StaticSaltedHasher> mnListDiffsCache;
    const CBlockIndex* tipIndex{nullptr};

public:
    explicit CDeterministicMNManager(CEvoDB& _evoDb);

    bool ProcessBlock(const CBlock& block, const CBlockIndex* pindex, CValidationState& state, bool fJustCheck);
    bool UndoBlock(const CBlock& block, const CBlockIndex* pindex);

    void UpdatedBlockTip(const CBlockIndex* pindex);

    // the returned list will not contain the correct block hash (we can't know it yet as the coinbase TX is not updated yet)
    bool BuildNewListFromBlock(const CBlock& block, const CBlockIndex* pindexPrev, CValidationState& state, CDeterministicMNList& mnListRet, bool debugLogs);
    void DecreasePoSePenalties(CDeterministicMNList& mnList);

    // to return a valid list, it must have been built first, so never call it with a block not-yet connected (e.g. from CheckBlock).
    CDeterministicMNList GetListForBlock(const CBlockIndex* pindex);
    CDeterministicMNList GetListAtChainTip();

    // Whether DMNs are enforced at provided height, or at the chain-tip
    bool IsDIP3Enforced(int nHeight) const;
    bool IsDIP3Enforced() const;

    // Whether Legacy MNs are disabled at provided height, or at the chain-tip
    bool LegacyMNObsolete(int nHeight) const;
    bool LegacyMNObsolete() const;

private:
    void CleanupCache(int nHeight);
};

extern std::unique_ptr<CDeterministicMNManager> deterministicMNManager;

#endif //DIGIWAGE_DETERMINISTICMNS_H
