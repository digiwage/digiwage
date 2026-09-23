// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>

#include <algorithm>

namespace {
    // returns a * exp(p/q) where |p/q| is small
    arith_uint256 mul_exp(arith_uint256 a, int64_t p, int64_t q)
    {
        bool isNegative = p < 0;
        uint64_t abs_p = p >= 0 ? p : -p;
        arith_uint256 result = a;
        uint64_t n = 0;
        while (a > 0) {
            ++n;
            a = a * abs_p / q / n;
            if (isNegative && (n % 2 == 1)) {
                result -= a;
            } else {
                result += a;
            }
        }
        return result;
    }
}

// ppcoin: find last block index up to pindex
const CBlockIndex* GetLastBlockIndex(const CBlockIndex* pindex, bool fProofOfStake)
{
    //CBlockIndex will be updated with information about the proof type later
    while (pindex && pindex->pprev && (pindex->IsProofOfStake() != fProofOfStake))
        pindex = pindex->pprev;
    return pindex;
}

inline arith_uint256 GetLimit(int nHeight, const Consensus::Params& params, bool fProofOfStake)
{
    if(fProofOfStake) {
        if(nHeight < params.QIP9Height) {
            return UintToArith256(params.posLimit);
        } else if(nHeight < params.nReduceBlocktimeHeight) {
            return UintToArith256(params.QIP9PosLimit);
        } else {
            return UintToArith256(params.RBTPosLimit);
        }
    } else {
        return UintToArith256(params.powLimit);
    }
}

static unsigned int DigiwageDarkGravityWave(const CBlockIndex* last, const Consensus::Params& params)
{
    const arith_uint256 limit = UintToArith256(params.powLimit);
    if (!last || last->nHeight < 24) return limit.GetCompact();

    const CBlockIndex* reading = last;
    arith_uint256 average;
    arith_uint256 previous_average;
    int64_t actual_timespan = 0;
    int64_t last_time = 0;
    int64_t count = 0;
    for (unsigned int i = 1; reading && reading->nHeight > 0 && i <= 24; ++i) {
        ++count;
        const arith_uint256 target = arith_uint256().SetCompact(reading->nBits);
        if (count == 1) average = target;
        else average = arith_uint256(((previous_average * count) + target) / (count + 1));
        previous_average = average;
        if (last_time > 0) actual_timespan += last_time - reading->GetBlockTime();
        last_time = reading->GetBlockTime();
        reading = reading->pprev;
    }
    const int64_t target_timespan = count * params.nPowTargetSpacing;
    actual_timespan = std::max(target_timespan / 3, std::min(actual_timespan, target_timespan * 3));
    average *= actual_timespan;
    average /= target_timespan;
    if (average > limit) average = limit;
    return average.GetCompact();
}

static unsigned int DigiwageNextWork(const CBlockIndex* last, const Consensus::Params& params)
{
    if (!last || last->nHeight < 24) return UintToArith256(params.powLimit).GetCompact();
    // Rehearsal chains (regtest-derived, fPowNoRetargeting) inherit the last
    // imported block's mainnet-era difficulty, which is ~2.7h per block for the
    // rehearsal stake under the DigiWage kernel; retargeting only eases after a
    // slow block, so the first post-history block would stall the chain.
    // Start it at ~2^208 (about 2-3 min expected) and let the normal
    // retarget take over. Gated three ways so it can never apply to mainnet: exact height,
    // fPowNoRetargeting (false on mainnet) and a contract height below 1000
    // (4,000,000 on mainnet).
    if (last->nHeight == 1000 && params.fPowNoRetargeting && params.digiwage_contract_height < 1000) {
        arith_uint256 easy;
        easy.SetCompact(0x1b010000);
        return easy.GetCompact();
    }
    if (last->nHeight < params.digiwage_pos_retarget_prev_height) return DigiwageDarkGravityWave(last, params);


    arith_uint256 target;
    target.SetCompact(last->nBits);
    int64_t spacing = last->GetBlockTime() - last->pprev->GetBlockTime();
    if (spacing < 0) spacing = 1;
    const int64_t target_spacing = params.digiwage_target_spacing;
    const int next_height = last->nHeight + 1;
    const bool time_v2 = next_height >= params.digiwage_rhf_height;
    if (time_v2 && spacing > target_spacing * 10) spacing = target_spacing * 10;
    const int64_t interval = (time_v2 ? params.digiwage_target_timespan_v2 : params.digiwage_target_timespan) / target_spacing;
    if (time_v2 && last->nHeight < params.digiwage_rhf_height) target <<= 4;
    target *= ((interval - 1) * target_spacing + 2 * spacing);
    target /= ((interval + 1) * target_spacing);
    const arith_uint256 limit = UintToArith256(time_v2 ? params.digiwage_pos_limit_v2 : params.posLimit);
    if (target == 0 || target > limit) target = limit;
    return target.GetCompact();
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params, bool fProofOfStake)
{
    if (params.digiwage_history) return DigiwageNextWork(pindexLast, params);

    unsigned int  nTargetLimit = GetLimit(pindexLast ? pindexLast->nHeight+1 : 0, params, fProofOfStake).GetCompact();

    // genesis block
    if (pindexLast == NULL)
        return nTargetLimit;

    // first block
    const CBlockIndex* pindexPrev = GetLastBlockIndex(pindexLast, fProofOfStake);
    if (pindexPrev->pprev == NULL)
        return nTargetLimit;

    // second block
    const CBlockIndex* pindexPrevPrev = GetLastBlockIndex(pindexPrev->pprev, fProofOfStake);
    if (pindexPrevPrev->pprev == NULL)
        return nTargetLimit;

    // min difficulty
    if (params.fPowAllowMinDifficultyBlocks)
    {
        // Special difficulty rule for testnet:
        // If the new block's timestamp is more than 2* 10 minutes
        // then allow mining of a min-difficulty block.
        int nHeight = pindexLast->nHeight + 1;
        if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.TargetSpacing(nHeight)*2)
            return nTargetLimit;
        else
        {
            // Return the last non-special-min-difficulty-rules-block
            const CBlockIndex* pindex = pindexLast;
            while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval(pindex->nHeight) != 0 && pindex->nBits == nTargetLimit)
                pindex = pindex->pprev;
            return pindex->nBits;
        }
        return pindexLast->nBits;
    }

    return CalculateNextWorkRequired(pindexPrev, pindexPrevPrev->GetBlockTime(), params, fProofOfStake);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params, bool fProofOfStake)
{
    if(fProofOfStake){
        if (params.fPoSNoRetargeting)
            return pindexLast->nBits;
    }else{
        if (params.fPowNoRetargeting)
            return pindexLast->nBits;
    }
    // Limit adjustment step
    int nHeight = pindexLast->nHeight + 1;
    int64_t nTargetSpacing = params.TargetSpacing(nHeight);
    int64_t nActualSpacing = pindexLast->GetBlockTime() - nFirstBlockTime;
    // Retarget
    const arith_uint256 bnTargetLimit = GetLimit(nHeight, params, fProofOfStake);
    // ppcoin: target change every block
    // ppcoin: retarget with exponential moving toward target spacing
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    int64_t nInterval = params.DifficultyAdjustmentInterval(nHeight); 

    if (nHeight < params.QIP9Height) {
        if (nActualSpacing < 0)
            nActualSpacing = nTargetSpacing;
        if (nActualSpacing > nTargetSpacing * 10)
            nActualSpacing = nTargetSpacing * 10;
        bnNew *= ((nInterval - 1) * nTargetSpacing + nActualSpacing + nActualSpacing);
        bnNew /= ((nInterval + 1) * nTargetSpacing);
    } else {
        if (nActualSpacing < 0)
            nActualSpacing = nTargetSpacing;
        if (nActualSpacing > nTargetSpacing * 20)
            nActualSpacing = nTargetSpacing * 20;
        uint32_t stakeTimestampMask=params.StakeTimestampMask(nHeight);
        bnNew = mul_exp(bnNew, 2 * (nActualSpacing - nTargetSpacing) / (stakeTimestampMask + 1), (nInterval + 1) * nTargetSpacing / (stakeTimestampMask + 1));
    }

    if (bnNew <= 0 || bnNew > bnTargetLimit)
        bnNew = bnTargetLimit;
    return bnNew.GetCompact();
}

// Check that on difficulty adjustments, the new difficulty does not increase
// or decrease beyond the permitted limits.
#ifdef DIGIWAGE_BUILD
bool PermittedDifficultyTransition(const Consensus::Params&, int64_t, uint32_t, uint32_t)
{
    // DigiWage has different difficulty adjustment algorithm than Bitcoin, so checking the borders for the new difficulty value might not be the same.
    // The method is used for unit testing and to reject headers (headerssync.cpp) during synching for Bitcoin depending on the difficulty transition.
    return true;
}
#else
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    if (params.fPowAllowMinDifficultyBlocks) return true;

    if (height % params.DifficultyAdjustmentInterval(height) == 0) {
        int64_t smallest_timespan = params.nPowTargetTimespan/4;
        int64_t largest_timespan = params.nPowTargetTimespan*4;

        const arith_uint256 pow_limit = UintToArith256(params.powLimit);
        arith_uint256 observed_new_target;
        observed_new_target.SetCompact(new_nbits);

        // Calculate the largest difficulty value possible:
        arith_uint256 largest_difficulty_target;
        largest_difficulty_target.SetCompact(old_nbits);
        largest_difficulty_target *= largest_timespan;
        largest_difficulty_target /= params.nPowTargetTimespan;

        if (largest_difficulty_target > pow_limit) {
            largest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 maximum_new_target;
        maximum_new_target.SetCompact(largest_difficulty_target.GetCompact());
        if (maximum_new_target < observed_new_target) return false;

        // Calculate the smallest difficulty value possible:
        arith_uint256 smallest_difficulty_target;
        smallest_difficulty_target.SetCompact(old_nbits);
        smallest_difficulty_target *= smallest_timespan;
        smallest_difficulty_target /= params.nPowTargetTimespan;

        if (smallest_difficulty_target > pow_limit) {
            smallest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 minimum_new_target;
        minimum_new_target.SetCompact(smallest_difficulty_target.GetCompact());
        if (minimum_new_target > observed_new_target) return false;
    } else if (old_nbits != new_nbits) {
        return false;
    }
    return true;
}
#endif

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
