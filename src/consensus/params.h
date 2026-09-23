// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include <uint256.h>

#include <chrono>
#include <limits>
#include <map>
#include <vector>

namespace Consensus {

/**
 * A buried deployment is one where the height of the activation has been hardcoded into
 * the client implementation long after the consensus change has activated. See BIP 90.
 */
enum BuriedDeployment : int16_t {
    // buried deployments get negative values to avoid overlap with DeploymentPos
    DEPLOYMENT_HEIGHTINCB = std::numeric_limits<int16_t>::min(),
    DEPLOYMENT_CLTV,
    DEPLOYMENT_DERSIG,
    DEPLOYMENT_CSV,
    DEPLOYMENT_SEGWIT,
};
constexpr bool ValidDeployment(BuriedDeployment dep) { return dep <= DEPLOYMENT_SEGWIT; }

enum DeploymentPos : uint16_t {
    DEPLOYMENT_TESTDUMMY,
    DEPLOYMENT_TAPROOT, // Deployment of Schnorr/Taproot (BIPs 340-342)
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in deploymentinfo.cpp
    MAX_VERSION_BITS_DEPLOYMENTS
};
constexpr bool ValidDeployment(DeploymentPos dep) { return dep < MAX_VERSION_BITS_DEPLOYMENTS; }

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit{28};
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime{NEVER_ACTIVE};
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout{NEVER_ACTIVE};
    /** If lock in occurs, delay activation until at least this block
     *  height.  Note that activation will only occur on a retarget
     *  boundary.
     */
    int min_activation_height{0};

    /** Constant for nTimeout very far in the future. */
    static constexpr int64_t NO_TIMEOUT = std::numeric_limits<int64_t>::max();

    /** Special value for nStartTime indicating that the deployment is always active.
     *  This is useful for testing, as it means tests don't need to deal with the activation
     *  process (which takes at least 3 BIP9 intervals). Only tests that specifically test the
     *  behaviour during activation cannot use this. */
    static constexpr int64_t ALWAYS_ACTIVE = -1;

    /** Special value for nStartTime indicating that the deployment is never active.
     *  This is useful for integrating the code changes for a new feature
     *  prior to deploying it on some or all networks. */
    static constexpr int64_t NEVER_ACTIVE = -2;
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    bool digiwage_history{false};
    // The chain is the legacy DigiWage chain itself (mainnet, legacytest), so
    // every legacy block rule applies below the contract fork. False on
    // rehearsal chains such as forktest that only reuse the legacy kernel.
    bool digiwage_legacy_chain{false};
    int digiwage_stake_modifier_v2_height{std::numeric_limits<int>::max()};
    int digiwage_zerocoin_height{std::numeric_limits<int>::max()};
    int digiwage_rhf_height{std::numeric_limits<int>::max()};
    int digiwage_contract_height{std::numeric_limits<int>::max()};
    int digiwage_stake_min_depth{0};
    uint256 digiwage_pos_limit_v2{};
    // Legacy rules that used to be hardcoded to mainnet values. The defaults
    // are those mainnet values, so only networks that override them change.
    // Last legacy PoW height; the legacy PoS kernel applies above it.
    int digiwage_last_pow_height{1000};
    // Lowest previous-block height whose successor is retargeted with the
    // legacy PoS formula instead of Dark Gravity Wave.
    int digiwage_pos_retarget_prev_height{1001};
    int digiwage_stake_modifier_new_selection_height{1551935};
    int64_t digiwage_target_spacing{60};
    int64_t digiwage_target_timespan{40 * 60};
    int64_t digiwage_target_timespan_v2{30 * 60};
    int64_t digiwage_time_slot{15};
    // Legacy CStakeKernel hashes with a zero v1 modifier when no modifier was
    // generated a selection interval after the coin; it only logs an error.
    // Mainnet's one hour stake age made that case unreachable.
    bool digiwage_old_modifier_zero_fallback{false};
    // Legacy ZC_TimeStart: blocks timed after it get the higher legacy
    // CheckBlock sigop limit (MAX_BLOCK_SIGOPS_CURRENT).
    int64_t digiwage_zerocoin_time_start{2526352132};
    // Legacy SPORK_17_COLDSTAKING_ENFORCEMENT: whether P2CS outputs may be
    // created before the contract fork.
    bool digiwage_cold_staking_allowed{true};
    uint256 hashGenesisBlock;
    int nSubsidyHalvingInterval;
    int nSubsidyHalvingIntervalV2;
    /**
     * Hashes of blocks that
     * - are known to be consensus valid, and
     * - buried in the chain, and
     * - fail if the default script verify flags are applied.
     */
    std::map<uint256, uint32_t> script_flag_exceptions;
    /** Block height and hash at which BIP34 becomes active */
    int BIP34Height;
    uint256 BIP34Hash;
    /** Block height at which BIP65 becomes active */
    int BIP65Height;
    /** Block height at which BIP66 becomes active */
    int BIP66Height;
    /** Block height at which CSV (BIP68, BIP112 and BIP113) becomes active */
    int CSVHeight;
    /** Block height at which Segwit (BIP141, BIP143 and BIP147) becomes active.
     * Note that segwit v0 script rules are enforced on all blocks except the
     * BIP 16 exception blocks. */
    int SegwitHeight;
    /** Don't warn about unknown BIP 9 activations below this height.
     * This prevents us from warning about the CSV and segwit activations. */
    int MinBIP9WarningHeight;
    /** Block height at which QIP5 becomes active */
    int QIP5Height;
    /** Block height at which QIP6 becomes active */
    int QIP6Height;
    /** Block height at which QIP7 becomes active */
    int QIP7Height;
    /** Block height at which QIP9 becomes active */
    int QIP9Height;
    /** Block height at which Offline Staking becomes active */
    int nOfflineStakeHeight;
    /** Block height at which Reduce Block Time becomes active */
    int nReduceBlocktimeHeight;
    /** Block height at which EVM Muir Glacier fork becomes active */
    int nMuirGlacierHeight;
    /** Block height at which EVM London fork becomes active */
    int nLondonHeight;
    /** Block height at which EVM Shanghai fork becomes active */
    int nShanghaiHeight;
    /**
     * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargeting period,
     * (nPowTargetTimespan / nPowTargetSpacing) which is also used for BIP9 deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t nRuleChangeActivationThreshold;
    uint32_t nMinerConfirmationWindow;
    BIP9Deployment vDeployments[MAX_VERSION_BITS_DEPLOYMENTS];
    /** Proof of work parameters */
    uint256 powLimit;
    uint256 posLimit;
    uint256 QIP9PosLimit;
    uint256 RBTPosLimit;
    bool fPowAllowMinDifficultyBlocks;
    bool fPowNoRetargeting;
    bool fPoSNoRetargeting;
    int64_t nPowTargetSpacing;
    int64_t nRBTPowTargetSpacing;
    int64_t nPowTargetTimespan;
    int64_t nPowTargetTimespanV2;
    int64_t nRBTPowTargetTimespan;
    std::chrono::seconds TargetSpacingChrono(int height) const
    {
        return std::chrono::seconds{TargetSpacing(height)};
    }
    int64_t DifficultyAdjustmentInterval(int height) const
    {
        int64_t targetTimespan = TargetTimespan(height);
        int64_t targetSpacing = TargetSpacing(height);
        return targetTimespan / targetSpacing;
    }
    /** The best chain should have at least this much work */
    uint256 nMinimumChainWork;
    /** By default assume that the signatures in ancestors of this block are valid */
    uint256 defaultAssumeValid;

    /**
     * If true, witness commitments contain a payload equal to a Bitcoin Script solution
     * to the signet challenge. See BIP325.
     */
    bool signet_blocks{false};
    std::vector<uint8_t> signet_challenge;

    int DeploymentHeight(BuriedDeployment dep) const
    {
        switch (dep) {
        case DEPLOYMENT_HEIGHTINCB:
            return BIP34Height;
        case DEPLOYMENT_CLTV:
            return BIP65Height;
        case DEPLOYMENT_DERSIG:
            return BIP66Height;
        case DEPLOYMENT_CSV:
            return CSVHeight;
        case DEPLOYMENT_SEGWIT:
            return SegwitHeight;
        } // no default case, so the compiler can warn about missing cases
        return std::numeric_limits<int>::max();
    }

    int nLastPOWBlock;
    int nFirstMPoSBlock;
    int nMPoSRewardRecipients;
    int nFixUTXOCacheHFHeight;
    int nEnableHeaderSignatureHeight;
    /** Block sync-checkpoint span*/
    int nCheckpointSpan;
    int nRBTCheckpointSpan;
    uint160 delegationsAddress;
    int nLastMPoSBlock;
    int nLastBigReward;
    uint32_t nStakeTimestampMask;
    uint32_t nRBTStakeTimestampMask;
    int64_t nBlocktimeDownscaleFactor;
    /** Coinbase transaction outputs can only be spent after this number of new blocks (network rule) */
    int nCoinbaseMaturity;
    int nRBTCoinbaseMaturity;
    int64_t StakeTimestampMask(int height) const
    {
        return height < nReduceBlocktimeHeight ? nStakeTimestampMask : nRBTStakeTimestampMask;
    }
    int64_t MinStakeTimestampMask() const
    {
        return nRBTStakeTimestampMask;
    }
    // Spacing of the times the staker tries. Legacy time protocol v2 (from the
    // RHF) requires block times on multiples of digiwage_time_slot.
    int64_t StakeTimeStep(int height) const
    {
        if (digiwage_legacy_chain && height >= digiwage_rhf_height) return digiwage_time_slot;
        return StakeTimestampMask(height) + 1;
    }
    // Rounds a time down to the staker grid; equals nTime & ~mask off the
    // legacy slot rule, since the step is then mask + 1 (a power of two).
    uint32_t AlignStakeTime(uint32_t nTime, int height) const
    {
        return nTime - nTime % StakeTimeStep(height);
    }
    int SubsidyHalvingInterval(int height) const
    {
        return height < nReduceBlocktimeHeight ? nSubsidyHalvingInterval : nSubsidyHalvingIntervalV2;
    }
    int64_t BlocktimeDownscaleFactor(int height) const
    {
        return height < nReduceBlocktimeHeight ? 1 : nBlocktimeDownscaleFactor;
    }
    int64_t TargetSpacing(int height) const
    {
        return height < nReduceBlocktimeHeight ? nPowTargetSpacing : nRBTPowTargetSpacing;
    }
    int SubsidyHalvingWeight(int height) const
    {
        if(height <= nLastBigReward)
            return 0;

        int blocktimeDownscaleFactor = BlocktimeDownscaleFactor(height);
        int blockCount = height - nLastBigReward;
        int beforeDownscale = blocktimeDownscaleFactor == 1 ? 0 : nReduceBlocktimeHeight - nLastBigReward - 1;
        int subsidyHalvingWeight = blockCount - beforeDownscale + beforeDownscale * blocktimeDownscaleFactor;
        return subsidyHalvingWeight;
    }
    int64_t TimestampDownscaleFactor(int height) const
    {
        return height < nReduceBlocktimeHeight ? 1 : (nStakeTimestampMask + 1) / (nRBTStakeTimestampMask + 1);
    }
    int64_t TargetTimespan(int height) const
    {
        return height < QIP9Height ? nPowTargetTimespan : 
            (height < nReduceBlocktimeHeight ? nPowTargetTimespanV2 : nRBTPowTargetTimespan);
    }
    int CheckpointSpan(int height) const
    {
        return height < nReduceBlocktimeHeight ? nCheckpointSpan : nRBTCheckpointSpan;
    }
    int CoinbaseMaturity(int height) const
    {
        return height < nReduceBlocktimeHeight ? nCoinbaseMaturity : nRBTCoinbaseMaturity;
    }
    int MaxCheckpointSpan() const
    {
        return nCheckpointSpan <= nRBTCheckpointSpan ? nRBTCheckpointSpan : nCheckpointSpan;
    }
};

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PARAMS_H
