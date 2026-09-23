#include <digiwage/stakemodifier.h>

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <hash.h>
#include <streams.h>

#include <algorithm>
#include <set>
#include <vector>

namespace {
constexpr int64_t MODIFIER_INTERVAL = 60;
constexpr int64_t OLD_SELECTION_INTERVAL = 2087;

int64_t SelectionIntervalSection(int round)
{
    return MODIFIER_INTERVAL * 63 / (63 + (63 - round) * 2);
}
}

bool ComputeDigiwageStakeModifier(const CBlockIndex* prev, uint64_t first_modifier,
                                  uint64_t& modifier, bool& generated)
{
    modifier = 0;
    generated = false;
    if (!prev) {
        generated = true;
        return true;
    }
    if (prev->nHeight == 0) {
        modifier = first_modifier;
        generated = true;
        return true;
    }

    const CBlockIndex* last_generated = prev;
    while (last_generated->pprev && !last_generated->fDigiwageStakeModifierGenerated) {
        last_generated = last_generated->pprev;
    }
    if (!last_generated->fDigiwageStakeModifierGenerated) return false;
    modifier = last_generated->nDigiwageStakeModifier;
    if (last_generated->GetBlockTime() / MODIFIER_INTERVAL >=
        prev->GetBlockTime() / MODIFIER_INTERVAL) return true;

    const int64_t start = (prev->GetBlockTime() / MODIFIER_INTERVAL) * MODIFIER_INTERVAL - OLD_SELECTION_INTERVAL;
    std::vector<const CBlockIndex*> candidates;
    for (const CBlockIndex* p = prev; p && p->GetBlockTime() >= start; p = p->pprev) {
        candidates.push_back(p);
    }
    std::sort(candidates.begin(), candidates.end(), [](const CBlockIndex* a, const CBlockIndex* b) {
        if (a->GetBlockTime() != b->GetBlockTime()) return a->GetBlockTime() < b->GetBlockTime();
        // Legacy uint256 ordered numerically. Modern uint256 orders its
        // internal little-endian bytes lexicographically.
        return UintToArith256(a->GetBlockHash()) < UintToArith256(b->GetBlockHash());
    });

    const bool new_selection = !candidates.empty() && candidates.front()->nHeight >=
        Params().GetConsensus().digiwage_stake_modifier_new_selection_height;
    std::set<const CBlockIndex*> selected;
    int64_t stop = start;
    uint64_t next = 0;
    for (int round = 0; round < std::min<int>(64, candidates.size()); ++round) {
        stop += SelectionIntervalSection(round);
        const CBlockIndex* best = nullptr;
        arith_uint256 best_hash;
        for (const CBlockIndex* candidate : candidates) {
            if (best && candidate->GetBlockTime() > stop) break;
            if (selected.count(candidate)) continue;
            const uint256 proof = (new_selection || !candidate->IsProofOfStake()) ?
                candidate->GetBlockHash() : uint256{};
            CDataStream stream(SER_GETHASH, 0);
            stream << proof << modifier;
            arith_uint256 selection_hash = UintToArith256(Hash(stream));
            if (candidate->IsProofOfStake()) selection_hash >>= 32;
            if (!best || selection_hash < best_hash) {
                best = candidate;
                best_hash = selection_hash;
            }
        }
        if (!best) return false;
        next |= uint64_t(best->GetBlockHash().GetUint64(0) & 1) << round;
        selected.insert(best);
    }
    modifier = next;
    generated = true;
    return true;
}

bool GetDigiwageOldModifier(const CBlockIndex* origin, const CBlockIndex* tip,
                           uint64_t& modifier)
{
    if (!origin || !tip || origin->nHeight >= tip->nHeight) return false;
    int64_t modifier_time = origin->GetBlockTime();
    const CBlockIndex* current = origin;
    for (int height = origin->nHeight + 1; height <= tip->nHeight; ++height) {
        current = tip->GetAncestor(height);
        if (!current) return false;
        if (current->fDigiwageStakeModifierGenerated) modifier_time = current->GetBlockTime();
        if (modifier_time >= origin->GetBlockTime() + OLD_SELECTION_INTERVAL) {
            modifier = current->nDigiwageStakeModifier;
            return true;
        }
    }
    return false;
}
