// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2016-2019 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"
#include "chainparams.h" // Needed for Params()
#include "util.h"        // Needed for LogPrintf, GetBoolArg, GetAdjustedTime etc.
#include "main.h"  // Needed for BlockStatus? (Maybe included via chain.h already) - Verify if needed
#include "hash.h"        // Needed for CHashWriter
#include "primitives/block.h" // Needed for CBlock, CBlockHeader
#include "primitives/transaction.h" // Needed for COutPoint
#include "libzerocoin/Denominations.h" // Needed for zerocoinDenomList

#include <algorithm>     // Needed for std::sort, std::max
#include <vector>        // Needed for std::vector used in GetLocator
#include <stdexcept>     // Needed for std::runtime_error
#include <map>           // Needed for std::map

// Add this function definition to chain.cpp
CBlockIndex* LastCommonAncestor(CBlockIndex* pa, CBlockIndex* pb) {
    if (pa == pb)
        return pa;

    CBlockIndex *pindex1 = pa;
    CBlockIndex *pindex2 = pb;

    // Move pa or pb up the chain until they are at the same height
    if (pindex1->nHeight > pindex2->nHeight)
        pindex1 = pindex1->GetAncestor(pindex2->nHeight);
    else if (pindex2->nHeight > pindex1->nHeight)
        pindex2 = pindex2->GetAncestor(pindex1->nHeight);

    // Now move both up simultaneously until they meet
    while (pindex1 != pindex2 && pindex1 && pindex2)
    {
        pindex1 = pindex1->pprev;
        pindex2 = pindex2->pprev;
    }

    // Return the common ancestor (or nullptr if none existed, e.g., different genesis)
    assert(pindex1 == pindex2); // Should meet unless chains are fundamentally unrelated
    return pindex1;
}


/**
 * CChain implementation
 */
void CChain::SetTip(CBlockIndex* pindex)
{
    if (pindex == nullptr) { // Use nullptr
        vChain.clear();
        return;
    }
    vChain.resize(pindex->nHeight + 1);
    while (pindex && vChain[pindex->nHeight] != pindex) {
        vChain[pindex->nHeight] = pindex;
        pindex = pindex->pprev;
    }
}

CBlockLocator CChain::GetLocator(const CBlockIndex* pindex) const
{
    int nStep = 1;
    std::vector<uint256> vHave;
    vHave.reserve(32);

    if (!pindex)
        pindex = Tip();
    while (pindex) {
        vHave.push_back(pindex->GetBlockHash());
        // Stop when we have added the genesis block.
        if (pindex->nHeight == 0)
            break;
        // Exponentially larger steps back, plus the genesis block.
        int nHeight = std::max(pindex->nHeight - nStep, 0);
        // Use Contains checks before accessing vector or GetAncestor
        if (Contains(pindex)) {
             // Safety check: ensure nHeight is within bounds
             if (nHeight >= 0 && nHeight < (int)vChain.size()) {
                 pindex = vChain[nHeight];
             } else {
                 // Should not happen if Contains() is true, but as a fallback:
                 pindex = pindex->GetAncestor(nHeight);
             }
        } else {
            // Otherwise, use O(log n) skiplist if pskip is implemented, or loop pprev
            pindex = pindex->GetAncestor(nHeight);
        }
        if (vHave.size() > 10)
            nStep *= 2;
    }
    // CBlockLocator constructor takes std::vector<uint256>
    return CBlockLocator(vHave);
}

const CBlockIndex* CChain::FindFork(const CBlockIndex* pindex) const
{
    if (pindex == nullptr)
        return nullptr;
    // If the input index is taller than our chain, walk it down first
    if (pindex->nHeight > Height())
        pindex = pindex->GetAncestor(Height());
    // Now walk backwards on both chains simultaneously until we find a common ancestor
    while (pindex && !Contains(pindex))
        pindex = pindex->pprev;
    return pindex; // Return the common ancestor (or nullptr if none found, e.g., different genesis)
}


// --- Helper function for Skip List ---
// Determine the skip height for BuildSkip()
// Based on common Bitcoin Core implementation
int GetSkipHeight(int height) {
    if (height < 2)
        return 0;

    // Determine the largest power of 2 less than or equal to height // 2
    int msb_pos = 0;
    int h = height >> 1; // Divide by 2
    while (h > 0) {
        h >>= 1;
        msb_pos++;
    }
    return 1 << msb_pos;
}


// --- CBlockIndex Method Implementations ---

// Default constructor definition (can be empty if member initializers cover everything)
CBlockIndex::CBlockIndex() :
    phashBlock(nullptr), pprev(nullptr), pskip(nullptr), nHeight(0), nFile(0), nDataPos(0), nUndoPos(0),
    nChainWork(), nTx(0), nChainTx(0), nStatus(0), vStakeModifier(), nMoneySupply(0), nFlags(0),
    nVersion(0), hashMerkleRoot(), nTime(0), nBits(0), nNonce(0), nAccumulatorCheckpoint(),
    hashStateRoot(), hashUTXORoot(), nSequenceId(0)
{
    ClearMapZcSupply();
}

// Constructor from CBlockHeader definition
CBlockIndex::CBlockIndex(const CBlockHeader& block) :
    phashBlock(nullptr), pprev(nullptr), pskip(nullptr), nHeight(0), nFile(0), nDataPos(0), nUndoPos(0),
    nChainWork(), nTx(0), nChainTx(0), nStatus(0), vStakeModifier(), nMoneySupply(0), nFlags(0),
    nVersion(block.nVersion), hashMerkleRoot(block.hashMerkleRoot), nTime(block.nTime),
    nBits(block.nBits), nNonce(block.nNonce), nAccumulatorCheckpoint(block.nAccumulatorCheckpoint),
    hashStateRoot(), hashUTXORoot(), nSequenceId(0)
{
     ClearMapZcSupply();
     // PoS flag needs to be set based on block content later if needed
}

// Constructor from CBlock definition (Corrected syntax)
CBlockIndex::CBlockIndex(const CBlock& block) : // Colon starts initializer list
    // Initialize ALL members using the initializer list
    phashBlock(nullptr), // Will be set later when added to mapBlockIndex
    pprev(nullptr),      // Will be set later
    pskip(nullptr),      // Will be set later
    nHeight(0),          // Will be set later
    nFile(0),            // Will be set later by AcceptBlock
    nDataPos(0),         // Will be set later by AcceptBlock
    nUndoPos(0),         // Will be set later by AcceptBlock
    nChainWork(),        // Will be calculated later
    nTx(block.vtx.size()), // Set from block
    nChainTx(0),         // Will be calculated later
    nStatus(0),          // Will be set later
    vStakeModifier(),    // Default init, set later if PoS
    nMoneySupply(0),     // Will be calculated later
    nFlags(0),           // Will be set later
    nVersion(block.nVersion), // Set from block header
    hashMerkleRoot(block.hashMerkleRoot), // Set from block header
    nTime(block.nTime), // Set from block header
    nBits(block.nBits), // Set from block header
    nNonce(block.nNonce), // Set from block header
    nAccumulatorCheckpoint(block.nAccumulatorCheckpoint), // Set from block header
    hashStateRoot(),     // Default init, set later by EVM logic
    hashUTXORoot(),      // Default init, set later by EVM logic
    nSequenceId(0)       // Will be set later
    // mapZerocoinSupply default initialized
{ // Body of the constructor starts HERE
    ClearMapZcSupply(); // Initialize map
    // Set PoS flag if applicable - Requires block.IsProofOfStake() method to exist
    if (block.IsProofOfStake()) {
        SetProofOfStake();
    }
    // Note: vStakeModifier is set later during ConnectBlock/ContextualCheckBlock
}


void CBlockIndex::ClearMapZcSupply()
{
    mapZerocoinSupply.clear();
    // Start supply of each denomination with 0s
    for (const auto& denom : libzerocoin::zerocoinDenomList)
        mapZerocoinSupply.insert(std::make_pair(denom, 0));
}

std::string CBlockIndex::ToString() const
{
    const std::string blockHashStr = (phashBlock ? phashBlock->ToString().substr(0,10) : "null");
    return strprintf("CBlockIndex(pprev=%p, nHeight=%d, merkle=%s, hashBlock=%s)",
        pprev, nHeight,
        hashMerkleRoot.ToString().substr(0,10).c_str(),
        blockHashStr.c_str());
}

CDiskBlockPos CBlockIndex::GetBlockPos() const
{
    CDiskBlockPos ret;
    if (nStatus & BLOCK_HAVE_DATA) {
        ret.nFile = nFile;
        ret.nPos = nDataPos;
    }
    return ret;
}

CDiskBlockPos CBlockIndex::GetUndoPos() const
{
    CDiskBlockPos ret;
    if (nStatus & BLOCK_HAVE_UNDO) {
        ret.nFile = nFile;
        ret.nPos = nUndoPos;
    }
    return ret;
}

CBlockHeader CBlockIndex::GetBlockHeader() const
{
    CBlockHeader block;
    block.nVersion = nVersion;
    if (pprev) block.hashPrevBlock = pprev->GetBlockHash();
    block.hashMerkleRoot = hashMerkleRoot;
    block.nTime = nTime;
    block.nBits = nBits;
    block.nNonce = nNonce;
    block.nAccumulatorCheckpoint = nAccumulatorCheckpoint;
    // EVM roots are not part of the consensus header
    return block;
}

// Definition for MaxFutureBlockTime
int64_t CBlockIndex::MaxFutureBlockTime() const
{
    // Use adjusted time from util.h and the consensus parameter
    // The drift value depends on the *next* block's height
    return GetAdjustedTime() + Params().GetConsensus().FutureBlockTimeDrift(nHeight + 1);
}

// Definition for MinPastBlockTime
int64_t CBlockIndex::MinPastBlockTime() const
{
    const Consensus::Params& consensus = Params().GetConsensus();
    // Check if Time Protocol V2 is active for the *next* block's height
    if (!consensus.IsTimeProtocolV2(nHeight + 1)) {
        // Time Protocol v1: Use MedianTimePast of the *current* block
        return GetMedianTimePast();
    } else {
        // Time Protocol v2 logic from original DigiWage code
        if (nHeight + 1 == consensus.height_start_TimeProtoV2)
            // Special handling for the transition block
            return GetBlockTime() - consensus.FutureBlockTimeDrift(nHeight) + consensus.FutureBlockTimeDrift(nHeight + 1);
        else
            // Time Protocol v2: Use previous block's time
            // Need pprev check here
            return (pprev ? pprev->GetBlockTime() : GetBlockTime()); // Fallback to current time if no prev (genesis)? Check original logic.
            // Bitcoin Core uses: return pprev->GetBlockTime();
    }
}

int64_t CBlockIndex::GetMedianTimePast() const
{
    const int nMedianTimeSpan = 11; // Define locally or use a constant
    int64_t pmedian[nMedianTimeSpan];
    int64_t* pbegin = &pmedian[nMedianTimeSpan];
    int64_t* pend = &pmedian[nMedianTimeSpan];

    const CBlockIndex* pindex = this;
    for (int i = 0; i < nMedianTimeSpan && pindex; i++, pindex = pindex->pprev)
        *(--pbegin) = pindex->GetBlockTime();

    std::sort(pbegin, pend);
    return pbegin[(pend - pbegin) / 2];
}

unsigned int CBlockIndex::GetStakeEntropyBit() const
{
    // Make sure phashBlock is not null
    if (!phashBlock) return 0; // Or handle error appropriately
    unsigned int nEntropyBit = ((phashBlock->GetCheapHash()) & 1); // Assumes GetCheapHash exists on uint256
    // Original DigiWage code had GetBoolArg, requires #include "util.h"
    if (GetBoolArg("-printstakemodifier", false))
        LogPrintf("GetStakeEntropyBit: nHeight=%u hashBlock=%s nEntropyBit=%u\n", nHeight, phashBlock->ToString().c_str(), nEntropyBit);

    return nEntropyBit;
}

bool CBlockIndex::SetStakeEntropyBit(unsigned int nEntropyBit)
{
    if (nEntropyBit > 1)
        return false;
    // Ensure flags are cleared first if needed, otherwise just OR
    nFlags &= ~BLOCK_STAKE_ENTROPY; // Clear the bit first? Check original logic
    nFlags |= (nEntropyBit ? BLOCK_STAKE_ENTROPY : 0);
    return true;
}

// Definition for GeneratedStakeModifier
bool CBlockIndex::GeneratedStakeModifier() const
{
    return (nFlags & BLOCK_STAKE_MODIFIER);
}

// Sets V1 stake modifier
void CBlockIndex::SetStakeModifier(const uint64_t nStakeModifier, bool fGeneratedStakeModifier)
{
    vStakeModifier.resize(sizeof(nStakeModifier));
    memcpy(vStakeModifier.data(), &nStakeModifier, sizeof(nStakeModifier));
    if (fGeneratedStakeModifier)
        nFlags |= BLOCK_STAKE_MODIFIER;
    else
        nFlags &= ~BLOCK_STAKE_MODIFIER; // Clear flag if not generated
}

// Sets V2 stake modifier
void CBlockIndex::SetStakeModifier(const uint256& nStakeModifier)
{
    vStakeModifier.assign(nStakeModifier.begin(), nStakeModifier.end());
    // Assuming setting V2 always implies it was generated? Check logic.
    nFlags |= BLOCK_STAKE_MODIFIER;
}

// Generates and sets new V2 stake modifier
void CBlockIndex::SetNewStakeModifier(const uint256& prevoutId)
{
    if (nHeight < Params().GetConsensus().height_start_StakeModifierV2) return; // Should not be called before V2 activation
    if (!pprev) throw std::runtime_error(strprintf("%s : ERROR: null pprev", __func__));

    CHashWriter ss(SER_GETHASH, 0); // Requires #include "hash.h"
    ss << prevoutId;
    // Get V2 modifier from previous block correctly
    ss << pprev->GetStakeModifierV2();
    SetStakeModifier(ss.GetHash()); // Calls the V2 setter
}

// Returns V1 stake modifier (uint64_t)
uint64_t CBlockIndex::GetStakeModifierV1() const
{
    if (vStakeModifier.size() != sizeof(uint64_t) || Params().GetConsensus().IsStakeModifierV2(nHeight))
        return 0;
    uint64_t nStakeModifier = 0; // Initialize
    memcpy(&nStakeModifier, vStakeModifier.data(), sizeof(nStakeModifier));
    return nStakeModifier;
}

// Returns V2 stake modifier (uint256)
uint256 CBlockIndex::GetStakeModifierV2() const
{
    if (vStakeModifier.size() != sizeof(uint256) || !Params().GetConsensus().IsStakeModifierV2(nHeight))
        return uint256(); // Return zero hash
    uint256 nStakeModifier;
    memcpy(nStakeModifier.begin(), vStakeModifier.data(), sizeof(nStakeModifier));
    return nStakeModifier;
}

//! Check whether this block index entry is valid up to the passed validity level.
bool CBlockIndex::IsValid(enum BlockStatus nUpTo) const
{
    assert(!(nUpTo & BLOCK_HAVE_MASK)); // Only validity flags allowed.
    assert(nUpTo != BLOCK_VALID_UNKNOWN);
    if (nStatus & BLOCK_FAILED_MASK)
        return false;
    return ((nStatus & BLOCK_VALID_MASK) >= nUpTo);
}

//! Raise the validity level of this block index entry.
bool CBlockIndex::RaiseValidity(enum BlockStatus nUpTo)
{
    assert(!(nUpTo & BLOCK_HAVE_MASK)); // Only validity flags allowed.
    assert(nUpTo != BLOCK_VALID_UNKNOWN);
    if (nStatus & BLOCK_FAILED_MASK)
        return false;
    unsigned int nOldStatus = nStatus;
    // Logic from original code
    if ((nStatus & BLOCK_VALID_MASK) < nUpTo) {
        nStatus = (nStatus & ~BLOCK_VALID_MASK) | nUpTo;
        return true; // Return true if status changed
    }
    return false; // Return false if status didn't change
}

// Build the skiplist pointer for this entry.
void CBlockIndex::BuildSkip()
{
    if (pprev)
        pskip = pprev->GetAncestor(nHeight - GetSkipHeight(nHeight));
}

CBlockIndex* CBlockIndex::GetAncestor(int height)
{
    if (height > nHeight || height < 0)
        return nullptr;

    CBlockIndex* pindexWalk = this;
    int heightWalk = nHeight;
    // Simple loop version (use if skip list logic isn't fully verified/needed)
    while (heightWalk > height) {
        pindexWalk = pindexWalk->pprev;
        if (!pindexWalk) return nullptr; // Should not happen if height < nHeight
        heightWalk--;
    }
    return pindexWalk;
}

const CBlockIndex* CBlockIndex::GetAncestor(int height) const
{
    // Const version calls non-const version and casts result
    return const_cast<CBlockIndex*>(this)->GetAncestor(height);
}


int64_t CBlockIndex::GetZerocoinSupply() const
{
    int64_t nTotal = 0;
    for (const auto& pair : mapZerocoinSupply) {
        nTotal += libzerocoin::ZerocoinDenominationToAmount(pair.first) * pair.second;
    }
    return nTotal;
}

int64_t CBlockIndex::GetZcMints(libzerocoin::CoinDenomination denom) const
{
    try {
         return mapZerocoinSupply.at(denom);
    } catch (const std::out_of_range& oor) {
         LogPrintf("%s: Error: Denomination %d not found in mapZerocoinSupply for block %d\n",
                   __func__, libzerocoin::ZerocoinDenominationToInt(denom), nHeight);
         return 0;
    }
}

int64_t CBlockIndex::GetZcMintsAmount(libzerocoin::CoinDenomination denom) const
{
    return libzerocoin::ZerocoinDenominationToAmount(denom) * GetZcMints(denom);
}