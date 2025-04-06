// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2011-2013 The PPCoin developers
// Copyright (c) 2013-2014 The NovaCoin Developers
// Copyright (c) 2014-2018 The BlackCoin Developers
// Copyright (c) 2015-2019 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAIN_H
#define BITCOIN_CHAIN_H

#include "primitives/block.h" // Includes uint256.h, CBlockHeader, CBlock
#include "primitives/transaction.h" // For COutPoint needed in CDiskBlockIndex serialization
#include "chainparams.h"     // For Params()
#include "pow.h"             // May be needed for functions used by CBlockIndex methods
#include "timedata.h"        // May be needed
#include "tinyformat.h"
#include "util.h"            // For strprintf, LogPrintf, DateTimeStrFormat etc.
#include "libzerocoin/Denominations.h" // Keep DigiWage specific
#include "serialize.h"       // For serialization macros
#include "version.h"         // For CLIENT_VERSION

#include <vector>
#include <map>               // For mapZerocoinSupply
#include <string>            // For ToString
#include <stdint.h>          // For int64_t etc.
#include <stdexcept>         // For std::runtime_error
#include <cstring>           // For memcpy
#include <algorithm>         // For std::sort, std::min, std::max

// Forward declare CBlockLocator if GetLocator implementation needs it
class CBlockLocator;

class CBlockFileInfo
{
public:
    unsigned int nBlocks{0};      //!< number of blocks stored in file
    unsigned int nSize{0};        //!< number of used bytes of block file
    unsigned int nUndoSize{0};    //!< number of used bytes in the undo file
    unsigned int nHeightFirst{0}; //!< lowest height of block in file
    unsigned int nHeightLast{0};  //!< highest height of block in file
    uint64_t nTimeFirst{0};       //!< earliest time of block in file
    uint64_t nTimeLast{0};        //!< latest time of block in file

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(VARINT(nBlocks));
        READWRITE(VARINT(nSize));
        READWRITE(VARINT(nUndoSize));
        READWRITE(VARINT(nHeightFirst));
        READWRITE(VARINT(nHeightLast));
        READWRITE(VARINT(nTimeFirst));
        READWRITE(VARINT(nTimeLast));
    }

    void SetNull()
    {
        nBlocks = 0;
        nSize = 0;
        nUndoSize = 0;
        nHeightFirst = 0;
        nHeightLast = 0;
        nTimeFirst = 0;
        nTimeLast = 0;
    }

    CBlockFileInfo()
    {
        SetNull();
    }

    // Declaration only, definition expected in chain.cpp
    std::string ToString() const;

    /** update statistics (does not update nSize) */
    void AddBlock(unsigned int nHeightIn, uint64_t nTimeIn)
    {
        if (nBlocks == 0 || nHeightFirst > nHeightIn)
            nHeightFirst = nHeightIn;
        if (nBlocks == 0 || nTimeFirst > nTimeIn)
            nTimeFirst = nTimeIn;
        nBlocks++;
        if (nHeightIn > nHeightLast)
            nHeightLast = nHeightIn;
        if (nTimeIn > nTimeLast)
            nTimeLast = nTimeIn;
    }
};

struct CDiskBlockPos {
    int nFile;
    unsigned int nPos;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(VARINT(nFile));
        READWRITE(VARINT(nPos));
    }

    CDiskBlockPos()
    {
        SetNull();
    }

    CDiskBlockPos(int nFileIn, unsigned int nPosIn)
    {
        nFile = nFileIn;
        nPos = nPosIn;
    }

    friend bool operator==(const CDiskBlockPos& a, const CDiskBlockPos& b)
    {
        return (a.nFile == b.nFile && a.nPos == b.nPos);
    }

    friend bool operator!=(const CDiskBlockPos& a, const CDiskBlockPos& b)
    {
        return !(a == b);
    }

    void SetNull()
    {
        nFile = -1;
        nPos = 0;
    }
    bool IsNull() const { return (nFile == -1); }
};

// Keep DigiWage BlockStatus enum
enum BlockStatus : unsigned int { // Use unsigned int if that's original, otherwise uint32_t
    //! Unused.
    BLOCK_VALID_UNKNOWN = 0,

    //! Parsed, version ok, hash satisfies claimed PoW, 1 <= vtx count <= max, timestamp not in future
    BLOCK_VALID_HEADER = 1,

    //! All parent headers found, difficulty matches, timestamp >= median previous, checkpoint. Implies all parents
    //! are also at least TREE.
    BLOCK_VALID_TREE = 2,

    /**
     * Only first tx is coinbase, 2 <= coinbase input script length <= 100, transactions valid, no duplicate txids,
     * sigops, size, merkle root. Implies all parents are at least TREE but not necessarily TRANSACTIONS. When all
     * parent blocks also have TRANSACTIONS, CBlockIndex::nChainTx will be set.
     */
    BLOCK_VALID_TRANSACTIONS = 3,

    //! Outputs do not overspend inputs, no double spends, coinbase output ok, immature coinbase spends, BIP30.
    //! Implies all parents are also at least CHAIN.
    BLOCK_VALID_CHAIN = 4,

    //! Scripts & signatures ok. Implies all parents are also at least SCRIPTS.
    BLOCK_VALID_SCRIPTS = 5,

    //! All validity bits.
    BLOCK_VALID_MASK = BLOCK_VALID_HEADER | BLOCK_VALID_TREE | BLOCK_VALID_TRANSACTIONS |
                       BLOCK_VALID_CHAIN |
                       BLOCK_VALID_SCRIPTS,

    BLOCK_HAVE_DATA = 8,  //! full block available in blk*.dat
    BLOCK_HAVE_UNDO = 16, //! undo data available in rev*.dat
    BLOCK_HAVE_MASK = BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO,

    BLOCK_FAILED_VALID = 32, //! stage after last reached validness failed
    BLOCK_FAILED_CHILD = 64, //! descends from failed block
    BLOCK_FAILED_MASK = BLOCK_FAILED_VALID | BLOCK_FAILED_CHILD,
};

// Keep DigiWage BlockIndex flags enum
enum {
    BLOCK_PROOF_OF_STAKE = (1 << 0), // is proof-of-stake block
    BLOCK_STAKE_ENTROPY = (1 << 1),  // entropy bit for stake modifier
    BLOCK_STAKE_MODIFIER = (1 << 2), // regenerated stake modifier
};

/** The block chain is a tree shaped structure starting with the
 * genesis block at the root, with each block potentially having multiple
 * candidates to be the next block. A blockindex may have multiple pprev pointing
 * to it, but at most one of them can be part of the currently active branch.
 */
class CBlockIndex
{
public:
    //! pointer to the hash of the block, if any. memory is owned by mapBlockIndex
    const uint256* phashBlock;

    //! pointer to the index of the predecessor of this block
    CBlockIndex* pprev;

    //! pointer to the index of some further predecessor of this block (Keep if used by DigiWage)
    CBlockIndex* pskip;

    //! height of the entry in the chain. The genesis block has height 0
    int nHeight;

    //! Which # file this block is stored in (blk?????.dat)
    int nFile;

    //! Byte offset within blk?????.dat where this block's data is stored
    unsigned int nDataPos;

    //! Byte offset within rev?????.dat where this block's undo data is stored
    unsigned int nUndoPos;

    //! (memory only) Total amount of work (expected number of hashes) in the chain up to and including this block
    uint256 nChainWork; // Use uint256 as per DigiWage

    //! Number of transactions in this block.
    unsigned int nTx;

    //! (memory only) Number of transactions in the chain up to and including this block.
    unsigned int nChainTx;

    //! Verification status of this block. See enum BlockStatus
    unsigned int nStatus;

    // Keep DigiWage proof-of-stake specific fields
    std::vector<unsigned char> vStakeModifier;
    int64_t nMoneySupply;
    unsigned int nFlags; // Stores BLOCK_PROOF_OF_STAKE etc.

    //! block header fields
    int nVersion;
    uint256 hashMerkleRoot;
    unsigned int nTime;
    unsigned int nBits;
    unsigned int nNonce;
    uint256 nAccumulatorCheckpoint; // Keep DigiWage specific field

    // >>> ADDED for Qtum EVM state <<<
    uint256 hashStateRoot; //! Root hash of the state trie after this block
    uint256 hashUTXORoot;  //! Root hash of the UTXO set after this block
    // >>> END of ADDED members <<<

    //! (memory only) Sequential id assigned to distinguish order in which blocks are received.
    int32_t nSequenceId;

    //! zerocoin specific fields (Keep DigiWage specific)
    std::map<libzerocoin::CoinDenomination, int64_t> mapZerocoinSupply;

    // --- Constructors (Declarations only) ---
    CBlockIndex();
    CBlockIndex(const CBlockHeader& block);
    CBlockIndex(const CBlock& block);

    // --- Methods (Declarations only, definitions in chain.cpp) ---
    void ClearMapZcSupply();
    std::string ToString() const;
    CDiskBlockPos GetBlockPos() const;
    CDiskBlockPos GetUndoPos() const;
    CBlockHeader GetBlockHeader() const;

    uint256 GetBlockHash() const {
        // This simple getter can stay inline in the header
        return (phashBlock ? *phashBlock : uint256());
    }

    int64_t GetBlockTime() const {
        // This simple getter can stay inline in the header
        return (int64_t)nTime;
    }

    int64_t GetMedianTimePast() const;

    // Added declarations for functions defined in chain.cpp
    int64_t MaxFutureBlockTime() const;
    int64_t MinPastBlockTime() const;

    // PoS/PoW checks (simple enough to be inline)
    bool IsProofOfStake() const { return (nFlags & BLOCK_PROOF_OF_STAKE); }
    bool IsProofOfWork() const { return !IsProofOfStake(); }
    void SetProofOfStake() { nFlags |= BLOCK_PROOF_OF_STAKE; } // Simple enough to be inline

    // Stake Modifier functions (declarations only)
    unsigned int GetStakeEntropyBit() const;
    bool SetStakeEntropyBit(unsigned int nEntropyBit);
    bool GeneratedStakeModifier() const; // Simple enough to be inline? Check chain.cpp def. If just return (nFlags &...), keep inline.
    void SetStakeModifier(const uint64_t nStakeModifier, bool fGeneratedStakeModifier);
    void SetStakeModifier(const uint256& nStakeModifier);
    void SetNewStakeModifier(const uint256& prevoutId);
    uint64_t GetStakeModifierV1() const;
    uint256 GetStakeModifierV2() const;

    //! Check whether this block index entry is valid up to the passed validity level.
    bool IsValid(enum BlockStatus nUpTo = BLOCK_VALID_TRANSACTIONS) const;

    //! Raise the validity level of this block index entry.
    bool RaiseValidity(enum BlockStatus nUpTo);

    //! Build the skiplist pointer for this entry. (Keep if DigiWage uses skip pointers)
    void BuildSkip();

    //! Efficiently find an ancestor of this block.
    CBlockIndex* GetAncestor(int height);
    const CBlockIndex* GetAncestor(int height) const;

    // Keep DigiWage Zerocoin functions (declarations only)
    int64_t GetZerocoinSupply() const;
    int64_t GetZcMints(libzerocoin::CoinDenomination denom) const;
    int64_t GetZcMintsAmount(libzerocoin::CoinDenomination denom) const;
};

CBlockIndex* LastCommonAncestor(CBlockIndex* pa, CBlockIndex* pb);


/** Used to marshal pointers into hashes for db storage. */
// Keep DigiWage versioning constant
static const int DBI_OLD_SER_VERSION = 2000000;

// CDiskBlockIndex remains unchanged from the previous "fixed" version,
// as its SerializationOp needs careful review against the original DigiWage file.
// Ensure the logic inside correctly uses 'version_to_use'.
class CDiskBlockIndex : public CBlockIndex
{
public:
    uint256 hashPrev;

    CDiskBlockIndex() : CBlockIndex()
    {
        hashPrev = uint256();
    }

    explicit CDiskBlockIndex(const CBlockIndex* pindex) : CBlockIndex(*pindex)
    {
        hashPrev = (pprev ? pprev->GetBlockHash() : uint256());
    }

    ADD_SERIALIZE_METHODS;
// Add this member variable:
std::vector<unsigned char> vchBlockSig;

// Add this getter method:
std::vector<unsigned char> GetBlockSignature() const {
    return vchBlockSig;
}
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nSerVersion_passed)
    {
        int version_to_use = nSerVersion_passed;

        if (ser_action.ForRead()) {
            int version_read_from_stream = 0;
            READWRITE(VARINT(version_read_from_stream));
            version_to_use = version_read_from_stream;
        } else {
            if (!(nType & SER_GETHASH)) {
                READWRITE(VARINT(version_to_use));
            }
        }

        READWRITE(VARINT(nHeight));
        READWRITE(VARINT(nStatus));
        READWRITE(VARINT(nTx));
        if (nStatus & (BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO))
            READWRITE(VARINT(nFile));
        if (nStatus & BLOCK_HAVE_DATA)
            READWRITE(VARINT(nDataPos));
        if (nStatus & BLOCK_HAVE_UNDO)
            READWRITE(VARINT(nUndoPos));

        if (version_to_use >= DBI_OLD_SER_VERSION) {
            READWRITE(nMoneySupply);
            READWRITE(nFlags);
            READWRITE(this->nVersion);
            READWRITE(vStakeModifier);
            READWRITE(hashPrev);
            READWRITE(hashMerkleRoot);
            READWRITE(nTime);
            READWRITE(nBits);
            READWRITE(nNonce);
            if (this->nVersion > 3) {
                READWRITE(mapZerocoinSupply);
                if (this->nVersion == 4) READWRITE(nAccumulatorCheckpoint);
            }
        } else {
            int64_t nMint = 0;
            uint256 hashNext = uint256();
            READWRITE(nMint);
            READWRITE(nMoneySupply);
            READWRITE(nFlags);
            if (!Params().GetConsensus().IsStakeModifierV2(nHeight)) {
                uint64_t nStakeModifier_v1 = 0;
                READWRITE(nStakeModifier_v1);
                 if (ser_action.ForRead())
                     this->SetStakeModifier(nStakeModifier_v1, this->GeneratedStakeModifier());
            } else {
                uint256 nStakeModifierV2;
                READWRITE(nStakeModifierV2);
                 if (ser_action.ForRead())
                     this->SetStakeModifier(nStakeModifierV2);
            }
            if (IsProofOfStake()) {
                COutPoint prevoutStake;
                unsigned int nStakeTime = 0;
                READWRITE(prevoutStake);
                READWRITE(VARINT(nStakeTime));
            }
            READWRITE(this->nVersion);
            READWRITE(hashPrev);
            READWRITE(hashNext);
            READWRITE(hashMerkleRoot);
            READWRITE(nTime);
            READWRITE(nBits);
            READWRITE(nNonce);
            if(this->nVersion > 3) {
                std::vector<libzerocoin::CoinDenomination> vMintDenominationsInBlock;
                READWRITE(nAccumulatorCheckpoint);
                READWRITE(mapZerocoinSupply);
                READWRITE(vMintDenominationsInBlock);
            }
        }

        // Add EVM fields serialization - place within versioning if needed
        // if (version_to_use >= EVM_FEATURE_VERSION) {
             READWRITE(hashStateRoot);
             READWRITE(hashUTXORoot);
        // } else if (ser_action.ForRead()) {
        //     hashStateRoot.SetNull();
        //     hashUTXORoot.SetNull();
        // }
    }


    uint256 GetBlockHash() const
    {
        CBlockHeader block;
        block.nVersion        = nVersion;
        block.hashPrevBlock   = hashPrev;
        block.hashMerkleRoot  = hashMerkleRoot;
        block.nTime           = nTime;
        block.nBits           = nBits;
        block.nNonce          = nNonce;
        block.nAccumulatorCheckpoint = nAccumulatorCheckpoint;
        return block.GetHash();
    }


    std::string ToString() const
    {
        std::string str = "CDiskBlockIndex(";
        str += CBlockIndex::ToString(); // Call base class ToString()
        str += strprintf("\n                hashBlock=%s, hashPrev=%s)",
            GetBlockHash().ToString(),
            hashPrev.ToString());
        return str;
    }
};

/** An in-memory indexed chain of blocks. */
class CChain
{
private:
    std::vector<CBlockIndex*> vChain;

public:
    /** Returns the index entry for the genesis block of this chain, or NULL if none. */
    CBlockIndex* Genesis() const
    {
        return vChain.size() > 0 ? vChain[0] : nullptr;
    }

    /** Returns the index entry for the tip of this chain, or NULL if none. */
    CBlockIndex* Tip(/*bool fProofOfStake = false*/) const
    {
        return vChain.empty() ? nullptr : vChain.back();
    }

    /** Returns the index entry at a particular height in this chain, or NULL if no such height exists. */
    CBlockIndex* operator[](int nHeight) const
    {
        if (nHeight < 0 || nHeight >= (int)vChain.size())
            return nullptr;
        return vChain[nHeight];
    }

    /** Compare two chains efficiently. */
    friend bool operator==(const CChain& a, const CChain& b)
    {
        return a.Height() == b.Height() && a.Tip() == b.Tip();
    }

    /** Efficiently check whether a block is present in this chain. */
    bool Contains(const CBlockIndex* pindex) const
    {
        if (pindex == nullptr || pindex->nHeight < 0 || pindex->nHeight >= (int)vChain.size())
            return false;
        return (*this)[pindex->nHeight] == pindex;
    }

    /** Find the successor of a block in this chain, or NULL if the given index is not found or is the tip. */
    CBlockIndex* Next(const CBlockIndex* pindex) const
    {
        if (!Contains(pindex))
             return nullptr;
        if (pindex->nHeight == Height()) // If pindex is the tip
            return nullptr;
        return (*this)[pindex->nHeight + 1];
    }

    /** Return the maximal height in the chain. Is equal to chain.Tip() ? chain.Tip()->nHeight : -1. */
    int Height() const
    {
        return vChain.size() - 1;
    }

    /** Set/initialize a chain with a given tip. */
    void SetTip(CBlockIndex* pindex); // Declaration only

    /** Return a CBlockLocator that refers to a block in this chain (by default the tip). */
    CBlockLocator GetLocator(const CBlockIndex* pindex = nullptr) const; // Declaration only

    /** Find the last common block between this chain and a block index entry. */
    const CBlockIndex* FindFork(const CBlockIndex* pindex) const; // Declaration only

    // Keep DigiWage NewSigsActive() if implementation exists and is needed
    // bool NewSigsActive() { return Params().GetConsensus().IsMessSigV2(Height()); }
};


#endif // BITCOIN_CHAIN_H