// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

// #include "amount.h" // <<<--- Original line
#include "../amount.h"  // <<<--- Corrected line: Go up one directory to find amount.h
#include "libzerocoin/Params.h"
#include "uint256.h"
#include <map>
#include <string>

namespace Consensus {

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    bool fPowAllowMinDifficultyBlocks;
    uint256 powLimit;
    uint256 posLimitV1;
    uint256 posLimitV2;
    int nBudgetCycleBlocks;
    int nBudgetFeeConfirmations;
    int nCoinbaseMaturity;
    int nFutureTimeDriftPoW;
    int nFutureTimeDriftPoS;
    int nMasternodeCountDrift;
    CAmount nMaxMoneyOut; // <<-- This will now compile correctly
    int nPoolMaxTransactions;
    int64_t nProposalEstablishmentTime;
    int nStakeMinAge;
    int nStakeMinDepth;
    int64_t nTargetTimespan;
    int64_t nTargetTimespanV2;
    int64_t nTargetSpacing;
    int nTimeSlotLength;
    std::string strObfuscationPoolDummyAddress;

    // spork keys
    std::string strSporkPubKey;

    // height-based activations
    int height_last_PoW;
    int height_last_ZC_AccumCheckpoint;
    int height_start_BIP65;
    int height_start_MessSignaturesV2;
    int height_start_StakeModifierNewSelection;
    int height_start_StakeModifierV2;
    int height_start_TimeProtoV2;
    int height_start_ZC;
    int height_start_ZC_PublicSpends;
    int height_start_ZC_SerialRangeCheck;
    int height_start_ZC_SerialsV2;
    int height_RHF;

    int64_t TargetTimespan(const bool fV2 = true) const { return fV2 ? nTargetTimespanV2 : nTargetTimespan; }
    uint256 ProofOfStakeLimit(const bool fV2) const { return fV2 ? posLimitV2 : posLimitV1; }
    bool MoneyRange(const CAmount& nValue) const { return (nValue >= 0 && nValue <= nMaxMoneyOut); } // <<-- This will now compile correctly
    bool IsMessSigV2(const int nHeight) const { return nHeight >= height_start_MessSignaturesV2; }
    bool IsPastRHFBlock(const int nHeight) const {return nHeight >= height_RHF; }
    bool IsTimeProtocolV2(const int nHeight) const { return nHeight >= height_start_TimeProtoV2; }
    bool IsStakeModifierV2(const int nHeight) const { return nHeight >= height_start_StakeModifierV2; }

    int FutureBlockTimeDrift(const int nHeight) const
    {
        // PoS (TimeV2): 14 seconds
        if (IsTimeProtocolV2(nHeight)) return nTimeSlotLength - 1;
        // PoS (TimeV1): 3 minutes - PoW: 2 hours
        return (nHeight > height_last_PoW ? nFutureTimeDriftPoS : nFutureTimeDriftPoW);
    }

    bool IsValidBlockTimeStamp(const int64_t nTime, const int nHeight) const
    {
        // Before time protocol V2, blocks can have arbitrary timestamps
        if (!IsTimeProtocolV2(nHeight)) return true;
        // Time protocol v2 requires time in slots
        return (nTime % nTimeSlotLength) == 0;
    }

    bool HasStakeMinAgeOrDepth(const int contextHeight, const uint32_t contextTime,
            const int utxoFromBlockHeight, const uint32_t utxoFromBlockTime) const
    {
        // before stake modifier V2, we require the utxo to be nStakeMinAge old
        if (!IsStakeModifierV2(contextHeight))
            return (utxoFromBlockTime + nStakeMinAge <= contextTime);
        // with stake modifier V2+, we require the utxo to be nStakeMinDepth deep in the chain
        return (contextHeight - utxoFromBlockHeight >= nStakeMinDepth);
    }

    int EVMChainID;
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
    /** Block height at which EVM Cancun fork becomes active */
    int nCancunHeight;
    
    /*
     * (Legacy) Zerocoin consensus params
     */
    std::string ZC_Modulus;  // parsed in Zerocoin_Params (either as hex or dec string)
    int ZC_MaxPublicSpendsPerTx;
    int ZC_MaxSpendsPerTx;
    int ZC_MinMintConfirmations;
    CAmount ZC_MinMintFee; // <<-- This will now compile correctly
    int ZC_MinStakeDepth;
    int ZC_TimeStart;

    libzerocoin::ZerocoinParams* Zerocoin_Params(bool useModulusV1) const
    {
        static CBigNum bnTrustedModulus(ZC_Modulus);
        static libzerocoin::ZerocoinParams ZCParams = libzerocoin::ZerocoinParams(bnTrustedModulus);
        return &ZCParams;
    }
};
} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PARAMS_H