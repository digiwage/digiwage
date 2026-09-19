#include <digiwage/pos.h>

#include <digiwage/stakemodifier.h>

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/validation.h>
#include <hash.h>
#include <primitives/block.h>
#include <streams.h>
#include <tinyformat.h>

bool CheckDigiwageStakeKernel(const CBlockIndex* prev, unsigned int nBits,
                              const CBlockIndex* origin, CAmount value,
                              const COutPoint& prevout, uint32_t nTimeBlock,
                              uint256& proof_hash)
{
    if (!prev || !origin) return false;
    const Consensus::Params& consensus = Params().GetConsensus();
    const int height = prev->nHeight + 1;
    // Original CPivStake::GetUniqueness serializes position before txid.
    CDataStream stream(SER_GETHASH, 0);
    if (height < consensus.digiwage_stake_modifier_v2_height) {
        uint64_t modifier = 0;
        if (!GetDigiwageOldModifier(origin, prev, modifier)) return false;
        stream << modifier;
    } else {
        stream << prev->nStakeModifier;
    }
    stream << origin->nTime << prevout.n << prevout.hash << nTimeBlock;
    proof_hash = Hash(stream);

    arith_uint256 target;
    target.SetCompact(nBits);
    target *= arith_uint256(uint64_t(value / 100));
    return UintToArith256(proof_hash) < target;
}

bool CheckDigiwageKernel(const CBlock& block, const CBlockIndex* prev,
                         CCoinsViewCache& view, BlockValidationState& state,
                         uint256& proof_hash)
{
    if (!prev || !block.IsProofOfStake() || block.vtx[1]->vin.empty()) {
        return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-digiwage-stake-shape");
    }
    const COutPoint& outpoint = block.vtx[1]->vin[0].prevout;
    Coin coin;
    if (!view.GetCoin(outpoint, coin) || coin.out.nValue <= 0) {
        return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-digiwage-stake-input");
    }
    const CBlockIndex* origin = prev->GetAncestor(coin.nHeight);
    const Consensus::Params& consensus = Params().GetConsensus();
    const int height = prev->nHeight + 1;
    if (height >= consensus.digiwage_rhf_height - 1 &&
        height - coin.nHeight < consensus.digiwage_stake_min_depth) {
        return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-digiwage-stake-depth");
    }
    if (!origin) {
        return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "digiwage-modifier-unavailable");
    }
    if (height < consensus.digiwage_stake_modifier_v2_height) {
        uint64_t old_modifier = 0;
        if (!GetDigiwageOldModifier(origin, prev, old_modifier)) {
            return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "digiwage-modifier-unavailable");
        }
    }
    if (!CheckDigiwageStakeKernel(prev, block.nBits, origin, coin.out.nValue, outpoint, block.nTime, proof_hash)) {
        arith_uint256 target;
        target.SetCompact(block.nBits);
        target *= arith_uint256(uint64_t(coin.out.nValue / 100));
        return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-digiwage-stake-kernel",
                             strprintf("origin=%d proof=%s target=%s value=%d",
                                       origin->nHeight, proof_hash.ToString(),
                                       ArithToUint256(target).ToString(), coin.out.nValue));
    }
    return true;
}
