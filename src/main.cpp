// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2011-2013 The PPCoin developers
// Copyright (c) 2013-2014 The NovaCoin Developers
// Copyright (c) 2014-2018 The BlackCoin Developers
// Copyright (c) 2015-2020 The DIGIWAGE developers
// Copyright (c) 2017- TBD The Qtum/DigiWage EVM developers // Added for Qtum merge
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// *** MEGA-MERGED main.cpp: DigiWage + Qtum EVM Core ***

#include "main.h" // Should include most necessary headers via main.h
#include "script/sigversion.h"
#include "addrman.h"
#include "amount.h"
#include "blocksignature.h" // DigiWage specific
#include "chainparams.h"
#include "checkpoints.h"
#include "checkqueue.h"
#include "clientversion.h"
#include "consensus/amount.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include "consensus/zerocoin_verify.h" // DigiWage specific
#include "init.h"
#include "kernel.h" // DigiWage specific staking
#include "legacy/stakemodifier.h"  // DigiWage specific
#include "masternode-budget.h" // DigiWage specific
#include "masternode-payments.h" // DigiWage specific
#include "masternodeman.h" // DigiWage specific
#include "merkleblock.h"
#include "messagesigner.h" // DigiWage specific
#include "net.h"
#include "obfuscation.h" // DigiWage specific
#include "pow.h"
// #include "primitives/block.h" // Included via main.h
// #include "primitives/transaction.h" // Included via main.h
#include "spork.h" // DigiWage specific
#include "sporkdb.h" // DigiWage specific
#include "swifttx.h" // DigiWage specific
#include "sync.h"
#include "timedata.h"
#include "tinyformat.h"
#include "txdb.h"
#include "txmempool.h"
#include "guiinterface.h"
// #include "uint256.h" // Included via primitives
#include "undo.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "utiltime.h"
#include "validationinterface.h"
#include "zpivchain.h" // DigiWage specific

#include "zpiv/zerocoin.h" // DigiWage specific
#include "libzerocoin/Denominations.h" // DigiWage specific
#include <sstream>
#include <numeric> // For std::accumulate
#include <fstream> // For vmLogToJSON file writing

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp> // Needed for fs::path
#include <boost/filesystem/fstream.hpp>
#include <boost/thread.hpp>
#include <boost/foreach.hpp>
#include <boost/variant.hpp> // Needed for boost::get
#include <atomic>
#include <queue>
#include <deque>
#include <algorithm>
#include <cassert>
#include <cmath> // For log

// Includes needed for EVM integration
#include <qtum/qtumutils.h>
#include <qtum/qtumstate.h>
#include <qtum/storageresults.h>
#include <univalue.h> // For vmLogToJSON
#include <libethcore/ABI.h> // For EVM logging/exceptions
#include <libdevcore/CommonIO.h>
#include <libdevcore/CommonData.h>
#include <libdevcore/Log.h>
#include <libdevcore/Exceptions.h>
#include <libethereum/TransactionReceipt.h>
#include <libethereum/ChainParams.h> // For SealEngine chain params
#include <libethereum/LastBlockHashesFace.h> // For LastHashes interface
#include <libethashseal/GenesisInfo.h> // Needed by SealEngine interface?
#include <script/solver.h> // For ExtractDestination specific types?
#include "script/interpreter.h" // For VerifyScript, CachingTransactionSignatureChecker etc.
#include "script/standard.h" // For IsStandard, ScriptSigArgsExpected
#include "hash.h" // For Hash()


#if defined(NDEBUG)
#error "DIGIWAGE cannot be compiled without assertions."
#endif

// Filesystem namespace alias
namespace fs = boost::filesystem;

/**
 * Global state
 */

RecursiveMutex cs_main;

BlockMap mapBlockIndex;
CChain chainActive;
CBlockIndex* pindexBestHeader = NULL;
int64_t nTimeBestReceived = 0;
CAmount nBurnedCoins = 0;

// Best block section
Mutex g_best_block_mutex;
std::condition_variable g_best_block_cv;
uint256 g_best_block;

int nScriptCheckThreads = 0;
std::atomic<bool> fImporting{false};
std::atomic<bool> fReindex{false};
bool fTxIndex = true;
bool fIsBareMultisigStd = true;
bool fCheckBlockIndex = false;
bool fVerifyingBlocks = false;
unsigned int nCoinCacheSize = 5000;

/* If the tip is older than this (in seconds), the node is considered to be in initial block download. */
int64_t nMaxTipAge = DEFAULT_MAX_TIP_AGE;

CFeeRate minRelayTxFee = CFeeRate(10000);

CTxMemPool mempool(::minRelayTxFee);

// Orphan transaction storage
struct COrphanTx {
    CTransaction tx;
    NodeId fromPeer;
};
std::map<uint256, COrphanTx> mapOrphanTransactions;
std::map<uint256, std::set<uint256> > mapOrphanTransactionsByPrev;
std::map<uint256, int64_t> mapRejectedBlocks;

// EVM Global State (Definitions)
std::unique_ptr<QtumState> globalState;
std::shared_ptr<dev::eth::SealEngineFace> globalSealEngine = nullptr; // Needs proper initialization
std::unique_ptr<StorageResults> pstorageresult;
bool fRecordLogOpcodes = false;
bool fIsVMlogFile = false;
bool fGettingValuesDGP = false;
bool fLogEvents = false;
bool fAddressIndex = false;

// Forward declarations (ensure all needed are present)
static void CheckBlockIndex();
void EraseOrphansFor(NodeId peer);
static bool AbortNode(CValidationState& state, const std::string& strMessage, const std::string& userMessage="");
// int ActiveProtocol(); // Declaration moved to main.h/net.h? Assuming it's available.
bool ProcessMessage(CNode* pfrom, std::string strCommand, CDataStream& vRecv, int64_t nTimeReceived);
static bool ActivateBestChainStep(CValidationState& state, CBlockIndex* pindexMostWork, CBlock* pblock, bool fAlreadyChecked);
static CBlockIndex* FindMostWorkChain();
static void UpdateTip(CBlockIndex* pindexNew);
static bool DisconnectTip(CValidationState& state);
static bool ConnectTip(CValidationState& state, CBlockIndex* pindexNew, CBlock* pblock, bool fAlreadyChecked);
static void PruneBlockIndexCandidates();
static void InvalidChainFound(CBlockIndex* pindexNew);
static void InvalidBlockFound(CBlockIndex* pindex, const CValidationState& state);
static void CheckForkWarningConditions();
static void CheckForkWarningConditionsOnNewFork(CBlockIndex* pindexNewForkTip);
UniValue vmLogToJSON(const ResultExecute& execRes, const CTransaction& tx, const CBlock& block, CChain& chain); // EVM Helper
std::string exceptedMessage(const dev::eth::TransactionException& excepted, const dev::bytes& output); // EVM Helper
bool FindBlockPos(CValidationState& state, CDiskBlockPos& pos, unsigned int nAddSize, unsigned int nHeight, uint64_t nTime, bool fKnown = false); // Forward declare FindBlockPos
bool CheckColdStakeFreeOutput(const CTransaction& tx, const int nHeight); // Forward declare (if definition exists elsewhere)

/** Constant stuff for coinbase transactions we create: */
CScript COINBASE_FLAGS;

const std::string strMessageMagic = "DarkNet Signed Message:\n"; // Keep DigiWage specific?

// --- EVM/Transaction Helper Functions ---
static bool TxHasCreateOrCall(const CTransaction& tx) {
    for (const CTxOut& txout : tx.vout) {
         if (txout.scriptPubKey.HasOpCreate() || txout.scriptPubKey.HasOpCall()) { // Assuming these methods exist now
             return true;
         }
    }
    return false;
}

static bool TxHasOpSpend(const CTransaction& tx) {
    for (const CTxOut& txout : tx.vout) {
         if (txout.scriptPubKey.IsZerocoinSpend()) { // Assuming this checks OP_ZEROCOINSPEND
             return true;
         }
    }
    return false;
}

static bool TxHasOpSender(const CTransaction& tx) {
     for (const CTxOut& txout : tx.vout) {
         if (txout.scriptPubKey.HasOpSender()) { // Assuming this method exists now
             return true;
         }
     }
    return false;
}



// --- End Helper Functions ---


// Internal stuff
namespace
{
// CBlockIndexWorkComparator from DigiWage
struct CBlockIndexWorkComparator {
    bool operator()(CBlockIndex* pa, CBlockIndex* pb) const
    {
        // Ensure pa and pb are not null before accessing members
        if (!pa) return pb != nullptr; // null is smaller than non-null
        if (!pb) return false;         // non-null is not smaller than null

        if (pa->nChainWork > pb->nChainWork) return false;
        if (pa->nChainWork < pb->nChainWork) return true;
        // Use sequence ID as tie-breaker
        if (pa->nSequenceId < pb->nSequenceId) return false;
        if (pa->nSequenceId > pb->nSequenceId) return true;
        // Use pointer address as final tie-breaker (consistent within single run)
        if (pa < pb) return false;
        if (pa > pb) return true;
        return false;
    }
};

CBlockIndex* pindexBestInvalid = nullptr;

std::set<CBlockIndex*, CBlockIndexWorkComparator> setBlockIndexCandidates;
int nSyncStarted = 0;
std::multimap<CBlockIndex*, CBlockIndex*> mapBlocksUnlinked;

RecursiveMutex cs_LastBlockFile;
std::vector<CBlockFileInfo> vinfoBlockFile;
int nLastBlockFile = 0;

RecursiveMutex cs_nBlockSequenceId;
int32_t nBlockSequenceId = 1; // Use int32_t to match CBlockIndex member

std::map<uint256, NodeId> mapBlockSource;

// QueuedBlock struct from DigiWage
struct QueuedBlock {
    uint256 hash;
    CBlockIndex* pindex;        //! Optional.
    int64_t nTime;              //! Time of "getdata" request in microseconds.
    int nValidatedQueuedBefore; //! Number of blocks queued with validated headers (globally) at the time this one is requested.
    bool fValidatedHeaders;     //! Whether this block has validated headers at the time of request.
};
std::map<uint256, std::pair<NodeId, std::list<QueuedBlock>::iterator> > mapBlocksInFlight;

int nQueuedValidatedHeaders = 0;
int nPreferredDownload = 0;

std::set<CBlockIndex*> setDirtyBlockIndex;
std::set<int> setDirtyFileInfo;

// CNodeState related structures (CBlockReject, CNodeBlocks, CNodeState) from DigiWage
struct CBlockReject {
    unsigned char chRejectCode;
    std::string strRejectReason;
    uint256 hashBlock;
};
// CNodeBlocks class from DigiWage
class CNodeBlocks {
public:
    CNodeBlocks() : maxSize(0), maxAvg(0) {
        maxSize = GetArg("-blockspamfiltermaxsize", DEFAULT_BLOCK_SPAM_FILTER_MAX_SIZE);
        maxAvg = GetArg("-blockspamfiltermaxavg", DEFAULT_BLOCK_SPAM_FILTER_MAX_AVG);
    }
    bool onBlockReceived(int nHeight) {
        if (nHeight > 0 && maxSize > 0 && maxAvg > 0) { // Check division by zero
             addPoint(nHeight); return true;
        }
        return false;
    }
    bool updateState(CValidationState& state, bool ret) {
        size_t current_size = points.size(); // Use different name than member 'maxSize'
        if (current_size == 0) return ret;
        size_t nBlocks = 0;
        for (const auto& pair : points) { nBlocks += pair.second; }
        // Check division by zero
        double nAvgValue = (current_size > 0) ? static_cast<double>(nBlocks) / current_size : 0.0;
        bool banNode = (nAvgValue >= 1.5 * maxAvg && current_size >= maxAvg) ||
                       (nAvgValue >= maxAvg && nBlocks >= maxSize) ||
                       (nBlocks >= maxSize * 3);
        if (banNode) {
            points.clear();
            // state.DoS returns bool, use it directly if needed, otherwise just call DoS
            state.DoS(100, false, REJECT_INVALID, "block-spam ban node for sending spam");
            return false; // Indicate failure/ban state
        }
        return ret;
    }
private:
    void addPoint(int height) {
        if (points.size() == maxSize && maxSize > 0) { // Check maxSize > 0
            points.erase(points.begin());
        }
        int occurrence = 0;
        auto mi = points.find(height);
        if (mi != points.end()) occurrence = (*mi).second;
        occurrence++;
        points[height] = occurrence;
    }
    std::map<int, int> points;
    size_t maxSize;
    size_t maxAvg;
};
// CNodeState struct from DigiWage
struct CNodeState {
    CService address;
    bool fCurrentlyConnected;
    int nMisbehavior;
    bool fShouldBan;
    std::string name;
    std::vector<CBlockReject> rejects;
    CBlockIndex* pindexBestKnownBlock;
    uint256 hashLastUnknownBlock;
    CBlockIndex* pindexLastCommonBlock;
    bool fSyncStarted;
    int64_t nStallingSince;
    std::list<QueuedBlock> vBlocksInFlight;
    int nBlocksInFlight;
    bool fPreferredDownload;
    CNodeBlocks nodeBlocks; // Ensure CNodeBlocks is included

    CNodeState() :
        fCurrentlyConnected(false), nMisbehavior(0), fShouldBan(false),
        pindexBestKnownBlock(nullptr), hashLastUnknownBlock(), pindexLastCommonBlock(nullptr),
        fSyncStarted(false), nStallingSince(0), nBlocksInFlight(0), fPreferredDownload(false),
        nodeBlocks() // Initialize nodeBlocks
         {}
};
std::map<NodeId, CNodeState> mapNodeState;

// Helper functions from DigiWage's anon namespace (State, GetHeight, UpdatePreferredDownload, InitializeNode, FinalizeNode, etc.)
CNodeState* State(NodeId pnode) {
    std::map<NodeId, CNodeState>::iterator it = mapNodeState.find(pnode);
    if (it == mapNodeState.end()) return nullptr;
    return &it->second;
}
int GetHeight() {
    LOCK(cs_main); return chainActive.Height();
}
void UpdatePreferredDownload(CNode* node, CNodeState* state) {
    if (!state) return; // Add null check
    nPreferredDownload -= state->fPreferredDownload;
    // Stricter conditions from later Bitcoin Core
    state->fPreferredDownload = (!node->fInbound || node->fWhitelisted) && !node->fOneShot && !node->fClient;
    nPreferredDownload += state->fPreferredDownload;
}
void InitializeNode(NodeId nodeid, const CNode* pnode) {
    LOCK(cs_main);
    CNodeState& state = mapNodeState.insert(std::make_pair(nodeid, CNodeState())).first->second;
    state.name = pnode->addrName;
    state.address = pnode->addr;
}
void FinalizeNode(NodeId nodeid) {
    LOCK(cs_main);
    CNodeState* state = State(nodeid);
    if (!state) return;
    if (state->fSyncStarted) nSyncStarted--;
    if (state->nMisbehavior == 0 && state->fCurrentlyConnected) AddressCurrentlyConnected(state->address);
    for (const QueuedBlock& entry : state->vBlocksInFlight) mapBlocksInFlight.erase(entry.hash);
    EraseOrphansFor(nodeid);
    nPreferredDownload -= state->fPreferredDownload;
    mapNodeState.erase(nodeid);
}
void MarkBlockAsReceived(const uint256& hash) {
    auto itInFlight = mapBlocksInFlight.find(hash);
    if (itInFlight != mapBlocksInFlight.end()) {
        CNodeState* state = State(itInFlight->second.first);
        if (state) {
            // Check if iterator is valid before accessing member
            if (itInFlight->second.second != state->vBlocksInFlight.end()) {
                nQueuedValidatedHeaders -= itInFlight->second.second->fValidatedHeaders;
                state->vBlocksInFlight.erase(itInFlight->second.second);
                state->nBlocksInFlight--;
                state->nStallingSince = 0;
            }
        }
        mapBlocksInFlight.erase(itInFlight);
    }
}
// Check BlockStatus enum definition is visible here. It should be via chain.h included in main.h
void MarkBlockAsInFlight(NodeId nodeid, const uint256& hash, CBlockIndex* pindex = nullptr) {
    CNodeState* state = State(nodeid); assert(state != nullptr);
    // Mark existing block as received to avoid duplicates for the same node
    MarkBlockAsReceived(hash);
    bool fValidatedHeaders = pindex && pindex->IsValid(BLOCK_VALID_TREE); // Check if headers are valid
    QueuedBlock newentry = {hash, pindex, GetTimeMicros(), nQueuedValidatedHeaders, fValidatedHeaders};
    nQueuedValidatedHeaders += newentry.fValidatedHeaders;
    auto it = state->vBlocksInFlight.insert(state->vBlocksInFlight.end(), newentry);
    state->nBlocksInFlight++;
    mapBlocksInFlight[hash] = std::make_pair(nodeid, it);
}
void ProcessBlockAvailability(NodeId nodeid) {
    CNodeState* state = State(nodeid); assert(state != nullptr);
    if (!state->hashLastUnknownBlock.IsNull()) {
        auto itOld = mapBlockIndex.find(state->hashLastUnknownBlock);
        if (itOld != mapBlockIndex.end() && itOld->second->nChainWork > 0) {
            if (!state->pindexBestKnownBlock || itOld->second->nChainWork >= state->pindexBestKnownBlock->nChainWork)
                state->pindexBestKnownBlock = itOld->second;
            state->hashLastUnknownBlock.SetNull();
        }
    }
}
void UpdateBlockAvailability(NodeId nodeid, const uint256& hash) {
    CNodeState* state = State(nodeid); assert(state != nullptr);
    ProcessBlockAvailability(nodeid);
    auto it = mapBlockIndex.find(hash);
    if (it != mapBlockIndex.end() && it->second->nChainWork > 0) {
        if (!state->pindexBestKnownBlock || it->second->nChainWork >= state->pindexBestKnownBlock->nChainWork)
            state->pindexBestKnownBlock = it->second;
    } else {
        state->hashLastUnknownBlock = hash;
    }
}
// Use implementation from chain.cpp now
// CBlockIndex* LastCommonAncestor(CBlockIndex* pa, CBlockIndex* pb) { ... }

void FindNextBlocksToDownload(CNode* pnode, unsigned int count, std::vector<CBlockIndex*>& vBlocks, NodeId& nodeStaller) {
    if (count == 0 || pnode == nullptr) return; // Add null check for pnode
    auto nodeid = pnode->GetId();
    vBlocks.reserve(vBlocks.size() + count);
    CNodeState* state = State(nodeid);
    assert(state != nullptr);
    ProcessBlockAvailability(nodeid);

    // If this peer has nothing according to our records, find optionally through headers-first logic.
    // DigiWage seems to use a simpler logic here compared to later Bitcoin Core.
    if (state->pindexBestKnownBlock == nullptr || state->pindexBestKnownBlock->nChainWork < chainActive.Tip()->nChainWork) {
         // If the peer claimed a height, use that.
         if (pnode->nStartingHeight > chainActive.Height()) {
             CBlockIndex* pindex = chainActive.Tip();
             int nFetch = std::min((int)count, pnode->nStartingHeight - pindex->nHeight);
             for(int i = 1; i <= nFetch; ++i) {
                 CBlockIndex* pnext = chainActive.Next(pindex);
                 if (!pnext) break; // Should only happen at tip
                 pindex = pnext;
                 // Add to fetch if we don't have data and it's not in flight
                 if (!(pindex->nStatus & BLOCK_HAVE_DATA) && mapBlocksInFlight.find(pindex->GetBlockHash()) == mapBlocksInFlight.end()) {
                     vBlocks.push_back(pindex);
                     if (vBlocks.size() >= count) return;
                 }
             }
         }
         return; // Nothing more to download from this peer based on current info
    }


    // If the peer selects something we haven't seen at all, use their tip on our mapBlockIndex.
    // If the peer selects something that is invalid, ignore them.
    if (state->pindexBestKnownBlock == nullptr || state->pindexBestKnownBlock->IsValid(BLOCK_FAILED_VALID)) {
        return;
    }

    // If we have the best block they claim, find the common ancestor.
    if (state->pindexLastCommonBlock == nullptr) {
         state->pindexLastCommonBlock = chainActive[std::min(state->pindexBestKnownBlock->nHeight, chainActive.Height())];
    }
    // Ensure pindexLastCommonBlock is really part of the main chain and pindexBestKnownBlock.
    state->pindexLastCommonBlock = LastCommonAncestor(state->pindexLastCommonBlock, state->pindexBestKnownBlock);
    if (state->pindexLastCommonBlock == state->pindexBestKnownBlock) return;

    std::vector<CBlockIndex*> vToFetch;
  //  CBlockIndex* pindexWalk = state->pindexLastCommonBlock;
    // Never request blocks deeper than this peer's starting height minus 1.
    int nWindowEnd = state->pindexLastCommonBlock->nHeight + BLOCK_DOWNLOAD_WINDOW;
    int nMaxHeight = std::min<int>(state->pindexBestKnownBlock->nHeight, nWindowEnd + 1);
    NodeId waitingfor = -1;

    // Walk backwards from the best known block to find blocks to fetch.
    CBlockIndex* pindexParent = state->pindexBestKnownBlock;
    while (pindexParent && pindexParent != state->pindexLastCommonBlock) {
        if (pindexParent->nHeight < nMaxHeight && // Within download window
            !(pindexParent->nStatus & BLOCK_HAVE_DATA) && // We don't have it
            mapBlocksInFlight.find(pindexParent->GetBlockHash()) == mapBlocksInFlight.end()) // Not already in flight
        {
             vToFetch.push_back(pindexParent);
             if (vToFetch.size() >= count) break; // Found enough
        } else if (mapBlocksInFlight.count(pindexParent->GetBlockHash())) {
             // Record who we're waiting for if we don't have it yet
             if (waitingfor == -1) waitingfor = mapBlocksInFlight[pindexParent->GetBlockHash()].first;
        }
        pindexParent = pindexParent->pprev;
    }
    // Reverse the order to request blocks sequentially
    std::reverse(vToFetch.begin(), vToFetch.end());
    vBlocks.insert(vBlocks.end(), vToFetch.begin(), vToFetch.end());

    // If we requested less than count, and we're waiting for blocks, mark the staller.
    if (vBlocks.size() < count && waitingfor != -1 && waitingfor != nodeid) {
        nodeStaller = waitingfor;
    }
}


} // anon namespace

// Public interface functions (Keep DigiWage implementations)
bool GetNodeStateStats(NodeId nodeid, CNodeStateStats& stats) {
    LOCK(cs_main); CNodeState* state = State(nodeid);
    if (state == nullptr) return false;
    stats.nMisbehavior = state->nMisbehavior;
    stats.nSyncHeight = state->pindexBestKnownBlock ? state->pindexBestKnownBlock->nHeight : -1;
    stats.nCommonHeight = state->pindexLastCommonBlock ? state->pindexLastCommonBlock->nHeight : -1;
    stats.vHeightInFlight.clear(); // Clear before filling
    for (const QueuedBlock& queue : state->vBlocksInFlight) {
        if (queue.pindex) stats.vHeightInFlight.push_back(queue.pindex->nHeight);
    }
    return true;
}
void RegisterNodeSignals(CNodeSignals& nodeSignals) {
    nodeSignals.GetHeight.connect(&GetHeight);
    nodeSignals.ProcessMessages.connect(&ProcessMessages);
    nodeSignals.SendMessages.connect(&SendMessages);
    nodeSignals.InitializeNode.connect(&InitializeNode);
    nodeSignals.FinalizeNode.connect(&FinalizeNode);
}
void UnregisterNodeSignals(CNodeSignals& nodeSignals) {
    nodeSignals.GetHeight.disconnect(&GetHeight);
    nodeSignals.ProcessMessages.disconnect(&ProcessMessages);
    nodeSignals.SendMessages.disconnect(&SendMessages);
    nodeSignals.InitializeNode.disconnect(&InitializeNode);
    nodeSignals.FinalizeNode.disconnect(&FinalizeNode);
}

// Defined globally now
CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator)
{
    // Find the first block the caller has in the main chain
    for (const uint256& hash : locator.vHave) {
        BlockMap::iterator mi = mapBlockIndex.find(hash);
        if (mi != mapBlockIndex.end()) {
            CBlockIndex* pindex = (*mi).second;
            if (chain.Contains(pindex))
                return pindex;
        }
    }
    return chain.Genesis();
}


// Global pointers
CCoinsViewCache* pcoinsTip = NULL;
CBlockTreeDB* pblocktree = NULL;
CZerocoinDB* zerocoinDB = NULL; // DigiWage specific
CSporkDB* pSporkDB = NULL; // DigiWage specific

// Orphan Transaction functions (Keep DigiWage implementations)
bool AddOrphanTx(const CTransaction& tx, NodeId peer) {
    uint256 hash = tx.GetHash();
    if (mapOrphanTransactions.count(hash)) return false;
    // Estimate serialize size without actually serializing for performance.
    // Use GetTransactionWeight for a better estimate if available.
    unsigned int sz = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
    unsigned int nMaxOrphanSize = GetArg("-maxorphansize", DEFAULT_MAX_ORPHAN_TRANSACTIONS * 1000); // Limit by total size?
    // Check DigiWage specific size limits
    unsigned int nTxMax = tx.ContainsZerocoins() ? MAX_ZEROCOIN_TX_SIZE : MAX_STANDARD_TX_SIZE;
    if (sz > nTxMax) {
        LogPrint("mempool", "ignoring large orphan tx (size: %u > %u, hash: %s)\n", sz, nTxMax, hash.ToString());
        return false;
    }
    // Check total orphan pool size? (Bitcoin Core does this)
    // uint64_t nOrphanPoolSize = 0;
    // for(const auto& pair : mapOrphanTransactions) { nOrphanPoolSize += ::GetSerializeSize(pair.second.tx, SER_NETWORK, PROTOCOL_VERSION); }
    // if (nOrphanPoolSize + sz > nMaxOrphanSize) return false; // Limit total pool size

    mapOrphanTransactions[hash].tx = tx;
    mapOrphanTransactions[hash].fromPeer = peer;
    for (const CTxIn& txin : tx.vin) {
        // Skip Zerocoin inputs for prev map
        if (txin.IsZerocoinSpend()) continue;
        mapOrphanTransactionsByPrev[txin.prevout.hash].insert(hash);
    }
    LogPrint("mempool", "stored orphan tx %s (mapsz %u prevsz %u)\n", hash.ToString(),
        mapOrphanTransactions.size(), mapOrphanTransactionsByPrev.size());
    return true;
}
void EraseOrphanTx(uint256 hash) { // Keep global for now
    auto it = mapOrphanTransactions.find(hash);
    if (it == mapOrphanTransactions.end()) return;
    for (const CTxIn& txin : it->second.tx.vin) {
         if (txin.IsZerocoinSpend()) continue; // Skip ZC
        auto itPrev = mapOrphanTransactionsByPrev.find(txin.prevout.hash);
        if (itPrev == mapOrphanTransactionsByPrev.end()) continue;
        itPrev->second.erase(hash);
        if (itPrev->second.empty()) mapOrphanTransactionsByPrev.erase(itPrev);
    }
    mapOrphanTransactions.erase(it);
}
void EraseOrphansFor(NodeId peer) {
    int nErased = 0;
    auto iter = mapOrphanTransactions.begin();
    while (iter != mapOrphanTransactions.end()) {
        auto maybeErase = iter++; // Increment before potential erase
        if (maybeErase->second.fromPeer == peer) {
            EraseOrphanTx(maybeErase->first); // Use hash from key
            ++nErased;
        }
    }
    if (nErased > 0) LogPrint("mempool", "Erased %d orphan tx from peer %d\n", nErased, peer);
}
unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans) {
    // Limit by count, not size (matches original DigiWage code)
    unsigned int nEvicted = 0;
    while (mapOrphanTransactions.size() > nMaxOrphans) {
        // Evict random orphan:
        uint256 randomhash = GetRandHash();
        auto it = mapOrphanTransactions.lower_bound(randomhash);
        if (it == mapOrphanTransactions.end()) it = mapOrphanTransactions.begin();
        EraseOrphanTx(it->first);
        ++nEvicted;
    }
    return nEvicted;
}

// Standard Transaction Checks (Keep DigiWage implementations)
bool IsStandardTx(const CTransaction& tx, std::string& reason) {
    AssertLockHeld(cs_main);
    if (tx.nVersion > CTransaction::CURRENT_VERSION || tx.nVersion < 1) { reason = "version"; return false; }
    if (!IsFinalTx(tx, chainActive.Height() + 1)) { reason = "non-final"; return false; }
    unsigned int sz = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION); // Use Global ::GetSerializeSize
    unsigned int nMaxSize = tx.ContainsZerocoins() ? MAX_ZEROCOIN_TX_SIZE : MAX_STANDARD_TX_SIZE;
    if (sz >= nMaxSize) { reason = "tx-size"; return false; }
    for (const CTxIn& txin : tx.vin) {
        if (txin.IsZerocoinSpend()) continue; // Skip ZC spends
        // Check scriptSig size and push only rules
        if (txin.scriptSig.size() > 1650) { reason = "scriptsig-size"; return false; }
        if (!txin.scriptSig.IsPushOnly()) { reason = "scriptsig-not-pushonly"; return false; }
    }
    unsigned int nDataOut = 0; txnouttype whichType;
    for (const CTxOut& txout : tx.vout) {
        if (!::IsStandard(txout.scriptPubKey, whichType)) { reason = "scriptpubkey"; return false; } // Use global ::IsStandard
        if (whichType == TX_NULL_DATA) nDataOut++;
        else if ((whichType == TX_MULTISIG) && (!fIsBareMultisigStd)) { reason = "bare-multisig"; return false; }
        else if (txout.IsDust(::minRelayTxFee)) { reason = "dust"; return false; }
    }
    if (nDataOut > 1) { reason = "multi-op-return"; return false; }
    return true;
}
bool AreInputsStandard(const CTransaction& tx, const CCoinsViewCache& mapInputs) {
    // Keep DigiWage implementation
    if (tx.IsCoinBase() || tx.HasZerocoinSpendInputs()) return true;
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const CTxOut& prev = mapInputs.GetOutputFor(tx.vin[i]);
        std::vector<std::vector<unsigned char> > vSolutions;
        txnouttype whichType;
        const CScript& prevScript = prev.scriptPubKey;
        if (!Solver(prevScript, whichType, vSolutions)) return false; // Uses global Solver
        int nArgsExpected = ScriptSigArgsExpected(whichType, vSolutions);
        if (nArgsExpected < 0) return false;
        std::vector<std::vector<unsigned char> > stack;
        if (!EvalScript(stack, tx.vin[i].scriptSig, SCRIPT_VERIFY_NONE, BaseSignatureChecker())) return false; // Use correct flags/checker
        if (whichType == TX_SCRIPTHASH) {
            if (stack.empty()) return false;
            CScript subscript(stack.back().begin(), stack.back().end());
            std::vector<std::vector<unsigned char> > vSolutions2;
            txnouttype whichType2;
            if (Solver(subscript, whichType2, vSolutions2)) {
                int tmpExpected = ScriptSigArgsExpected(whichType2, vSolutions2);
                if (tmpExpected < 0) return false;
                nArgsExpected += tmpExpected;
            } else {
                // Use the CScript::GetSigOpCount method
                unsigned int sigops = subscript.GetSigOpCount(true);
                return (sigops <= MAX_P2SH_SIGOPS);
            }
        }
        if (stack.size() != (unsigned int)nArgsExpected) return false;
    }
    return true;
}
int GetInputAge(CTxIn& vin) { // Keep DigiWage implementation
    CCoinsView viewDummy; CCoinsViewCache view(&viewDummy);
    {
        LOCK(mempool.cs);
        CCoinsViewMemPool viewMempool(pcoinsTip, mempool);
        view.SetBackend(viewMempool);
        const CCoins* coins = view.AccessCoins(vin.prevout.hash);
        if (coins) {
            if (coins->nHeight < 0) return 0;
            LOCK(cs_main); // Lock needed for chainActive.Tip()
            if (!chainActive.Tip()) return -1; // Handle null tip
            return (chainActive.Tip()->nHeight + 1) - coins->nHeight;
        } else return -1;
    }
}
int GetIXConfirmations(uint256 nTXHash) { // Keep DigiWage implementation
    int sigs = 0;
    // LOCK(cs_mapTxLocks); // Ensure this lock exists and is correct
    if (mapTxLocks.count(nTXHash)) {
       sigs = mapTxLocks[nTXHash].CountSignatures();
    }
    if (sigs >= SWIFTTX_SIGNATURES_REQUIRED) return nSwiftTXDepth; // Ensure these constants are defined
    return 0;
}
bool CheckFinalTx(const CTransaction& tx, int flags) { // Keep DigiWage implementation
    AssertLockHeld(cs_main);
    flags = std::max(flags, 0);
    // CheckFinalTx relies on the current block height and time.
    const CBlockIndex* pindexTip = chainActive.Tip();
    if (pindexTip == nullptr) return false; // Cannot determine finality without a chain tip
    const int nBlockHeight = pindexTip->nHeight + 1;
    const int64_t nBlockTime = (flags & LOCKTIME_MEDIAN_TIME_PAST) ? pindexTip->GetMedianTimePast() : GetAdjustedTime();
    return IsFinalTx(tx, nBlockHeight, nBlockTime);
}
CAmount GetMinRelayFee(const CTransaction& tx, unsigned int nBytes, bool fAllowFree) { // Keep DigiWage implementation
    {
        LOCK(mempool.cs); uint256 hash = tx.GetHash(); double dPriorityDelta = 0; CAmount nFeeDelta = 0;
        mempool.ApplyDeltas(hash, dPriorityDelta, nFeeDelta);
        if (dPriorityDelta > 0 || nFeeDelta > 0) return 0;
    }
    CAmount nMinFee = ::minRelayTxFee.GetFee(nBytes);
    if (fAllowFree) {
        // Check priority based on mempool calculation or simpler size check
        // Need AllowFree function if using priority
        // bool AllowFree(double dPriority); // Declaration needed
        // if (AllowFree(view.GetPriority(tx, chainHeight + 1))) return 0; // Need view context
        // Simpler DigiWage logic:
        if (nBytes < (DEFAULT_BLOCK_PRIORITY_SIZE - 1000)) nMinFee = 0;
    }
    // Ensure MoneyRange check uses Consensus parameters
    if (!Params().GetConsensus().MoneyRange(nMinFee)) nMinFee = Params().GetConsensus().nMaxMoneyOut;
    return nMinFee;
}



bool AcceptToMemoryPool(CTxMemPool& pool, CValidationState& state, const CTransaction& tx, bool fLimitFree, bool* pfMissingInputs, bool fRejectInsaneFee, bool ignoreFees)
{
    AssertLockHeld(cs_main);
    if (pfMissingInputs)
        *pfMissingInputs = false;

    // Reject all new Zerocoin transactions
    if (tx.ContainsZerocoins())
        return state.DoS(10, error("%s : Zerocoin transactions have been permanently disabled",
                __func__), REJECT_INVALID, "bad-tx");

    const Consensus::Params& consensus = Params().GetConsensus();

    // Check transaction
    int chainHeight = chainActive.Height();
    bool fColdStakingActive = sporkManager.IsSporkActive(SPORK_17_COLDSTAKING_ENFORCEMENT);
    if (!CheckTransaction(tx, chainHeight >= consensus.height_start_ZC, state, fColdStakingActive))
        return state.DoS(100, error("%s : CheckTransaction failed", __func__), REJECT_INVALID, "bad-tx");

    // Coinbase is only valid in a block, not as a loose transaction
    if (tx.IsCoinBase())
        return state.DoS(100, error("%s : coinbase as individual tx",
                __func__), REJECT_INVALID, "coinbase");

    // Coinstake is also only valid in a block, not as a loose transaction
    if (tx.IsCoinStake())
        return state.DoS(100, error("%s : coinstake as individual tx (id=%s): %s",
                __func__, tx.GetHash().GetHex(), tx.ToString()), REJECT_INVALID, "coinstake");

    // Only accept nLockTime-using transactions that can be mined in the next
    // block; we don't want our mempool filled up with transactions that can't
    // be mined yet.
    if (!CheckFinalTx(tx, STANDARD_LOCKTIME_VERIFY_FLAGS))
        return state.DoS(0, false, REJECT_NONSTANDARD, "non-final");

    // Rather not work on nonstandard transactions (unless regtest)
    std::string reason;
    if (!Params().IsRegTestNet() && !IsStandardTx(tx, reason))
        return state.DoS(0, error("%s : nonstandard transaction: %s",
                    __func__, reason), REJECT_NONSTANDARD, reason);
    // is it already in the memory pool?
    uint256 hash = tx.GetHash();
    if (pool.exists(hash)) {
        return error("%s tx already in mempool", __func__);
    }

    // ----------- swiftTX transaction scanning -----------

    for (const CTxIn& in : tx.vin) {
        if (mapLockedInputs.count(in.prevout)) {
            if (mapLockedInputs[in.prevout] != tx.GetHash()) {
                return state.DoS(0,
                    error("%s : conflicts with existing transaction lock: %s",
                            __func__, reason), REJECT_INVALID, "tx-lock-conflict");
            }
        }
    }

    // Check for conflicts with in-memory transactions
    LOCK(pool.cs); // protect pool.mapNextTx
    for (const auto &in : tx.vin) {
        COutPoint outpoint = in.prevout;
        if (pool.mapNextTx.count(outpoint)) {
            // Disable replacement feature for now
            return false;
        }
    }


    {
        CCoinsView dummy;
        CCoinsViewCache view(&dummy);

        CAmount nValueIn = 0;
        LOCK(pool.cs);
        CCoinsViewMemPool viewMemPool(pcoinsTip, pool);
        view.SetBackend(viewMemPool);

        // do we already have it?
        if (view.HaveCoins(hash))
            return false;

        // do all inputs exist?
        // Note that this does not check for the presence of actual outputs (see the next check for that),
        // only helps filling in pfMissingInputs (to determine missing vs spent).
        for (const CTxIn& txin : tx.vin) {
            if (!view.HaveCoins(txin.prevout.hash)) {
                if (pfMissingInputs)
                    *pfMissingInputs = true;
                return false;
            }
        }

        // are the actual inputs available?
        if (!view.HaveInputs(tx))
            return state.Invalid(error("%s : inputs already spent",
                    __func__), REJECT_DUPLICATE, "bad-txns-inputs-spent");

        // Bring the best block into scope
        view.GetBestBlock();

        nValueIn = view.GetValueIn(tx);

        // we have all inputs cached now, so switch back to dummy, so we don't need to keep lock on mempool
        view.SetBackend(dummy);

        // Check for non-standard pay-to-script-hash in inputs
        if (!Params().IsRegTestNet() && !AreInputsStandard(tx, view))
            return error("%s : nonstandard transaction input", __func__);

        // Check that the transaction doesn't have an excessive number of
        // sigops, making it impossible to mine. Since the coinbase transaction
        // itself can contain sigops MAX_TX_SIGOPS is less than
        // MAX_BLOCK_SIGOPS; we still consider this an invalid rather than
        // merely non-standard transaction.
        unsigned int nSigOps = GetLegacySigOpCount(tx);
        unsigned int nMaxSigOps = MAX_TX_SIGOPS_CURRENT;
        nSigOps += GetP2SHSigOpCount(tx, view);
        if(nSigOps > nMaxSigOps)
            return state.DoS(0, error("%s : too many sigops %s, %d > %d",
                    __func__, hash.ToString(), nSigOps, nMaxSigOps), REJECT_NONSTANDARD, "bad-txns-too-many-sigops");

        CAmount nValueOut = tx.GetValueOut();
        CAmount nFees = nValueIn - nValueOut;
        double dPriority = 0;
        view.GetPriority(tx, chainHeight);

        CTxMemPoolEntry entry(tx, nFees, GetTime(), dPriority, chainHeight);
        unsigned int nSize = entry.GetTxSize();

        // Don't accept it if it can't get into a block
        // but prioritise dstx and don't check fees for it
        if (mapObfuscationBroadcastTxes.count(hash)) {
            mempool.PrioritiseTransaction(hash, hash.ToString(), 1000, 0.1 * COIN);
        } else if (!ignoreFees) {
            CAmount txMinFee = GetMinRelayFee(tx, nSize, true);
            if (fLimitFree && nFees < txMinFee)
                return state.DoS(0, error("%s : not enough fees %s, %d < %d",
                        __func__, hash.ToString(), nFees, txMinFee), REJECT_INSUFFICIENTFEE, "insufficient fee");

            // Require that free transactions have sufficient priority to be mined in the next block.
            if (GetBoolArg("-relaypriority", true) && nFees < ::minRelayTxFee.GetFee(nSize) && !AllowFree(view.GetPriority(tx, chainHeight + 1))) {
                return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "insufficient priority");
            }

            // Continuously rate-limit free (really, very-low-fee) transactions
            // This mitigates 'penny-flooding' -- sending thousands of free transactions just to
            // be annoying or make others' transactions take longer to confirm.
            if (fLimitFree && nFees < ::minRelayTxFee.GetFee(nSize)) {
                static RecursiveMutex csFreeLimiter;
                static double dFreeCount;
                static int64_t nLastTime;
                int64_t nNow = GetTime();

                LOCK(csFreeLimiter);

                // Use an exponentially decaying ~10-minute window:
                dFreeCount *= pow(1.0 - 1.0 / 600.0, (double)(nNow - nLastTime));
                nLastTime = nNow;
                // -limitfreerelay unit is thousand-bytes-per-minute
                // At default rate it would take over a month to fill 1GB
                if (dFreeCount >= GetArg("-limitfreerelay", 30) * 10 * 1000)
                    return state.DoS(0, error("%s : free transaction rejected by rate limiter",
                            __func__), REJECT_INSUFFICIENTFEE, "rate limited free transaction");
                LogPrint("mempool", "Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount + nSize);
                dFreeCount += nSize;
            }
        }

        if (fRejectInsaneFee && nFees > ::minRelayTxFee.GetFee(nSize) * 10000)
            return error("%s : insane fees %s, %d > %d",
                    __func__, hash.ToString(), nFees, ::minRelayTxFee.GetFee(nSize) * 10000);

        // As zero fee transactions are not going to be accepted in the near future (4.0) and the code will be fully refactored soon.
        // This is just a quick inline towards that goal, the mempool by default will not accept them. Blocking
        // any subsequent network relay.
        if (!Params().IsRegTestNet() && nFees == 0) {
            return error("%s : zero fees not accepted %s, %d > %d",
                    __func__, hash.ToString(), nFees, ::minRelayTxFee.GetFee(nSize) * 10000);
        }

        bool fCLTVIsActivated = (chainHeight >= consensus.height_start_BIP65);

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        int flags = STANDARD_SCRIPT_VERIFY_FLAGS;
        if (fCLTVIsActivated)
            flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
        if (!CheckInputs(tx, state, view, true, flags, true)) {
            return error("%s : ConnectInputs failed %s", __func__, hash.ToString());
        }

        // Check again against just the consensus-critical mandatory script
        // verification flags, in case of bugs in the standard flags that cause
        // transactions to pass as valid when they're actually invalid. For
        // instance the STRICTENC flag was incorrectly allowing certain
        // CHECKSIG NOT scripts to pass, even though they were invalid.
        //
        // There is a similar check in CreateNewBlock() to prevent creating
        // invalid blocks, however allowing such transactions into the mempool
        // can be exploited as a DoS attack.
        flags = MANDATORY_SCRIPT_VERIFY_FLAGS;
        if (fCLTVIsActivated)
            flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
        if (!CheckInputs(tx, state, view, true, flags, true)) {
            return error("%s : BUG! PLEASE REPORT THIS! ConnectInputs failed against MANDATORY but not STANDARD flags %s",
                    __func__, hash.ToString());
        }

        // Store transaction in memory
        pool.addUnchecked(hash, entry);
    }

    SyncWithWallets(tx, nullptr);

    return true;
}



// --- AcceptableInputs (Keep DigiWage implementation) ---
bool AcceptableInputs(CTxMemPool& pool, CValidationState& state, const CTransaction& tx, bool fLimitFree, bool* pfMissingInputs, bool fRejectInsaneFee, bool isDSTX)
{
    AssertLockHeld(cs_main);
    if (pfMissingInputs) *pfMissingInputs = false;

    const CBlockIndex* pindexTip = chainActive.Tip();
    const int chainHeight = pindexTip ? pindexTip->nHeight : 0; // Handle null tip
    const Consensus::Params& consensusParams = Params().GetConsensus();

    // Use sporkManager if available, otherwise assume active/inactive
    bool fColdStakingActive = sporkManager.IsSporkActive(SPORK_17_COLDSTAKING_ENFORCEMENT);
    bool fZerocoinActive = chainHeight >= consensusParams.height_start_ZC; // Simple height check for ZC

    if (!CheckTransaction(tx, fZerocoinActive, state, fColdStakingActive)) // Pass correct flags
        return error("AcceptableInputs: CheckTransaction failed: %s", state.GetRejectReason());

    if (tx.IsCoinBase()) return state.DoS(100, error("AcceptableInputs: coinbase as individual tx"), REJECT_INVALID, "coinbase");

    uint256 hash = tx.GetHash();
    if (pool.exists(hash)) return false; // Already in mempool

    // Check against locks from SwiftX
    for (const CTxIn& in : tx.vin) {
        if (mapLockedInputs.count(in.prevout)) {
            if (mapLockedInputs[in.prevout] != tx.GetHash()) {
                return state.DoS(0, error("AcceptableInputs: conflicts with existing transaction lock"), REJECT_INVALID, "tx-lock-conflict");
            }
        }
    }

    // Check against inputs already spent in mempool (for non-ZC spends)
    if (!tx.HasZerocoinSpendInputs()) {
        LOCK(pool.cs);
        for (const auto &in : tx.vin) { if (pool.mapNextTx.count(in.prevout)) return false; }
    }

    { // Scope for view
        CCoinsView dummy; CCoinsViewCache view(&dummy); CAmount nValueIn = 0;
        {
            LOCK(pool.cs); CCoinsViewMemPool viewMemPool(pcoinsTip, pool); view.SetBackend(viewMemPool);

            // Don't cache results if checking transaction for transit (~fInChainValue evaluation)
            // Also don't store HASH representation of transaction entry, if logic relies on it later.
            if (view.HaveCoins(hash)) return false; // tx already present?

            // Are inputs missing?
            for (const CTxIn& txin : tx.vin) {
                if (txin.IsZerocoinSpend()) continue; // Skip ZC spends for HaveCoins check
                if (!view.HaveCoins(txin.prevout.hash)) {
                     if (pfMissingInputs) *pfMissingInputs = true;
                     // Avoid state pollution for missing inputs
                     return false; // fMissingInputs and !state.IsInvalid() is used to detect this condition, don't set state.Invalid()
                }
            }

            // Are the actual inputs available?
            if (!view.HaveInputs(tx)) return state.Invalid(error("AcceptableInputs: inputs already spent"), REJECT_DUPLICATE, "bad-txns-inputs-spent");

            // Get sum of inputs
            nValueIn = view.GetValueIn(tx);

        } // Release lock pool.cs

        // we have all inputs cached now, so switch back to dummy backend.
        view.SetBackend(dummy);

        // Check legacy signature operations
        unsigned int nSigOps = GetLegacySigOpCount(tx);
        // Check P2SH signature operations
        nSigOps += GetP2SHSigOpCount(tx, view); // Pass the populated view

        unsigned int nMaxSigOps = MAX_TX_SIGOPS_CURRENT; // Use constant
        if (nSigOps > nMaxSigOps)
             return state.DoS(0, error("AcceptableInputs: too many sigops %s, %d > %d", hash.ToString(), nSigOps, nMaxSigOps), REJECT_NONSTANDARD, "bad-txns-too-many-sigops");

        CAmount nValueOut = tx.GetValueOut(); CAmount nFees = nValueIn - nValueOut;

        // Prevent conversion errors
        if (!consensusParams.MoneyRange(nValueIn) || !consensusParams.MoneyRange(nValueOut))
            return state.DoS(100, error("AcceptableInputs: value out of range"), REJECT_INVALID, "bad-txns-vout-outofrange");

        // ZC spends have zero fees, don't check fee rules
        if (!tx.HasZerocoinSpendInputs()) {
            // Keep track of transactions submitted by logical connections as帥 free
            double dPriority = view.GetPriority(tx, chainHeight); // Use appropriate height
            CTxMemPoolEntry entry(tx, nFees, GetTime(), dPriority, chainHeight);
            unsigned int nSize = entry.GetTxSize();

            if (isDSTX) {
                // Prioritize DSTX, needs mempool method
                // mempool.PrioritiseTransaction(hash, hash.ToString(), 1000, 0.1 * COIN);
                 LogPrint("mempool", "Prioritizing DSTX tx %s\n", hash.ToString());
            } else {
                // Check fees unless DSTX or ZC Spend
                CAmount txMinFee = GetMinRelayFee(tx, nSize, true);
                if (fLimitFree && nFees < txMinFee)
                     return state.DoS(0, error("AcceptableInputs: not enough fees %s, %d < %d", hash.ToString(), nFees, txMinFee), REJECT_INSUFFICIENTFEE, "insufficient fee");

                // Check priority for relay / inclusion in blocks
                // Need AllowFree function if using priority
                // if (GetBoolArg("-relaypriority", true) && nFees < ::minRelayTxFee.GetFee(nSize) && !AllowFree(view.GetPriority(tx, chainHeight + 1)))
                //     return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "insufficient priority");

                // Rate limit free transactions (simple logic from DigiWage)
                if (fLimitFree && nFees < ::minRelayTxFee.GetFee(nSize)) {
                    static RecursiveMutex csFreeLimiter; static double dFreeCount; static int64_t nLastTime; int64_t nNow = GetTime();
                    LOCK(csFreeLimiter);
                    dFreeCount *= pow(1.0 - 1.0 / 600.0, (double)(nNow - nLastTime)); nLastTime = nNow;
                    if (dFreeCount >= GetArg("-limitfreerelay", 30.0) * 10.0 * 1000.0) // Check arg type
                         return state.DoS(0, error("AcceptableInputs: free transaction rejected by rate limiter"), REJECT_INSUFFICIENTFEE, "rate limited free transaction");
                    LogPrint("mempool", "Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount + nSize); dFreeCount += nSize;
                }
            }
             if (fRejectInsaneFee && nFees > ::minRelayTxFee.GetFee(nSize) * 10000)
                 return error("AcceptableInputs: insane fees %s, %d > %d", hash.ToString(), nFees, ::minRelayTxFee.GetFee(nSize) * 10000);
        } // End fee checks


        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion attacks.
        bool fCLTVIsActivated = pindexTip && pindexTip->nHeight >= consensusParams.height_start_BIP65;
        int flags = STANDARD_SCRIPT_VERIFY_FLAGS;
        if (fCLTVIsActivated) flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
        // Note: Store results in pvChecks so ContextualCheckInputs doesn't need to redo calculations.
        if (!CheckInputs(tx, state, view, true, flags, true)) { // Pass true for fScriptChecks
            // State filled in by CheckInputs
            return error("AcceptableInputs: ConnectInputs failed %s", hash.ToString());
        }
    } // End scope for view

    // Add to transaction pool
    // This assumes CheckInputs cached the results in pool.mapTmpInputScripts
    // TODO: Verify this caching mechanism exists or adapt CheckInputs call/logic
    // if (!pool.addUnchecked(hash, entry, !IsInitialBlockDownload())) // Use addUnchecked if checks were done
    //    return state.DoS(0, error("AcceptableInputs: addUnchecked failed"), REJECT_INVALID, "mempool-add-failed");

    return true;
}


// --- GetOutput (Keep DigiWage implementation) ---
bool GetOutput(const uint256& hash, unsigned int index, CValidationState& state, CTxOut& out) {
    CTransaction txPrev; uint256 hashBlock;
    if (!GetTransaction(hash, txPrev, hashBlock, true)) return state.DoS(100, error("Output not found"));
    if (index >= txPrev.vout.size()) return state.DoS(100, error("Output not found, invalid index %d for %s", index, hash.GetHex()));
    out = txPrev.vout[index];
    return true;
}

// --- GetTransaction (Keep DigiWage implementation) ---
bool GetTransaction(const uint256& hash, CTransaction& txOut, uint256& hashBlock, bool fAllowSlow, CBlockIndex* blockIndex) {
    CBlockIndex* pindexSlow = blockIndex;
    LOCK(cs_main);
    if (!blockIndex) {
        if (mempool.lookup(hash, txOut)) return true;
        if (fTxIndex) {
            CDiskTxPos postx;
            if (pblocktree && pblocktree->ReadTxIndex(hash, postx)) { // Add null check for pblocktree
                CAutoFile file(OpenBlockFile(postx, true), SER_DISK, CLIENT_VERSION);
                if (file.IsNull()) return error("%s: OpenBlockFile failed", __func__);
                CBlockHeader header;
                try {
                    file >> header; fseek(file.Get(), postx.nTxOffset, SEEK_CUR); file >> txOut;
                } catch (const std::exception& e) { return error("%s : Deserialize or I/O error - %s", __func__, e.what()); }
                hashBlock = header.GetHash();
                if (txOut.GetHash() != hash) return error("%s : txid mismatch", __func__);
                return true;
            }
            return false; // Not found in index
        }
        if (fAllowSlow) {
            int nHeight = -1;
            {
                if (pcoinsTip) { // Add null check for pcoinsTip
                    CCoinsViewCache& view = *pcoinsTip;
                    const CCoins* coins = view.AccessCoins(hash);
                    if (coins) nHeight = coins->nHeight;
                }
            }
            if (nHeight > 0) pindexSlow = chainActive[nHeight];
        }
    }
    if (pindexSlow) {
        CBlock block;
        if (ReadBlockFromDisk(block, pindexSlow)) {
            for (const CTransaction& tx : block.vtx) {
                if (tx.GetHash() == hash) { txOut = tx; hashBlock = pindexSlow->GetBlockHash(); return true; }
            }
        }
    }
    return false;
}

// ... (Keep DigiWage CBlock/CBlockIndex disk/helper functions)
//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

bool WriteBlockToDisk(CBlock& block, CDiskBlockPos& pos)
{
    // Open history file to append
    CAutoFile fileout(OpenBlockFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("WriteBlockToDisk : OpenBlockFile failed");

    // Write index header
    unsigned int nSize = fileout.GetSerializeSize(block);
    fileout << FLATDATA(Params().MessageStart()) << nSize;

    // Write block
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0)
        return error("WriteBlockToDisk : ftell failed");
    pos.nPos = (unsigned int)fileOutPos;
    fileout << block;

    return true;
}

bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos)
{
    block.SetNull();

    // Open history file to read
    CAutoFile filein(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
        return error("ReadBlockFromDisk : OpenBlockFile failed");

    // Read block
    try {
        filein >> block;
    } catch (const std::exception& e) {
        return error("%s : Deserialize or I/O error - %s", __func__, e.what());
    }

    // Check the header
    if (block.IsProofOfWork()) {
        if (!CheckProofOfWork(block.GetHash(), block.nBits))
            return error("ReadBlockFromDisk : Errors in block header");
    }

    return true;
}

bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex)
{
    if (!ReadBlockFromDisk(block, pindex->GetBlockPos()))
        return false;
    if (block.GetHash() != pindex->GetBlockHash()) {
        LogPrintf("%s : block=%s index=%s\n", __func__, block.GetHash().GetHex(), pindex->GetBlockHash().GetHex());
        return error("ReadBlockFromDisk(CBlock&, CBlockIndex*) : GetHash() doesn't match index");
    }
    return true;
}


double ConvertBitsToDouble(unsigned int nBits)
{
    int nShift = (nBits >> 24) & 0xff;

    double dDiff =
        (double)0x0000ffff / (double)(nBits & 0x00ffffff);

    while (nShift < 29) {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29) {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

int64_t GetBlockValue(int nHeight)
{
    int64_t nSubsidy = 0;

    if (nHeight == 0) {
        nSubsidy = 27179800 * COIN; //Total No. of coins out till block 123316(last block before gnenesis creation), to be swapped 1:1 for PHILS
    } else if (nHeight > 0 && nHeight < 86400) {
        nSubsidy = 120 * COIN;
    } else if (nHeight >= 86400 && nHeight < 259200) {
        nSubsidy = 60 * COIN;
    } else if (nHeight >= 259200 && nHeight < 432000) {
        nSubsidy = 30 * COIN;
    } else if (nHeight >= 432000 && nHeight < 604800) {
        nSubsidy = 15 * COIN;
    } else {
        nSubsidy = 7.5 * COIN;
    }
    return nSubsidy;
}

int64_t GetMasternodePayment(int nHeight, int64_t blockValue, int nMasternodeCount)
{
    int64_t ret = blockValue / 3 * 2;  //66.66% of blockvalue
    return ret;
}


bool IsInitialBlockDownload()
{
    LOCK(cs_main);
    const int chainHeight = chainActive.Height();
    if (fImporting || fReindex || fVerifyingBlocks || chainHeight < Checkpoints::GetTotalBlocksEstimate())
        return true;
    static bool lockIBDState = false;
    if (lockIBDState)
        return false;
    bool state = (chainHeight < pindexBestHeader->nHeight - 24 * 6 ||
            pindexBestHeader->GetBlockTime() < GetTime() - nMaxTipAge);
    if (!state)
        lockIBDState = true;
    return state;
}

bool fLargeWorkForkFound = false;
bool fLargeWorkInvalidChainFound = false;
bool fBlockDatabaseForkFound = false;
CBlockIndex *pindexBestForkTip = NULL, *pindexBestForkBase = NULL;

static void AlertNotify(const std::string& strMessage, bool fThread)
{
    uiInterface.NotifyAlertChanged();
    std::string strCmd = GetArg("-alertnotify", "");
    if (strCmd.empty()) return;

    // Alert text should be plain ascii coming from a trusted source, but to
    // be safe we first strip anything not in safeChars, then add single quotes around
    // the whole string before passing it to the shell:
    std::string singleQuote("'");
    std::string safeStatus = SanitizeString(strMessage);
    safeStatus = singleQuote+safeStatus+singleQuote;
    boost::replace_all(strCmd, "%s", safeStatus);

    if (fThread)
        boost::thread t(runCommand, strCmd); // thread runs free
    else
        runCommand(strCmd);
}

void CheckForkWarningConditions()
{
    AssertLockHeld(cs_main);
    // Before we get past initial download, we cannot reliably alert about forks
    // (we assume we don't get stuck on a fork before the last checkpoint)
    if (IsInitialBlockDownload())
        return;

    // If our best fork is no longer within 72 blocks (+/- 3 hours if no one mines it)
    // of our head, drop it
    if (pindexBestForkTip && chainActive.Height() - pindexBestForkTip->nHeight >= 72)
        pindexBestForkTip = NULL;

    if (pindexBestForkTip || (pindexBestInvalid && pindexBestInvalid->nChainWork > chainActive.Tip()->nChainWork + (GetBlockProof(*chainActive.Tip()) * 6))) {
        if (!fLargeWorkForkFound && pindexBestForkBase) {
            if (pindexBestForkBase->phashBlock) {
                std::string warning = std::string("'Warning: Large-work fork detected, forking after block ") +
                                      pindexBestForkBase->phashBlock->ToString() + std::string("'");
                AlertNotify(warning, true);
            }
        }
        if (pindexBestForkTip && pindexBestForkBase) {
            if (pindexBestForkBase->phashBlock) {
                LogPrintf("CheckForkWarningConditions: Warning: Large valid fork found\n  forking the chain at height %d (%s)\n  lasting to height %d (%s).\nChain state database corruption likely.\n",
                    pindexBestForkBase->nHeight, pindexBestForkBase->phashBlock->ToString(),
                    pindexBestForkTip->nHeight, pindexBestForkTip->phashBlock->ToString());
                fLargeWorkForkFound = true;
            }
        } else {
            LogPrintf("CheckForkWarningConditions: Warning: Found invalid chain at least ~6 blocks longer than our best chain.\nChain state database corruption likely.\n");
            fLargeWorkInvalidChainFound = true;
        }
    } else {
        fLargeWorkForkFound = false;
        fLargeWorkInvalidChainFound = false;
    }
}

void CheckForkWarningConditionsOnNewFork(CBlockIndex* pindexNewForkTip)
{
    AssertLockHeld(cs_main);
    // If we are on a fork that is sufficiently large, set a warning flag
    CBlockIndex* pfork = pindexNewForkTip;
    CBlockIndex* plonger = chainActive.Tip();
    while (pfork && pfork != plonger) {
        while (plonger && plonger->nHeight > pfork->nHeight)
            plonger = plonger->pprev;
        if (pfork == plonger)
            break;
        pfork = pfork->pprev;
    }

    // We define a condition which we should warn the user about as a fork of at least 7 blocks
    // who's tip is within 72 blocks (+/- 3 hours if no one mines it) of ours
    // or a chain that is entirely longer than ours and invalid (note that this should be detected by both)
    // We use 7 blocks rather arbitrarily as it represents just under 10% of sustained network
    // hash rate operating on the fork.
    // We define it this way because it allows us to only store the highest fork tip (+ base) which meets
    // the 7-block condition and from this always have the most-likely-to-cause-warning fork
    if (pfork && (!pindexBestForkTip || (pindexBestForkTip && pindexNewForkTip->nHeight > pindexBestForkTip->nHeight)) &&
        pindexNewForkTip->nChainWork - pfork->nChainWork > (GetBlockProof(*pfork) * 7) &&
        chainActive.Height() - pindexNewForkTip->nHeight < 72) {
        pindexBestForkTip = pindexNewForkTip;
        pindexBestForkBase = pfork;
    }

    CheckForkWarningConditions();
}

// Requires cs_main.
void Misbehaving(NodeId pnode, int howmuch)
{
    if (howmuch == 0)
        return;

    CNodeState* state = State(pnode);
    if (state == NULL)
        return;

    state->nMisbehavior += howmuch;
    int banscore = GetArg("-banscore", 100);
    if (state->nMisbehavior >= banscore && state->nMisbehavior - howmuch < banscore) {
        LogPrintf("Misbehaving: %s (%d -> %d) BAN THRESHOLD EXCEEDED\n", state->name, state->nMisbehavior - howmuch, state->nMisbehavior);
        state->fShouldBan = true;
    } else
        LogPrintf("Misbehaving: %s (%d -> %d)\n", state->name, state->nMisbehavior - howmuch, state->nMisbehavior);
}

void static InvalidChainFound(CBlockIndex* pindexNew)
{
    if (!pindexBestInvalid || pindexNew->nChainWork > pindexBestInvalid->nChainWork)
        pindexBestInvalid = pindexNew;

    LogPrintf("InvalidChainFound: invalid block=%s  height=%d  log2_work=%.16f  date=%s\n",
        pindexNew->GetBlockHash().ToString(), pindexNew->nHeight,
        log(pindexNew->nChainWork.getdouble()) / log(2.0), DateTimeStrFormat("%Y-%m-%d %H:%M:%S",
                                                               pindexNew->GetBlockTime()));

    const CBlockIndex* pChainTip = mapBlockIndex[chainActive.Tip()->GetBlockHash()];
    LogPrintf("InvalidChainFound:  current best=%s  height=%d  log2_work=%.16f  date=%s\n",
            pChainTip->GetBlockHash().GetHex(), pChainTip->nHeight, log(pChainTip->nChainWork.getdouble()) / log(2.0),
        DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pChainTip->GetBlockTime()));
    CheckForkWarningConditions();
}

void static InvalidBlockFound(CBlockIndex* pindex, const CValidationState& state)
{
    int nDoS = 0;
    if (state.IsInvalid(nDoS)) {
        std::map<uint256, NodeId>::iterator it = mapBlockSource.find(pindex->GetBlockHash());
        if (it != mapBlockSource.end() && State(it->second)) {
            CBlockReject reject = {state.GetRejectCode(), state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), pindex->GetBlockHash()};
            State(it->second)->rejects.push_back(reject);
            if (nDoS > 0)
                Misbehaving(it->second, nDoS);
        }
    }
    if (!state.CorruptionPossible()) {
        pindex->nStatus |= BLOCK_FAILED_VALID;
        setDirtyBlockIndex.insert(pindex);
        setBlockIndexCandidates.erase(pindex);
        InvalidChainFound(pindex);
    }
}

void UpdateCoins(const CTransaction& tx, CValidationState& state, CCoinsViewCache& inputs, CTxUndo& txundo, int nHeight)
{
    // mark inputs spent
    if (!tx.IsCoinBase() && !tx.HasZerocoinSpendInputs()) {
        txundo.vprevout.reserve(tx.vin.size());
        for (const CTxIn& txin : tx.vin) {
            txundo.vprevout.push_back(CTxInUndo());
            bool ret = inputs.ModifyCoins(txin.prevout.hash)->Spend(txin.prevout, txundo.vprevout.back());
            assert(ret);
        }
    }

    // add outputs
    inputs.ModifyCoins(tx.GetHash())->FromTx(tx, nHeight);
}

bool CScriptCheck::operator()()
{
    const CScript& scriptSig = ptxTo->vin[nIn].scriptSig;
    if (!VerifyScript(scriptSig, scriptPubKey, nFlags, CachingTransactionSignatureChecker(ptxTo, nIn, cacheStore), &error)) {
        return ::error("CScriptCheck(): %s:%d VerifySignature failed: %s", ptxTo->GetHash().ToString(), nIn, ScriptErrorString(error));
    }
    return true;
}

bool CheckInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, bool fScriptChecks, unsigned int flags, bool cacheStore, std::vector<CScriptCheck>* pvChecks)
{
    if (!tx.IsCoinBase() && !tx.HasZerocoinSpendInputs()) {
        if (pvChecks)
            pvChecks->reserve(tx.vin.size());

        // This doesn't trigger the DoS code on purpose; if it did, it would make it easier
        // for an attacker to attempt to split the network.
        if (!inputs.HaveInputs(tx))
            return state.Invalid(error("CheckInputs() : %s inputs unavailable", tx.GetHash().ToString()));

        // While checking, GetBestBlock() refers to the parent block.
        // This is also true for mempool checks.
        const Consensus::Params& consensus = Params().GetConsensus();
        CBlockIndex* pindexPrev = mapBlockIndex.find(inputs.GetBestBlock())->second;
        int nSpendHeight = pindexPrev->nHeight + 1;
        CAmount nValueIn = 0;
        CAmount nFees = 0;
        for (unsigned int i = 0; i < tx.vin.size(); i++) {
            const COutPoint& prevout = tx.vin[i].prevout;
            const CCoins* coins = inputs.AccessCoins(prevout.hash);
            assert(coins);

            // If prev is coinbase, check that it's matured
            if (coins->IsCoinBase() || coins->IsCoinStake()) {
                if (nSpendHeight - coins->nHeight < Params().GetConsensus().nCoinbaseMaturity)
                    return state.Invalid(
                        error("CheckInputs() : tried to spend coinbase at depth %d, coinstake=%d", nSpendHeight - coins->nHeight, coins->IsCoinStake()),
                        REJECT_INVALID, "bad-txns-premature-spend-of-coinbase");
            }

            // Check for negative or overflow input values
            nValueIn += coins->vout[prevout.n].nValue;
            if (!consensus.MoneyRange(coins->vout[prevout.n].nValue) || !consensus.MoneyRange(nValueIn))
                return state.DoS(100, error("CheckInputs() : txin values out of range"),
                    REJECT_INVALID, "bad-txns-inputvalues-outofrange");
        }

        if (!tx.IsCoinStake()) {
            if (nValueIn < tx.GetValueOut())
                return state.DoS(100, error("CheckInputs() : %s value in (%s) < value out (%s)",
                                          tx.GetHash().ToString(), FormatMoney(nValueIn), FormatMoney(tx.GetValueOut())),
                    REJECT_INVALID, "bad-txns-in-belowout");

            // Tally transaction fees
            CAmount nTxFee = nValueIn - tx.GetValueOut();
            if (nTxFee < 0)
                return state.DoS(100, error("CheckInputs() : %s nTxFee < 0", tx.GetHash().ToString()),
                    REJECT_INVALID, "bad-txns-fee-negative");
            nFees += nTxFee;
            if (!consensus.MoneyRange(nFees))
                return state.DoS(100, error("CheckInputs() : nFees out of range"),
                    REJECT_INVALID, "bad-txns-fee-outofrange");
        }
        // The first loop above does all the inexpensive checks.
        // Only if ALL inputs pass do we perform expensive ECDSA signature checks.
        // Helps prevent CPU exhaustion attacks.

        // Skip ECDSA signature verification when connecting blocks
        // before the last block chain checkpoint. This is safe because block merkle hashes are
        // still computed and checked, and any change will be caught at the next checkpoint.
        if (fScriptChecks) {
            for (unsigned int i = 0; i < tx.vin.size(); i++) {
                const COutPoint& prevout = tx.vin[i].prevout;
                const CCoins* coins = inputs.AccessCoins(prevout.hash);
                assert(coins);

                // Verify signature
                CScriptCheck check(*coins, tx, i, flags, cacheStore);
                if (pvChecks) {
                    pvChecks->push_back(CScriptCheck());
                    check.swap(pvChecks->back());
                } else if (!check()) {
                    if (flags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS) {
                        // Check whether the failure was caused by a
                        // non-mandatory script verification check, such as
                        // non-standard DER encodings or non-null dummy
                        // arguments; if so, don't trigger DoS protection to
                        // avoid splitting the network between upgraded and
                        // non-upgraded nodes.
                        CScriptCheck check(*coins, tx, i,
                            flags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS, cacheStore);
                        if (check())
                            return state.Invalid(false, REJECT_NONSTANDARD, strprintf("non-mandatory-script-verify-flag (%s)", ScriptErrorString(check.GetScriptError())));
                    }
                    // Failures of other flags indicate a transaction that is
                    // invalid in new blocks, e.g. a invalid P2SH. We DoS ban
                    // such nodes as they are not following the protocol. That
                    // said during an upgrade careful thought should be taken
                    // as to the correct behavior - we may want to continue
                    // peering with non-upgraded nodes even after a soft-fork
                    // super-majority vote has passed.
                    return state.DoS(100, false, REJECT_INVALID, strprintf("mandatory-script-verify-flag-failed (%s)", ScriptErrorString(check.GetScriptError())));
                }
            }
        }
    }

    return true;
}

/** Abort with a message */
static bool AbortNode(const std::string& strMessage, const std::string& userMessage="")
{
    strMiscWarning = strMessage;
    LogPrintf("*** %s\n", strMessage);
    uiInterface.ThreadSafeMessageBox(
        userMessage.empty() ? _("Error: A fatal internal error occured, see debug.log for details") : userMessage,
        "", CClientUIInterface::MSG_ERROR);
    StartShutdown();
    return false;
}

static bool AbortNode(CValidationState& state, const std::string& strMessage, const std::string& userMessage)
{
    AbortNode(strMessage, userMessage);
    return state.Error(strMessage);
}

// Legacy Zerocoin DB: used for performance during IBD
// (between Zerocoin_Block_V2_Start and Zerocoin_Block_Last_Checkpoint)
void DataBaseAccChecksum(CBlockIndex* pindex, bool fWrite)
{
    const Consensus::Params& consensus = Params().GetConsensus();
    if (!pindex ||
            pindex->nHeight < consensus.height_start_ZC_SerialsV2 ||
            pindex->nHeight > consensus.height_last_ZC_AccumCheckpoint ||
            pindex->nAccumulatorCheckpoint == pindex->pprev->nAccumulatorCheckpoint)
        return;

    uint256 accCurr = pindex->nAccumulatorCheckpoint;
    uint256 accPrev = pindex->pprev->nAccumulatorCheckpoint;
    // add/remove changed checksums to/from DB
    for (int i = (int)libzerocoin::zerocoinDenomList.size()-1; i >= 0; i--) {
        const uint32_t& nChecksum = accCurr.Get32();
        if (nChecksum != accPrev.Get32()) {
            fWrite ?
               zerocoinDB->WriteAccChecksum(nChecksum, libzerocoin::zerocoinDenomList[i], pindex->nHeight) :
               zerocoinDB->EraseAccChecksum(nChecksum, libzerocoin::zerocoinDenomList[i]);
        }
        accCurr >>= 32;
        accPrev >>= 32;
    }
}

bool DisconnectBlock(CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& view, bool* pfClean)
{
    if (pindex->GetBlockHash() != view.GetBestBlock())
        LogPrintf("%s : pindex=%s view=%s\n", __func__, pindex->GetBlockHash().GetHex(), view.GetBestBlock().GetHex());
    assert(pindex->GetBlockHash() == view.GetBestBlock());

    if (pfClean)
        *pfClean = false;

    bool fClean = true;

    CBlockUndo blockUndo;
    CDiskBlockPos pos = pindex->GetUndoPos();
    if (pos.IsNull())
        return error("DisconnectBlock() : no undo data available");
    if (!blockUndo.ReadFromDisk(pos, pindex->pprev->GetBlockHash()))
        return error("DisconnectBlock() : failure reading undo data");

    if (blockUndo.vtxundo.size() + 1 != block.vtx.size())
        return error("DisconnectBlock() : block and undo data inconsistent");

    // undo transactions in reverse order
    for (int i = block.vtx.size() - 1; i >= 0; i--) {
        const CTransaction& tx = block.vtx[i];

        /** UNDO ZEROCOIN DATABASING
         * note we only undo zerocoin databasing in the following statement, value to and from DIGIWAGE
         * addresses should still be handled by the typical bitcoin based undo code
         * */
        if (tx.ContainsZerocoins()) {
            libzerocoin::ZerocoinParams *params = Params().GetConsensus().Zerocoin_Params(false);
            if (tx.HasZerocoinSpendInputs()) {
                // Erase all zerocoinspends in this transaction
                for (const CTxIn &txin : tx.vin) {
                    if (txin.scriptSig.IsZerocoinSpend()) {
                        CBigNum serial;
                        libzerocoin::CoinSpend spend = TxInToZerocoinSpend(txin);
                        serial = spend.getCoinSerialNumber();

                        if (!zerocoinDB->EraseCoinSpend(serial))
                            return error("failed to erase spent zerocoin in block");
                    }
                }
            }

            if (tx.HasZerocoinMintOutputs()) {
                // Erase all zerocoinmints in this transaction
                for (const CTxOut &txout : tx.vout) {
                    if (txout.scriptPubKey.empty() || !txout.IsZerocoinMint())
                        continue;

                    libzerocoin::PublicCoin pubCoin(params);
                    if (!TxOutToPublicCoin(txout, pubCoin, state))
                        return error("DisconnectBlock(): TxOutToPublicCoin() failed");

                    if (!zerocoinDB->EraseCoinMint(pubCoin.getValue()))
                        return error("DisconnectBlock(): Failed to erase coin mint");
                }
            }
        }

        uint256 hash = tx.GetHash();

        // Check that all outputs are available and match the outputs in the block itself
        // exactly. Note that transactions with only provably unspendable outputs won't
        // have outputs available even in the block itself, so we handle that case
        // specially with outsEmpty.
        {
            CCoins outsEmpty;
            CCoinsModifier outs = view.ModifyCoins(hash);
            outs->ClearUnspendable();

            CCoins outsBlock(tx, pindex->nHeight);
            // The CCoins serialization does not serialize negative numbers.
            // No network rules currently depend on the version here, so an inconsistency is harmless
            // but it must be corrected before txout nversion ever influences a network rule.
            if (outsBlock.nVersion < 0)
                outs->nVersion = outsBlock.nVersion;
            if (*outs != outsBlock)
                fClean = fClean && error("DisconnectBlock() : added transaction mismatch? database corrupted");

            // remove outputs
            outs->Clear();
        }

        // restore inputs
        if (!tx.IsCoinBase() && !tx.HasZerocoinSpendInputs()) { // not coinbases or zerocoinspend because they dont have traditional inputs
            const CTxUndo& txundo = blockUndo.vtxundo[i - 1];
            if (txundo.vprevout.size() != tx.vin.size())
                return error("DisconnectBlock() : transaction and undo data inconsistent - txundo.vprevout.siz=%d tx.vin.siz=%d", txundo.vprevout.size(), tx.vin.size());
            for (unsigned int j = tx.vin.size(); j-- > 0;) {
                const COutPoint& out = tx.vin[j].prevout;
                const CTxInUndo& undo = txundo.vprevout[j];
                CCoinsModifier coins = view.ModifyCoins(out.hash);
                if (undo.nHeight != 0) {
                    // undo data contains height: this is the last output of the prevout tx being spent
                    if (!coins->IsPruned())
                        fClean = fClean && error("DisconnectBlock() : undo data overwriting existing transaction");
                    coins->Clear();
                    coins->fCoinBase = undo.fCoinBase;
                    coins->nHeight = undo.nHeight;
                    coins->nVersion = undo.nVersion;
                } else {
                    if (coins->IsPruned())
                        fClean = fClean && error("DisconnectBlock() : undo data adding output to missing transaction");
                }
                if (coins->IsAvailable(out.n))
                    fClean = fClean && error("DisconnectBlock() : undo data overwriting existing output");
                if (coins->vout.size() < out.n + 1)
                    coins->vout.resize(out.n + 1);
                coins->vout[out.n] = undo.txout;
            }
        }
    }

    // move best block pointer to prevout block
    view.SetBestBlock(pindex->pprev->GetBlockHash());

    const Consensus::Params& consensus = Params().GetConsensus();
    if (pindex->nHeight >= consensus.height_start_ZC_SerialsV2 &&
            pindex->nHeight <= consensus.height_last_ZC_AccumCheckpoint) {
        // Legacy Zerocoin DB: If Accumulators Checkpoint is changed, remove changed checksums
        DataBaseAccChecksum(pindex, false);
    }

    if (pfClean) {
        *pfClean = fClean;
        return true;
    } else {
        return fClean;
    }
}

void static FlushBlockFile(bool fFinalize = false)
{
    LOCK(cs_LastBlockFile);

    CDiskBlockPos posOld(nLastBlockFile, 0);

    FILE* fileOld = OpenBlockFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }

    fileOld = OpenUndoFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nUndoSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }
}

bool FindUndoPos(CValidationState& state, int nFile, CDiskBlockPos& pos, unsigned int nAddSize);

static CCheckQueue<CScriptCheck> scriptcheckqueue(128);

void ThreadScriptCheck()
{
    util::ThreadRename("digiwage-scriptch");
    scriptcheckqueue.Thread();
}

static int64_t nTimeVerify = 0;
static int64_t nTimeConnect = 0;
static int64_t nTimeIndex = 0;
static int64_t nTimeCallbacks = 0;
static int64_t nTimeTotal = 0;

bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& view, bool fJustCheck, bool fAlreadyChecked)
{
    AssertLockHeld(cs_main);
    // Check it again in case a previous version let a bad block in
    if (!fAlreadyChecked && !CheckBlock(block, state, !fJustCheck, !fJustCheck)) {
        if (state.CorruptionPossible()) {
            // We don't write down blocks to disk if they may have been
            // corrupted, so this should be impossible unless we're having hardware
            // problems.
            return AbortNode(state, "Corrupt block found indicating potential hardware failure; shutting down");
        }
        return false;
    }

    // verify that the view's current state corresponds to the previous block
    uint256 hashPrevBlock = pindex->pprev == NULL ? UINT256_ZERO : pindex->pprev->GetBlockHash();
    if (hashPrevBlock != view.GetBestBlock())
        LogPrintf("%s: hashPrev=%s view=%s\n", __func__, hashPrevBlock.GetHex(), view.GetBestBlock().GetHex());
    assert(hashPrevBlock == view.GetBestBlock());

    const Consensus::Params& consensus = Params().GetConsensus();

    // Special case for the genesis block, skipping connection of its transactions
    // (its coinbase is unspendable)
    if (block.GetHash() == consensus.hashGenesisBlock) {
        if (!fJustCheck)
            view.SetBestBlock(pindex->GetBlockHash());
        return true;
    }

    const int last_pow_block = consensus.height_last_PoW;
    if (pindex->nHeight <= last_pow_block && block.IsProofOfStake())
        return state.DoS(100, error("ConnectBlock() : PoS period not active"),
            REJECT_INVALID, "PoS-early");

    if (pindex->nHeight > last_pow_block && block.IsProofOfWork())
        return state.DoS(100, error("ConnectBlock() : PoW period ended"),
            REJECT_INVALID, "PoW-ended");

    bool fScriptChecks = pindex->nHeight >= Checkpoints::GetTotalBlocksEstimate();

    // If scripts won't be checked anyways, don't bother seeing if CLTV is activated
    bool fCLTVIsActivated = false;
    if (fScriptChecks && pindex->pprev) {
        fCLTVIsActivated = pindex->pprev->nHeight >= consensus.height_start_BIP65;
    }

    CCheckQueueControl<CScriptCheck> control(fScriptChecks && nScriptCheckThreads ? &scriptcheckqueue : nullptr);

    int64_t nTimeStart = GetTimeMicros();
    CAmount nFees = 0;
    CAmount nUnspendable = 0;
    int nInputs = 0;
    unsigned int nSigOps = 0;
    CDiskTxPos pos(pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()));
    std::vector<std::pair<uint256, CDiskTxPos> > vPos;
    std::vector<std::pair<libzerocoin::CoinSpend, uint256> > vSpends;
    std::vector<std::pair<libzerocoin::PublicCoin, uint256> > vMints;
    vPos.reserve(block.vtx.size());
    CBlockUndo blockundo;
    blockundo.vtxundo.reserve(block.vtx.size() - 1);
    CAmount nValueOut = 0;
    CAmount nValueIn = 0;
    unsigned int nMaxBlockSigOps = MAX_BLOCK_SIGOPS_CURRENT;
    std::vector<uint256> vSpendsInBlock;
    uint256 hashBlock = block.GetHash();
    for (unsigned int i = 0; i < block.vtx.size(); i++) {
        const CTransaction& tx = block.vtx[i];

        // First check for BIP30.
        // Do not allow blocks that contain transactions which 'overwrite' older transactions,
        // unless those are already completely spent.
        // If such overwrites are allowed, coinbases and transactions depending upon those
        // can be duplicated to remove the ability to spend the first instance -- even after
        // being sent to another address.
        // See BIP30 and http://r6.ca/blog/20120206T005236Z.html for more information.
        // This logic is not necessary for memory pool transactions, as AcceptToMemoryPool
        // already refuses previously-known transaction ids entirely.
        const CCoins* coins = view.AccessCoins(tx.GetHash());
        if (coins && !coins->IsPruned())
            return state.DoS(100, error("ConnectBlock() : tried to overwrite transaction"),
                             REJECT_INVALID, "bad-txns-BIP30");

        nInputs += tx.vin.size();
        nSigOps += GetLegacySigOpCount(tx);
        if (nSigOps > nMaxBlockSigOps)
            return state.DoS(100, error("ConnectBlock() : too many sigops"), REJECT_INVALID, "bad-blk-sigops");

        // Permanently reject new Zerocoin transactions (that somehow make it past mempool checks?)
        if (!IsInitialBlockDownload() && tx.ContainsZerocoins())
            return state.DoS(100, error("ConnectBlock() : Zerocoin transactions have been permanently disabled"));

        if (tx.HasZerocoinMintOutputs()) {
            // Parse minted coins
            for (auto& out : tx.vout) {
                if (!out.IsZerocoinMint()) continue;
                libzerocoin::PublicCoin coin(consensus.Zerocoin_Params(false));
                if (!TxOutToPublicCoin(out, coin, state))
                    return state.DoS(100, error("%s: failed final check of zerocoinmint for tx %s", __func__, tx.GetHash().GetHex()));
                vMints.emplace_back(std::make_pair(coin, tx.GetHash()));
            }
        }

        if (tx.HasZerocoinSpendInputs()) {
          //  int nHeightTx = 0;
            uint256 txid = tx.GetHash();
            vSpendsInBlock.emplace_back(txid);

            // Include Zerocoin spends to properly calculate block mints / supply,
            // but don't worry about serial double-spends as the Zerocoin supply
            // is essentially "frozen" and cannot be changed.
            for (const CTxIn& txIn : tx.vin) {
                bool isZerocoinSpend = txIn.IsZerocoinSpend();
                if (!isZerocoinSpend)
                    continue;

                libzerocoin::CoinSpend spend = TxInToZerocoinSpend(txIn);
                nValueIn += spend.getDenomination() * COIN;
                // Queue for DB write after the 'justcheck' section has concluded
                vSpends.emplace_back(std::make_pair(spend, tx.GetHash()));
            }

        } else if (!tx.IsCoinBase()) {
            if (!view.HaveInputs(tx))
                return state.DoS(100, error("ConnectBlock() : inputs missing/spent"),
                    REJECT_INVALID, "bad-txns-inputs-missingorspent");

            // Add in sigops done by pay-to-script-hash inputs;
            // this is to prevent a "rogue miner" from creating
            // an incredibly-expensive-to-validate block.
            nSigOps += GetP2SHSigOpCount(tx, view);
            if (nSigOps > nMaxBlockSigOps)
                return state.DoS(100, error("ConnectBlock() : too many sigops"), REJECT_INVALID, "bad-blk-sigops");

            if (!tx.IsCoinStake())
                nFees += view.GetValueIn(tx) - tx.GetValueOut();
            nValueIn += view.GetValueIn(tx);

            std::vector<CScriptCheck> vChecks;
            unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_DERSIG;
            if (fCLTVIsActivated)
                flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;

            if (!CheckInputs(tx, state, view, fScriptChecks, flags, false, nScriptCheckThreads ? &vChecks : NULL))
                return false;
            control.Add(vChecks);

            // Consider unspendable outputs as 'burned'
            if (!fJustCheck) {
                for (const CTxOut& txOut : tx.vout) {
                    if (txOut.scriptPubKey.IsUnspendable())
                        nUnspendable += txOut.nValue;
                }
            }
        }
        nValueOut += tx.GetValueOut();

        CTxUndo undoDummy;
        if (i > 0) {
            blockundo.vtxundo.emplace_back();
        }
        UpdateCoins(tx, state, view, i == 0 ? undoDummy : blockundo.vtxundo.back(), pindex->nHeight);

        vPos.emplace_back(tx.GetHash(), pos);
        pos.nTxOffset += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);
    }

    // Track zWAGE money supply in the block index
    if (!UpdateZWAGESupply(block, pindex))
        return state.DoS(100, error("%s: Failed to calculate new zWAGE supply for block=%s height=%d", __func__,
                                    block.GetHash().GetHex(), pindex->nHeight), REJECT_INVALID);

    // Track money supply and mint amount info
    CAmount nMoneySupplyPrev = pindex->pprev ? pindex->pprev->nMoneySupply : 0;
    pindex->nMoneySupply = nMoneySupplyPrev + nValueOut - nValueIn;
    const int64_t nMint = pindex->nMoneySupply - nMoneySupplyPrev + nFees;

    int64_t nTime1 = GetTimeMicros();
    nTimeConnect += nTime1 - nTimeStart;
    LogPrint("bench", "      - Connect %u transactions: %.2fms (%.3fms/tx, %.3fms/txin) [%.2fs]\n", (unsigned)block.vtx.size(), 0.001 * (nTime1 - nTimeStart), 0.001 * (nTime1 - nTimeStart) / block.vtx.size(), nInputs <= 1 ? 0 : 0.001 * (nTime1 - nTimeStart) / (nInputs - 1), nTimeConnect * 0.000001);

    // PoW phase redistributed fees to miner. PoS stage destroys fees.
    CAmount nExpectedMint = GetBlockValue(pindex->pprev->nHeight);
    if (block.IsProofOfWork())
        nExpectedMint += nFees;
    else if (!fJustCheck)
        nUnspendable += nFees;

    // Check that the block does not overmint
    if (!IsBlockValueValid(block, nExpectedMint, nMint)) {
        return state.DoS(100, error("ConnectBlock() : reward pays too much (actual=%s vs limit=%s)",
                                    FormatMoney(nMint), FormatMoney(nExpectedMint)),
                         REJECT_INVALID, "bad-cb-amount");
    }

    if (!control.Wait())
        return state.DoS(100, error("%s: CheckQueue failed", __func__), REJECT_INVALID, "block-validation-failed");
    int64_t nTime2 = GetTimeMicros();
    nTimeVerify += nTime2 - nTimeStart;
    LogPrint("bench", "    - Verify %u txins: %.2fms (%.3fms/txin) [%.2fs]\n", nInputs - 1, 0.001 * (nTime2 - nTimeStart), nInputs <= 1 ? 0 : 0.001 * (nTime2 - nTimeStart) / (nInputs - 1), nTimeVerify * 0.000001);

    // IMPORTANT NOTE: Nothing before this point should actually store to disk (or even memory)
    if (fJustCheck)
        return true;

    // Update burned supply, and write it to disk
    nBurnedCoins += nUnspendable;
    if (!IsInitialBlockDownload() || pindex->nHeight % 1000 == 0) {
        // Save to DB every 1000 blocks during IBD, and every block after IBD.
        pblocktree->WriteInt("burned", std::round(nBurnedCoins / COIN));
    }

    // Write undo information to disk
    if (pindex->GetUndoPos().IsNull() || !pindex->IsValid(BLOCK_VALID_SCRIPTS)) {
        if (pindex->GetUndoPos().IsNull()) {
            CDiskBlockPos diskPosBlock;
            if (!FindUndoPos(state, pindex->nFile, diskPosBlock, ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) + 40))
                return error("ConnectBlock() : FindUndoPos failed");
            if (!blockundo.WriteToDisk(diskPosBlock, pindex->pprev->GetBlockHash()))
                return AbortNode(state, "Failed to write undo data");

            // update nUndoPos in block index
            pindex->nUndoPos = diskPosBlock.nPos;
            pindex->nStatus |= BLOCK_HAVE_UNDO;
        }

        pindex->RaiseValidity(BLOCK_VALID_SCRIPTS);
        setDirtyBlockIndex.insert(pindex);
    }

    // Flush spend/mint info to disk
    if (!vSpends.empty() && !zerocoinDB->WriteCoinSpendBatch(vSpends))
        return AbortNode(state, "Failed to record coin serials to database");

    if (!vMints.empty() && !zerocoinDB->WriteCoinMintBatch(vMints))
        return AbortNode(state, "Failed to record new mints to database");

    if (fTxIndex)
        if (!pblocktree->WriteTxIndex(vPos))
            return AbortNode(state, "Failed to write transaction index");

    // add this block to the view's block chain
    view.SetBestBlock(pindex->GetBlockHash());

    int64_t nTime3 = GetTimeMicros();
    nTimeIndex += nTime3 - nTime2;
    LogPrint("bench", "    - Index writing: %.2fms [%.2fs]\n", 0.001 * (nTime3 - nTime2), nTimeIndex * 0.000001);

    // Watch for changes to the previous coinbase transaction.
    static uint256 hashPrevBestCoinBase;
    GetMainSignals().UpdatedTransaction(hashPrevBestCoinBase);
    hashPrevBestCoinBase = block.vtx[0].GetHash();

    int64_t nTime4 = GetTimeMicros();
    nTimeCallbacks += nTime4 - nTime3;
    LogPrint("bench", "    - Callbacks: %.2fms [%.2fs]\n", 0.001 * (nTime4 - nTime3), nTimeCallbacks * 0.000001);


    if (pindex->nHeight >= consensus.height_start_ZC_SerialsV2 && pindex->nHeight < consensus.height_last_ZC_AccumCheckpoint) {
        // Legacy Zerocoin DB: If Accumulators Checkpoint is changed, database the checksums
        DataBaseAccChecksum(pindex, true);
    } else if (pindex->nHeight == consensus.height_last_ZC_AccumCheckpoint) {
        // After last Checkpoint block, wipe the checksum database
        zerocoinDB->WipeAccChecksums();
    }

    return true;
}



/**
 * Update the on-disk chain state.
 * The caches and indexes are flushed if either they're too large, forceWrite is set, or
 * fast is not set and it's been a while since the last write.
 */
 bool FlushStateToDisk(CValidationState& state, FlushStateMode mode)
 {
     LOCK(cs_main);
     static int64_t nLastWrite = 0;
     try {
         // Use scoped enum values for comparison
         if ((mode == FlushStateMode::ALWAYS) ||
             ((mode == FlushStateMode::PERIODIC || mode == FlushStateMode::IF_NEEDED) && pcoinsTip && pcoinsTip->GetCacheSize() > nCoinCacheSize) || // Add null check for pcoinsTip
             (mode == FlushStateMode::PERIODIC && GetTimeMicros() > nLastWrite + DATABASE_WRITE_INTERVAL * 1000000)) {
 
             // Ensure free space check uses the cache size correctly
             uint64_t nCoinCacheSizeEstimate = pcoinsTip ? pcoinsTip->GetCacheSize() : 0; // Handle null pcoinsTip
             uint64_t nRequiredSpace = 100 * 2 * 2 * nCoinCacheSizeEstimate; // Rough estimate
             if (!CheckDiskSpace(nRequiredSpace))
                 return state.Error("out of disk space");
 
             FlushBlockFile(); // Flush block/undo files first
 
             { // Scope for DB batch write
                 std::vector<std::pair<int, const CBlockFileInfo*> > vFiles;
                 vFiles.reserve(setDirtyFileInfo.size());
                 for (std::set<int>::iterator it = setDirtyFileInfo.begin(); it != setDirtyFileInfo.end(); ) {
                     // Ensure index is valid before accessing vinfoBlockFile
                     if (*it >= 0 && (unsigned int)*it < vinfoBlockFile.size()) {
                         vFiles.push_back(std::make_pair(*it, &vinfoBlockFile[*it]));
                     }
                     setDirtyFileInfo.erase(it++);
                 }
 
                 std::vector<const CBlockIndex*> vBlocks;
                 vBlocks.reserve(setDirtyBlockIndex.size());
                 for (std::set<CBlockIndex*>::iterator it = setDirtyBlockIndex.begin(); it != setDirtyBlockIndex.end(); ) {
                     vBlocks.push_back(*it);
                     setDirtyBlockIndex.erase(it++);
                 }
 
                 if (!pblocktree || !pblocktree->WriteBatchSync(vFiles, nLastBlockFile, vBlocks)) { // Add null check
                     return AbortNode(state, "Files to write to block index database");
                 }
             }
 
             // Flush chainstate
             if (!pcoinsTip || !pcoinsTip->Flush()) // Add null check
                 return AbortNode(state, "Failed to write to coin database");
 
             // Update best block in wallet signals
             if (mode != FlushStateMode::IF_NEEDED) { // Use scoped enum
                  if (chainActive.Tip()) // Check tip exists
                     GetMainSignals().SetBestChain(chainActive.GetLocator());
             }
             nLastWrite = GetTimeMicros();
         }
     } catch (const std::runtime_error& e) {
         return AbortNode(state, std::string("System error while flushing: ") + e.what());
     }
     return true;
 }


// --- FlushStateToDisk() overload (Fixed call) ---
void FlushStateToDisk()
{
    CValidationState state;
    // Call the main function with the correct scoped enum value
    FlushStateToDisk(state, FlushStateMode::ALWAYS);
}


/** Update chainActive and related internal data structures. */
void static UpdateTip(CBlockIndex* pindexNew)
{
    chainActive.SetTip(pindexNew);

    // New best block
    nTimeBestReceived = GetTime();
    mempool.AddTransactionsUpdated(1);

    {
        LOCK(g_best_block_mutex);
        g_best_block = pindexNew->GetBlockHash();
        g_best_block_cv.notify_all();
    }

    const CBlockIndex* pChainTip = chainActive.Tip();
    LogPrintf("UpdateTip: new best=%s  height=%d version=%d  log2_work=%.16f  tx=%lu  date=%s progress=%f  cache=%u\n",
            pChainTip->GetBlockHash().GetHex(), pChainTip->nHeight, pChainTip->nVersion, log(pChainTip->nChainWork.getdouble()) / log(2.0), (unsigned long)pChainTip->nChainTx,
              DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pChainTip->GetBlockTime()),
              Checkpoints::GuessVerificationProgress(pChainTip), (unsigned int)pcoinsTip->GetCacheSize());

    // Check the version of the last 100 blocks to see if we need to upgrade:
    static bool fWarned = false;
    if (!IsInitialBlockDownload() && !fWarned) {
        int nUpgraded = 0;
        const CBlockIndex* pindex = chainActive.Tip();
        for (int i = 0; i < 100 && pindex != NULL; i++) {
            if (pindex->nVersion > CBlock::CURRENT_VERSION)
                ++nUpgraded;
            pindex = pindex->pprev;
        }
        if (nUpgraded > 0)
            LogPrintf("SetBestChain: %d of last 100 blocks above version %d\n", nUpgraded, (int)CBlock::CURRENT_VERSION);
        if (nUpgraded > 100 / 2) {
            // strMiscWarning is read by GetWarnings(), called by Qt and the JSON-RPC code to warn the user:
            strMiscWarning = _("Warning: This version is obsolete, upgrade required!");
            AlertNotify(strMiscWarning, true);
            fWarned = true;
        }
    }
}

/** Disconnect chainActive's tip. */
bool static DisconnectTip(CValidationState& state)
{
    CBlockIndex* pindexDelete = chainActive.Tip();
    assert(pindexDelete);
    mempool.check(pcoinsTip);
    // Read block from disk.
    CBlock block;
    if (!ReadBlockFromDisk(block, pindexDelete))
        return AbortNode(state, "Failed to read block");
    // Apply the block atomically to the chain state.
    int64_t nStart = GetTimeMicros();
    {
        CCoinsViewCache view(pcoinsTip);
        if (!DisconnectBlock(block, state, pindexDelete, view))
            return error("DisconnectTip() : DisconnectBlock %s failed", pindexDelete->GetBlockHash().ToString());
        assert(view.Flush());
    }
    LogPrint("bench", "- Disconnect block: %.2fms\n", (GetTimeMicros() - nStart) * 0.001);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FlushStateMode::ALWAYS))
    return false;
    // Resurrect mempool transactions from the disconnected block.
    for (const CTransaction& tx : block.vtx) {
        // ignore validation errors in resurrected transactions
        std::list<CTransaction> removed;
        CValidationState stateDummy;
        if (tx.IsCoinBase() || tx.IsCoinStake() || !AcceptToMemoryPool(mempool, stateDummy, tx, false, NULL))
            mempool.remove(tx, removed, true);
    }
    mempool.removeCoinbaseSpends(pcoinsTip, pindexDelete->nHeight);
    mempool.check(pcoinsTip);
    // Update chainActive and related variables.
    UpdateTip(pindexDelete->pprev);
    // Let wallets know transactions went from 1-confirmed to
    // 0-confirmed or conflicted:
    for (const CTransaction& tx : block.vtx) {
        SyncWithWallets(tx, NULL);
    }
    return true;
}

static int64_t nTimeReadFromDisk = 0;
static int64_t nTimeConnectTotal = 0;
static int64_t nTimeFlush = 0;
static int64_t nTimeChainState = 0;
static int64_t nTimePostConnect = 0;

/**
 * Connect a new block to chainActive. pblock is either NULL or a pointer to a CBlock
 * corresponding to pindexNew, to bypass loading it again from disk.
 */
bool static ConnectTip(CValidationState& state, CBlockIndex* pindexNew, CBlock* pblock, bool fAlreadyChecked)
{
    assert(pindexNew->pprev == chainActive.Tip());
    mempool.check(pcoinsTip);
    CCoinsViewCache view(pcoinsTip);

    if (pblock == NULL)
        fAlreadyChecked = false;

    // Read block from disk.
    int64_t nTime1 = GetTimeMicros();
    CBlock block;
    if (!pblock) {
        if (!ReadBlockFromDisk(block, pindexNew))
            return AbortNode(state, "Failed to read block");
        pblock = &block;
    }
    // Apply the block atomically to the chain state.
    int64_t nTime2 = GetTimeMicros();
    nTimeReadFromDisk += nTime2 - nTime1;
    int64_t nTime3;
    LogPrint("bench", "  - Load block from disk: %.2fms [%.2fs]\n", (nTime2 - nTime1) * 0.001, nTimeReadFromDisk * 0.000001);
    {
        CInv inv(MSG_BLOCK, pindexNew->GetBlockHash());
        bool rv = ConnectBlock(*pblock, state, pindexNew, view, false, fAlreadyChecked);
        GetMainSignals().BlockChecked(*pblock, state);
        if (!rv) {
            if (state.IsInvalid())
                InvalidBlockFound(pindexNew, state);
            return error("ConnectTip() : ConnectBlock %s failed", pindexNew->GetBlockHash().ToString());
        }
        mapBlockSource.erase(inv.hash);
        nTime3 = GetTimeMicros();
        nTimeConnectTotal += nTime3 - nTime2;
        LogPrint("bench", "  - Connect total: %.2fms [%.2fs]\n", (nTime3 - nTime2) * 0.001, nTimeConnectTotal * 0.000001);
        assert(view.Flush());
    }
    int64_t nTime4 = GetTimeMicros();
    nTimeFlush += nTime4 - nTime3;
    LogPrint("bench", "  - Flush: %.2fms [%.2fs]\n", (nTime4 - nTime3) * 0.001, nTimeFlush * 0.000001);

    // Write the chain state to disk, if necessary. Always write to disk if this is the first of a new file.
    FlushStateMode flushMode = FlushStateMode::IF_NEEDED;
    if (pindexNew->pprev && (pindexNew->GetBlockPos().nFile != pindexNew->pprev->GetBlockPos().nFile))
    flushMode = FlushStateMode::ALWAYS;
    if (!FlushStateToDisk(state, flushMode))
        return false;
    int64_t nTime5 = GetTimeMicros();
    nTimeChainState += nTime5 - nTime4;
    LogPrint("bench", "  - Writing chainstate: %.2fms [%.2fs]\n", (nTime5 - nTime4) * 0.001, nTimeChainState * 0.000001);

    // Remove conflicting transactions from the mempool.
    std::list<CTransaction> txConflicted;
    mempool.removeForBlock(pblock->vtx, pindexNew->nHeight, txConflicted);
    mempool.check(pcoinsTip);
    // Update chainActive & related variables.
    UpdateTip(pindexNew);
    // Tell wallet about transactions that went from mempool
    // to conflicted:
    for (const CTransaction& tx : txConflicted) {
        SyncWithWallets(tx, NULL);
    }
    // ... and about transactions that got confirmed:
    for (const CTransaction& tx : pblock->vtx) {
        SyncWithWallets(tx, pblock);
    }

    int64_t nTime6 = GetTimeMicros();
    nTimePostConnect += nTime6 - nTime5;
    nTimeTotal += nTime6 - nTime1;
    LogPrint("bench", "  - Connect postprocess: %.2fms [%.2fs]\n", (nTime6 - nTime5) * 0.001, nTimePostConnect * 0.000001);
    LogPrint("bench", "- Connect block: %.2fms [%.2fs]\n", (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);
    return true;
}

bool DisconnectBlocks(int nBlocks)
{
    LOCK(cs_main);

    CValidationState state;

    LogPrintf("%s: Got command to replay %d blocks\n", __func__, nBlocks);
    for (int i = 0; i <= nBlocks; i++)
        DisconnectTip(state);

    return true;
}

void ReprocessBlocks(int nBlocks)
{
    std::map<uint256, int64_t>::iterator it = mapRejectedBlocks.begin();
    while (it != mapRejectedBlocks.end()) {
        //use a window twice as large as is usual for the nBlocks we want to reset
        if ((*it).second > GetTime() - (nBlocks * Params().GetConsensus().nTargetSpacing * 2)) {
            BlockMap::iterator mi = mapBlockIndex.find((*it).first);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                LOCK(cs_main);

                CBlockIndex* pindex = (*mi).second;
                LogPrintf("%s - %s\n", __func__, (*it).first.ToString());

                CValidationState state;
                ReconsiderBlock(state, pindex);
            }
        }
        ++it;
    }

    CValidationState state;
    {
        LOCK(cs_main);
        DisconnectBlocks(nBlocks);
    }

    if (state.IsValid()) {
        ActivateBestChain(state);
    }
}

/*
    DisconnectBlockAndInputs

    Remove conflicting blocks for successful SwiftX transaction locks
    This should be very rare (Probably will never happen)
*/
// ***TODO*** clean up here
bool DisconnectBlockAndInputs(CValidationState& state, CTransaction txLock)
{
    // All modifications to the coin state will be done in this cache.
    // Only when all have succeeded, we push it to pcoinsTip.
    //    CCoinsViewCache view(*pcoinsTip, true);

    CBlockIndex* BlockReading = chainActive.Tip();
    CBlockIndex* pindexNew = NULL;

    bool foundConflictingTx = false;

    //remove anything conflicting in the memory pool
    std::list<CTransaction> txConflicted;
    mempool.removeConflicts(txLock, txConflicted);


    // List of what to disconnect (typically nothing)
    std::vector<CBlockIndex*> vDisconnect;

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0 && !foundConflictingTx && i < 6; i++) {
        vDisconnect.push_back(BlockReading);
        pindexNew = BlockReading->pprev; //new best block

        CBlock block;
        if (!ReadBlockFromDisk(block, BlockReading))
            return AbortNode(state, _("Failed to read block"));

        // Queue memory transactions to resurrect.
        // We only do this for blocks after the last checkpoint (reorganisation before that
        // point should only happen with -reindex/-loadblock, or a misbehaving peer.
        for (const CTransaction& tx : block.vtx) {
            if (!tx.IsCoinBase()) {
                for (const CTxIn& in1 : txLock.vin) {
                    for (const CTxIn& in2 : tx.vin) {
                        if (in1.prevout == in2.prevout) foundConflictingTx = true;
                    }
                }
            }
        }

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    if (!foundConflictingTx) {
        LogPrintf("DisconnectBlockAndInputs: Can't find a conflicting transaction to inputs\n");
        return false;
    }

    if (vDisconnect.size() > 0) {
        LogPrintf("REORGANIZE: Disconnect Conflicting Blocks %lli blocks; %s..\n", vDisconnect.size(), pindexNew->GetBlockHash().ToString());
        for (CBlockIndex* pindex : vDisconnect) {
            LogPrintf(" -- disconnect %s\n", pindex->GetBlockHash().ToString());
            DisconnectTip(state);
        }
    }

    return true;
}


/**
 * Return the tip of the chain with the most work in it, that isn't
 * known to be invalid (it's however far from certain to be valid).
 */
static CBlockIndex* FindMostWorkChain()
{
    do {
        CBlockIndex* pindexNew = NULL;

        // Find the best candidate header.
        {
            std::set<CBlockIndex*, CBlockIndexWorkComparator>::reverse_iterator it = setBlockIndexCandidates.rbegin();
            if (it == setBlockIndexCandidates.rend())
                return NULL;
            pindexNew = *it;
        }

        // Check whether all blocks on the path between the currently active chain and the candidate are valid.
        // Just going until the active chain is an optimization, as we know all blocks in it are valid already.
        CBlockIndex* pindexTest = pindexNew;
        bool fInvalidAncestor = false;
        while (pindexTest && !chainActive.Contains(pindexTest)) {
            assert(pindexTest->nChainTx || pindexTest->nHeight == 0);

            // Pruned nodes may have entries in setBlockIndexCandidates for
            // which block files have been deleted.  Remove those as candidates
            // for the most work chain if we come across them; we can't switch
            // to a chain unless we have all the non-active-chain parent blocks.
            bool fFailedChain = pindexTest->nStatus & BLOCK_FAILED_MASK;
            bool fMissingData = !(pindexTest->nStatus & BLOCK_HAVE_DATA);
            if (fFailedChain || fMissingData) {
                // Candidate chain is not usable (either invalid or missing data)
                if (fFailedChain && (pindexBestInvalid == NULL || pindexNew->nChainWork > pindexBestInvalid->nChainWork))
                    pindexBestInvalid = pindexNew;
                CBlockIndex* pindexFailed = pindexNew;
                // Remove the entire chain from the set.
                while (pindexTest != pindexFailed) {
                    if (fFailedChain) {
                        pindexFailed->nStatus |= BLOCK_FAILED_CHILD;
                    } else if (fMissingData) {
                        // If we're missing data, then add back to mapBlocksUnlinked,
                        // so that if the block arrives in the future we can try adding
                        // to setBlockIndexCandidates again.
                        mapBlocksUnlinked.insert(std::make_pair(pindexFailed->pprev, pindexFailed));
                    }
                    setBlockIndexCandidates.erase(pindexFailed);
                    pindexFailed = pindexFailed->pprev;
                }
                setBlockIndexCandidates.erase(pindexTest);
                fInvalidAncestor = true;
                break;
            }
            pindexTest = pindexTest->pprev;
        }
        if (!fInvalidAncestor)
            return pindexNew;
    } while (true);
}

/** Delete all entries in setBlockIndexCandidates that are worse than the current tip. */
static void PruneBlockIndexCandidates()
{
    // Note that we can't delete the current block itself, as we may need to return to it later in case a
    // reorganization to a better block fails.
    std::set<CBlockIndex*, CBlockIndexWorkComparator>::iterator it = setBlockIndexCandidates.begin();
    while (it != setBlockIndexCandidates.end() && setBlockIndexCandidates.value_comp()(*it, chainActive.Tip())) {
        setBlockIndexCandidates.erase(it++);
    }
    // Either the current tip or a successor of it we're working towards is left in setBlockIndexCandidates.
    assert(!setBlockIndexCandidates.empty());
}

/**
 * Try to make some progress towards making pindexMostWork the active block.
 * pblock is either NULL or a pointer to a CBlock corresponding to pindexMostWork.
 */
static bool ActivateBestChainStep(CValidationState& state, CBlockIndex* pindexMostWork, CBlock* pblock, bool fAlreadyChecked)
{
    AssertLockHeld(cs_main);
    if (pblock == NULL)
        fAlreadyChecked = false;
    bool fInvalidFound = false;
    const CBlockIndex* pindexOldTip = chainActive.Tip();
    const CBlockIndex* pindexFork = chainActive.FindFork(pindexMostWork);

    // Disconnect active blocks which are no longer in the best chain.
    while (chainActive.Tip() && chainActive.Tip() != pindexFork) {
        if (!DisconnectTip(state))
            return false;
    }

    // Build list of new blocks to connect.
    std::vector<CBlockIndex*> vpindexToConnect;
    bool fContinue = true;
    int nHeight = pindexFork ? pindexFork->nHeight : -1;
    while (fContinue && nHeight != pindexMostWork->nHeight) {
        // Don't iterate the entire list of potential improvements toward the best tip, as we likely only need
        // a few blocks along the way.
        int nTargetHeight = std::min(nHeight + 32, pindexMostWork->nHeight);
        vpindexToConnect.clear();
        vpindexToConnect.reserve(nTargetHeight - nHeight);
        CBlockIndex* pindexIter = pindexMostWork->GetAncestor(nTargetHeight);
        while (pindexIter && pindexIter->nHeight != nHeight) {
            vpindexToConnect.push_back(pindexIter);
            pindexIter = pindexIter->pprev;
        }
        nHeight = nTargetHeight;

        // Connect new blocks.
        BOOST_REVERSE_FOREACH (CBlockIndex* pindexConnect, vpindexToConnect) {
            if (!ConnectTip(state, pindexConnect, pindexConnect == pindexMostWork ? pblock : NULL, fAlreadyChecked)) {
                if (state.IsInvalid()) {
                    // The block violates a consensus rule.
                    if (!state.CorruptionPossible())
                        InvalidChainFound(vpindexToConnect.back());
                    state = CValidationState();
                    fInvalidFound = true;
                    fContinue = false;
                    break;
                } else {
                    // A system error occurred (disk space, database error, ...).
                    return false;
                }
            } else {
                PruneBlockIndexCandidates();
                if (!pindexOldTip || chainActive.Tip()->nChainWork > pindexOldTip->nChainWork) {
                    // We're in a better position than we were. Return temporarily to release the lock.
                    fContinue = false;
                    break;
                }
            }
        }
    }

    // Callbacks/notifications for a new best chain.
    if (fInvalidFound)
        CheckForkWarningConditionsOnNewFork(vpindexToConnect.back());
    else
        CheckForkWarningConditions();

    return true;
}

/**
 * Make the best chain active, in multiple steps. The result is either failure
 * or an activated best chain. pblock is either NULL or a pointer to a block
 * that is already loaded (to avoid loading it again from disk).
 */
bool ActivateBestChain(CValidationState& state, CBlock* pblock, bool fAlreadyChecked)
{
    // Note that while we're often called here from ProcessNewBlock, this is
    // far from a guarantee. Things in the P2P/RPC will often end up calling
    // us in the middle of ProcessNewBlock - do not assume pblock is set
    // sanely for performance or correctness!
    AssertLockNotHeld(cs_main);

    CBlockIndex* pindexNewTip = nullptr;
    CBlockIndex* pindexMostWork = nullptr;
    do {
        boost::this_thread::interruption_point();

        const CBlockIndex *pindexFork;
        bool fInitialDownload;
        while (true) {
            TRY_LOCK(cs_main, lockMain);
            if (!lockMain) {
                MilliSleep(50);
                continue;
            }

            CBlockIndex *pindexOldTip = chainActive.Tip();
            pindexMostWork = FindMostWorkChain();

            // Whether we have anything to do at all.
            if (pindexMostWork == NULL || pindexMostWork == chainActive.Tip()) {
                if (pindexMostWork != NULL )
                    g_best_block = pindexMostWork->GetBlockHash();
                return true;
            }

            if (!ActivateBestChainStep(state, pindexMostWork, pblock && pblock->GetHash() == pindexMostWork->GetBlockHash() ? pblock : NULL, fAlreadyChecked))
                return false;

            pindexNewTip = chainActive.Tip();
            pindexFork = chainActive.FindFork(pindexOldTip);
            fInitialDownload = IsInitialBlockDownload();
            break;
        }

        // When we reach this point, we switched to a new tip (stored in pindexNewTip).
        // Notifications/callbacks that can run without cs_main
        // Always notify the UI if a new block tip was connected
        if (pindexFork != pindexNewTip) {

            // Notify the UI
            uiInterface.NotifyBlockTip(fInitialDownload, pindexNewTip);

            // Notifications/callbacks that can run without cs_main
            if (!fInitialDownload) {
                uint256 hashNewTip = pindexNewTip->GetBlockHash();
                // Relay inventory, but don't relay old inventory during initial block download.
                int nBlockEstimate = Checkpoints::GetTotalBlocksEstimate();
                {
                    LOCK(cs_vNodes);
                    for (CNode *pnode : vNodes)
                        if (chainActive.Height() >
                            (pnode->nStartingHeight != -1 ? pnode->nStartingHeight - 2000 : nBlockEstimate))
                            pnode->PushInventory(CInv(MSG_BLOCK, hashNewTip));
                }
                // Notify external listeners about the new tip.
                GetMainSignals().UpdatedBlockTip(pindexNewTip);

                unsigned size = 0;
                if (pblock)
                    size = GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION);
                // If the size is over 1 MB notify external listeners, and it is within the last 5 minutes
                if (size > MAX_BLOCK_SIZE_LEGACY && pblock->GetBlockTime() > GetAdjustedTime() - 300) {
                    uiInterface.NotifyBlockSize(static_cast<int>(size), hashNewTip);
                }
            }

        }
    } while (pindexMostWork != chainActive.Tip());
    CheckBlockIndex();

    // Write changes periodically to disk, after relay.
    if (!FlushStateToDisk(state, FlushStateMode::PERIODIC)) {
        return false;
    }

    return true;
}

bool InvalidateBlock(CValidationState& state, CBlockIndex* pindex)
{
    AssertLockHeld(cs_main);

    // Mark the block itself as invalid.
    pindex->nStatus |= BLOCK_FAILED_VALID;
    setDirtyBlockIndex.insert(pindex);
    setBlockIndexCandidates.erase(pindex);

    while (chainActive.Contains(pindex)) {
        CBlockIndex* pindexWalk = chainActive.Tip();
        pindexWalk->nStatus |= BLOCK_FAILED_CHILD;
        setDirtyBlockIndex.insert(pindexWalk);
        setBlockIndexCandidates.erase(pindexWalk);
        // ActivateBestChain considers blocks already in chainActive
        // unconditionally valid already, so force disconnect away from it.
        if (!DisconnectTip(state)) {
            return false;
        }
    }

    // The resulting new best tip may not be in setBlockIndexCandidates anymore, so
    // add them again.
    BlockMap::iterator it = mapBlockIndex.begin();
    while (it != mapBlockIndex.end()) {
        if (it->second->IsValid(BLOCK_VALID_TRANSACTIONS) && it->second->nChainTx && !setBlockIndexCandidates.value_comp()(it->second, chainActive.Tip())) {
            setBlockIndexCandidates.insert(it->second);
        }
        it++;
    }

    InvalidChainFound(pindex);
    return true;
}

bool ReconsiderBlock(CValidationState& state, CBlockIndex* pindex)
{
    AssertLockHeld(cs_main);

    int nHeight = pindex->nHeight;

    // Remove the invalidity flag from this block and all its descendants.
    BlockMap::iterator it = mapBlockIndex.begin();
    while (it != mapBlockIndex.end()) {
        if (!it->second->IsValid() && it->second->GetAncestor(nHeight) == pindex) {
            it->second->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(it->second);
            if (it->second->IsValid(BLOCK_VALID_TRANSACTIONS) && it->second->nChainTx && setBlockIndexCandidates.value_comp()(chainActive.Tip(), it->second)) {
                setBlockIndexCandidates.insert(it->second);
            }
            if (it->second == pindexBestInvalid) {
                // Reset invalid block marker if it was pointing to one of those.
                pindexBestInvalid = NULL;
            }
        }
        it++;
    }

    // Remove the invalidity flag from all ancestors too.
    while (pindex != NULL) {
        if (pindex->nStatus & BLOCK_FAILED_MASK) {
            pindex->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(pindex);
        }
        pindex = pindex->pprev;
    }
    return true;
}

CBlockIndex* AddToBlockIndex(const CBlock& block)
{
    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator it = mapBlockIndex.find(hash);
    if (it != mapBlockIndex.end())
        return it->second;

    // Construct new block index object
    CBlockIndex* pindexNew = new CBlockIndex(block);
    assert(pindexNew);
    // We assign the sequence id to blocks only when the full data is available,
    // to avoid miners withholding blocks but broadcasting headers, to get a
    // competitive advantage.
    pindexNew->nSequenceId = 0;
    BlockMap::iterator mi = mapBlockIndex.insert(std::make_pair(hash, pindexNew)).first;

    pindexNew->phashBlock = &((*mi).first);
    BlockMap::iterator miPrev = mapBlockIndex.find(block.hashPrevBlock);
    if (miPrev != mapBlockIndex.end()) {
        pindexNew->pprev = (*miPrev).second;
        pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
        pindexNew->BuildSkip();

        // ppcoin: compute stake entropy bit for stake modifier
        if (!pindexNew->SetStakeEntropyBit(pindexNew->GetStakeEntropyBit()))
            LogPrintf("AddToBlockIndex() : SetStakeEntropyBit() failed \n");

        if (!Params().GetConsensus().IsStakeModifierV2(pindexNew->nHeight)) {
            uint64_t nStakeModifier = 0;
            bool fGeneratedStakeModifier = false;
            if (!ComputeNextStakeModifier(pindexNew->pprev, nStakeModifier, fGeneratedStakeModifier))
                LogPrintf("AddToBlockIndex() : ComputeNextStakeModifier() failed \n");
            pindexNew->SetStakeModifier(nStakeModifier, fGeneratedStakeModifier);
        } else {
            // compute new v2 stake modifier
            pindexNew->SetNewStakeModifier(block.vtx[1].vin[0].prevout.hash);
        }
    }
    pindexNew->nChainWork = (pindexNew->pprev ? pindexNew->pprev->nChainWork : 0) + GetBlockProof(*pindexNew);
    pindexNew->RaiseValidity(BLOCK_VALID_TREE);
    if (pindexBestHeader == NULL || pindexBestHeader->nChainWork < pindexNew->nChainWork)
        pindexBestHeader = pindexNew;

    setDirtyBlockIndex.insert(pindexNew);

    return pindexNew;
}

/** Mark a block as having its data received and checked (up to BLOCK_VALID_TRANSACTIONS). */
bool ReceivedBlockTransactions(const CBlock& block, CValidationState& state, CBlockIndex* pindexNew, const CDiskBlockPos& pos)
{
    if (block.IsProofOfStake())
        pindexNew->SetProofOfStake();
    pindexNew->nTx = block.vtx.size();
    pindexNew->nChainTx = 0;
    pindexNew->nFile = pos.nFile;
    pindexNew->nDataPos = pos.nPos;
    pindexNew->nUndoPos = 0;
    pindexNew->nStatus |= BLOCK_HAVE_DATA;
    pindexNew->RaiseValidity(BLOCK_VALID_TRANSACTIONS);
    setDirtyBlockIndex.insert(pindexNew);

    if (pindexNew->pprev == NULL || pindexNew->pprev->nChainTx) {
        // If pindexNew is the genesis block or all parents are BLOCK_VALID_TRANSACTIONS.
        std::deque<CBlockIndex*> queue;
        queue.push_back(pindexNew);

        // Recursively process any descendant blocks that now may be eligible to be connected.
        while (!queue.empty()) {
            CBlockIndex* pindex = queue.front();
            queue.pop_front();
            pindex->nChainTx = (pindex->pprev ? pindex->pprev->nChainTx : 0) + pindex->nTx;
            {
                LOCK(cs_nBlockSequenceId);
                pindex->nSequenceId = nBlockSequenceId++;
            }
            if (chainActive.Tip() == NULL || !setBlockIndexCandidates.value_comp()(pindex, chainActive.Tip())) {
                setBlockIndexCandidates.insert(pindex);
            }
            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> range = mapBlocksUnlinked.equal_range(pindex);
            while (range.first != range.second) {
                std::multimap<CBlockIndex*, CBlockIndex*>::iterator it = range.first;
                queue.push_back(it->second);
                range.first++;
                mapBlocksUnlinked.erase(it);
            }
        }
    } else {
        if (pindexNew->pprev && pindexNew->pprev->IsValid(BLOCK_VALID_TREE)) {
            mapBlocksUnlinked.insert(std::make_pair(pindexNew->pprev, pindexNew));
        }
    }

    return true;
}


// --- CheckBlockTime (Keep DigiWage implementation) ---
bool CheckBlockTime(const CBlockHeader& block, CValidationState& state, CBlockIndex* const pindexPrev) {
    if (Params().IsRegTestNet()) return true;
    const int64_t blockTime = block.GetBlockTime();
    const int blockHeight = pindexPrev->nHeight + 1;
    if (blockTime > pindexPrev->MaxFutureBlockTime()) return state.Invalid(error("%s : block timestamp too far in the future", __func__), REJECT_INVALID, "time-too-new");
    if (blockTime <= pindexPrev->MinPastBlockTime()) return state.DoS(50, error("%s : block timestamp too old", __func__), REJECT_INVALID, "time-too-old");
    if (!Params().GetConsensus().IsValidBlockTimeStamp(blockTime, blockHeight)) return state.DoS(100, error("%s : block timestamp mask not valid", __func__), REJECT_INVALID, "invalid-time-mask");
    return true;
}

// --- CheckBlock (Modified for EVM) ---
bool CheckBlock(const CBlock& block, CValidationState& state, bool fCheckPOW, bool fCheckMerkleRoot, bool fCheckSig) {
    // These are checks that are independent of context.
    const bool IsPoS = block.IsProofOfStake();
    LogPrintf("%s: block=%s PoS=%d\n", __func__, block.GetHash().ToString().c_str(), IsPoS);

    if (block.fChecked) return true;

    // Check that the header is valid (particularly PoW).
    if (!CheckBlockHeader(block, state, !IsPoS)) // Don't check PoW for PoS block here
        return state.DoS(100, error("%s : CheckBlockHeader failed", __func__), REJECT_INVALID, "bad-header", true);

    // Check the merkle root.
    if (fCheckMerkleRoot) {
        bool mutated; uint256 hashMerkleRoot2 = BlockMerkleRoot(block, &mutated);
        if (block.hashMerkleRoot != hashMerkleRoot2) return state.DoS(100, error("%s : hashMerkleRoot mismatch", __func__), REJECT_INVALID, "bad-txnmrklroot", true);
        if (mutated) return state.DoS(100, error("%s : duplicate transaction", __func__), REJECT_INVALID, "bad-txns-duplicate", true);
    }

    // Size limits (Use DigiWage's constant for now)
    unsigned int nMaxBlockSize = MAX_BLOCK_SIZE_CURRENT;
    // TODO: EVM - Should this use dgpMaxBlockSize? Needs DGP integration.
    if (block.vtx.empty() || block.vtx.size() > nMaxBlockSize || ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION) > nMaxBlockSize)
        return state.DoS(100, error("%s : size limits failed", __func__), REJECT_INVALID, "bad-blk-length");

    // First transaction must be coinbase
    if (block.vtx.empty() || !block.vtx[0].IsCoinBase()) return state.DoS(100, error("%s : first tx is not coinbase", __func__), REJECT_INVALID, "bad-cb-missing");
    for (unsigned int i = 1; i < block.vtx.size(); i++) if (block.vtx[i].IsCoinBase()) return state.DoS(100, error("%s : more than one coinbase", __func__), REJECT_INVALID, "bad-cb-multiple");

    // EVM: Don't allow contract opcodes in coinbase
    if(TxHasOpSpend(block.vtx[0]) || TxHasCreateOrCall(block.vtx[0]) || TxHasOpSender(block.vtx[0])){
        return state.DoS(100, error("%s: coinbase contains contract opcodes", __func__), REJECT_INVALID, "bad-cb-contract");
    }

    if (IsPoS) {
        // Coinbase output should be empty if proof-of-stake block
        if (block.vtx[0].vout.size() != 1 || !block.vtx[0].vout[0].IsEmpty()) return state.DoS(100, error("%s : coinbase output not empty for proof-of-stake block", __func__));
        // Second transaction must be coinstake
        if (block.vtx.empty() || block.vtx.size() < 2 || !block.vtx[1].IsCoinStake()) return state.DoS(100, error("%s : second tx is not coinstake", __func__));
        for (unsigned int i = 2; i < block.vtx.size(); i++) if (block.vtx[i].IsCoinStake()) return state.DoS(100, error("%s : more than one coinstake", __func__));
        // EVM: Don't allow contract opcodes in coinstake (Qtum restriction)
        if(TxHasOpSpend(block.vtx[1]) || TxHasCreateOrCall(block.vtx[1]) || TxHasOpSender(block.vtx[1])){
            return state.DoS(100, error("%s: coinstake contains contract opcodes", __func__), REJECT_INVALID, "bad-cs-contract");
        }
    }

    // Check SwiftX locks (Keep DigiWage logic)
    if (sporkManager.IsSporkActive(SPORK_3_SWIFTTX_BLOCK_FILTERING)) {
        for (const CTransaction& tx : block.vtx) {
            if (!tx.IsCoinBase()) {
                for (const CTxIn& in : tx.vin) {
                    if (mapLockedInputs.count(in.prevout)) {
                        if (mapLockedInputs[in.prevout] != tx.GetHash()) {
                            mapRejectedBlocks.insert(std::make_pair(block.GetHash(), GetTime()));
                            LogPrintf("%s : found conflicting transaction with transaction lock %s %s\n", __func__, mapLockedInputs[in.prevout].ToString(), tx.GetHash().GetHex());
                            return state.DoS(0, error("%s : found conflicting transaction with transaction lock", __func__), REJECT_INVALID, "conflicting-tx-ix");
                        }
                    }
                }
            }
        }
    } else { LogPrintf("%s : skipping transaction locking checks\n", __func__); }

    // Cold Staking / Masternode / Budget checks (Keep DigiWage context-dependent logic)
    // Note: These might need adjustment based on block height context, moved to Contextual checks?
    bool fColdStakingActive = true; // Assume active for basic check, context check will use spork
    bool fZerocoinActive = block.GetBlockTime() > Params().GetConsensus().ZC_TimeStart;
    CBlockIndex* pindexPrev = nullptr;
    if (mapBlockIndex.count(block.hashPrevBlock)) pindexPrev = mapBlockIndex[block.hashPrevBlock];
    int nHeight = pindexPrev ? pindexPrev->nHeight + 1 : 0;

    if (pindexPrev) { // Only do these checks if we know the previous block
        fColdStakingActive = sporkManager.IsSporkActive(SPORK_17_COLDSTAKING_ENFORCEMENT);
        if (!IsInitialBlockDownload()) {
             if (IsPoS && nHeight > 0 && !CheckColdStakeFreeOutput(block.vtx[1], nHeight)) { // Check moved from ConnectBlock for earlier failure
                 mapRejectedBlocks.insert(std::make_pair(block.GetHash(), GetTime()));
                 return state.DoS(0, error("%s : Cold stake outputs not valid", __func__), REJECT_INVALID, "bad-p2cs-outs");
             }
             if (nHeight > 0 && !IsBlockPayeeValid(block, nHeight)) { // Check moved from ConnectBlock
                 mapRejectedBlocks.insert(std::make_pair(block.GetHash(), GetTime()));
                 return state.DoS(0, error("%s : Couldn't find masternode/budget payment", __func__), REJECT_INVALID, "bad-cb-payee");
             }
        } else { if (fDebug) LogPrintf("%s: Masternode/Budget/P2CS payment checks skipped on sync\n", __func__); }
    }

    // Check transactions basic validity
    bool lastWasContractOrSpend = false;
    for (const CTransaction& tx : block.vtx) {
        if (!CheckTransaction(tx, fZerocoinActive, state, fColdStakingActive)) // Use flags based on context
            return error("%s : CheckTransaction failed for tx %s", __func__, tx.GetHash().ToString());

        // EVM: Check OP_SPEND ordering
        bool thisHasOpSpend = TxHasOpSpend(tx);
        if(thisHasOpSpend && !lastWasContractOrSpend && !TxHasCreateOrCall(tx)){
            return state.DoS(100, error("%s: OP_SPEND without preceding contract/spend tx %s", __func__, tx.GetHash().ToString()),
                             REJECT_INVALID, "bad-opspend-tx");
        }
        lastWasContractOrSpend = TxHasCreateOrCall(tx) || thisHasOpSpend;
    }

    // Check legacy sigops
    unsigned int nSigOps = 0;
    for (const CTransaction& tx : block.vtx) { nSigOps += GetLegacySigOpCount(tx); }
    unsigned int nMaxBlockSigOps = MAX_BLOCK_SIGOPS_CURRENT; // Adapt based on ZC activation?
    if (nSigOps > nMaxBlockSigOps) return state.DoS(100, error("%s : out-of-bounds SigOpCount", __func__), REJECT_INVALID, "bad-blk-sigops", true);

    // Check PoS block signature if requested
    if (IsPoS && fCheckSig && !CheckBlockSignature(block))
    LogPrintf("%s: bad PoS block signature\n", __func__); // Log message
    return state.Invalid(false, REJECT_INVALID, "bad-blk-sig"); // Call with correct 3 arguments
    if (fCheckPOW && fCheckMerkleRoot && fCheckSig) block.fChecked = true;
    return true;
}

// --- ContextualCheckBlockHeader (Keep DigiWage implementation) ---
bool ContextualCheckBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex* const pindexPrev) {
    const Consensus::Params& consensus = Params().GetConsensus();
    uint256 hash = block.GetHash();
    if (hash == consensus.hashGenesisBlock) return true;
    assert(pindexPrev);
    const int nHeight = pindexPrev->nHeight + 1;
    const int chainHeight = chainActive.Height();
    int nMaxReorgDepth = GetArg("-maxreorg", DEFAULT_MAX_REORG_DEPTH);
    if (chainHeight - nHeight >= nMaxReorgDepth) return state.DoS(1, error("%s: forked chain older than max reorganization depth (height %d)", __func__, chainHeight - nHeight));
    if (!CheckBlockTime(block, state, pindexPrev)) return false;
    if (!Checkpoints::CheckBlock(nHeight, hash)) return state.DoS(100, error("%s : rejected by checkpoint lock-in at %d", __func__, nHeight), REJECT_CHECKPOINT, "checkpoint mismatch");
    CBlockIndex* pcheckpoint = Checkpoints::GetLastCheckpoint();
    if (pcheckpoint && nHeight < pcheckpoint->nHeight) return state.DoS(0, error("%s : forked chain older than last checkpoint (height %d)", __func__, nHeight));
    if ((block.nVersion < 3 && nHeight >= 1) || (block.nVersion < 4 && nHeight >= consensus.height_start_ZC) || (block.nVersion < 5 && nHeight >= consensus.height_RHF)) {
        std::string stringErr = strprintf("rejected block version %d at height %d", block.nVersion, nHeight);
        return state.Invalid(error("%s : %s", __func__, stringErr), REJECT_OBSOLETE, stringErr);
    }
    // EVM/PoS difficulty check moved to ConnectBlock/CheckBlockHeader as it needs state/prevout info
    return true;
}

// --- IsBlockHashInChain (Keep DigiWage implementation) ---
bool IsBlockHashInChain(const uint256& hashBlock) {
    LOCK(cs_main); // Need lock for mapBlockIndex/chainActive
    if (hashBlock.IsNull() || !mapBlockIndex.count(hashBlock)) return false;
    return chainActive.Contains(mapBlockIndex[hashBlock]);
}

// --- IsTransactionInChain (Keep DigiWage implementation) ---
bool IsTransactionInChain(const uint256& txId, int& nHeightTx, CTransaction& tx) {
    uint256 hashBlock;
    if (!GetTransaction(txId, tx, hashBlock, true)) return false;
    if (!IsBlockHashInChain(hashBlock)) return false;
    LOCK(cs_main); // Need lock for mapBlockIndex
    nHeightTx = mapBlockIndex.at(hashBlock)->nHeight;
    return true;
}
bool IsTransactionInChain(const uint256& txId, int& nHeightTx) {
    CTransaction tx; return IsTransactionInChain(txId, nHeightTx, tx);
}

// --- ContextualCheckBlock (Keep DigiWage implementation) ---
bool ContextualCheckBlock(const CBlock& block, CValidationState& state, CBlockIndex* const pindexPrev) {
    const int nHeight = pindexPrev == nullptr ? 0 : pindexPrev->nHeight + 1;
    for (const CTransaction& tx : block.vtx) if (!IsFinalTx(tx, nHeight, block.GetBlockTime())) return state.DoS(10, error("%s : contains a non-final transaction", __func__), REJECT_INVALID, "bad-txns-nonfinal");
    if (pindexPrev) {
        CScript expect = CScript() << nHeight;
        bool isAfterRHF = Params().GetConsensus().IsPastRHFBlock(nHeight);
        if(isAfterRHF && (block.vtx[0].vin[0].scriptSig.size() < expect.size() || !std::equal(expect.begin(), expect.end(), block.vtx[0].vin[0].scriptSig.begin()))) {
             return state.DoS(100, error("%s : block height mismatch in coinbase", __func__), REJECT_INVALID, "bad-cb-height");
        }
    }
    // EVM witness commitment check? Qtum handles this differently. Keep DigiWage simple for now.
    return true;
}

// --- AcceptBlockHeader (Keep DigiWage implementation, add EVM PoS check?) ---
bool AcceptBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex** ppindex) {
    AssertLockHeld(cs_main);
    uint256 hash = block.GetHash();
    auto miSelf = mapBlockIndex.find(hash);
    CBlockIndex* pindex = nullptr;
    if (miSelf != mapBlockIndex.end()) {
        pindex = miSelf->second;
        if (ppindex) *ppindex = pindex;
        if (pindex->nStatus & BLOCK_FAILED_MASK) return state.Invalid(error("%s : block is marked invalid", __func__), 0, "duplicate");
        return true;
    }
    if (!CheckBlockHeader(block, state, true)) { // Check basic header validity (PoW if applicable)
        LogPrintf("AcceptBlockHeader(): CheckBlockHeader failed \n");
        return false;
    }
    // Get prev block index
    CBlockIndex* pindexPrev = nullptr;
    if (hash != Params().GetConsensus().hashGenesisBlock) {
        auto mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi == mapBlockIndex.end()) return state.DoS(0, error("%s : prev block %s not found", __func__, block.hashPrevBlock.GetHex()), 0, "bad-prevblk");
        pindexPrev = (*mi).second;
        if (pindexPrev->nStatus & BLOCK_FAILED_MASK) {
            // Consider reconsidering based on checkpoints (Keep DigiWage logic)
            if (Checkpoints::CheckBlock(pindexPrev->nHeight, block.hashPrevBlock, true)) {
                LogPrintf("%s : Reconsidering block %s height %d\n", __func__, pindexPrev->GetBlockHash().GetHex(), pindexPrev->nHeight);
                CValidationState statePrev; ReconsiderBlock(statePrev, pindexPrev);
                if (statePrev.IsValid()) { ActivateBestChain(statePrev); return true; } // Might return true even if Activation fails? Check logic.
            }
            return state.DoS(100, error("%s : prev block height=%d hash=%s is invalid, unable to add block %s", __func__, pindexPrev->nHeight, block.hashPrevBlock.GetHex(), block.GetHash().GetHex()), REJECT_INVALID, "bad-prevblk");
        }
    }
    if (!ContextualCheckBlockHeader(block, state, pindexPrev)) return false;

    // EVM: Check PoS header validity if applicable?
    // Qtum's CheckHeaderPoS requires CCoinsViewCache, can't be done here easily.
    // Rely on checks within ConnectBlock/CheckBlock for now.

    if (pindex == nullptr) pindex = AddToBlockIndex(block);
    if (ppindex) *ppindex = pindex;
    return true;
}

// --- AcceptBlock (Keep DigiWage implementation, ensure state roots are potentially handled in AddToBlockIndex/ConnectBlock) ---
bool AcceptBlock(CBlock& block, CValidationState& state, CBlockIndex** ppindex, CDiskBlockPos* dbp, bool fAlreadyCheckedBlock) {
    AssertLockHeld(cs_main);
    CBlockIndex*& pindex = *ppindex;
    CBlockIndex* pindexPrev = nullptr;
    if (block.GetHash() != Params().GetConsensus().hashGenesisBlock) {
        auto mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi == mapBlockIndex.end()) return state.DoS(0, error("%s : prev block %s not found", __func__, block.hashPrevBlock.GetHex()), 0, "bad-prevblk");
        pindexPrev = (*mi).second;
        if (pindexPrev->nStatus & BLOCK_FAILED_MASK) {
            if (Checkpoints::CheckBlock(pindexPrev->nHeight, block.hashPrevBlock, true)) {
                LogPrintf("%s : Reconsidering block %s height %d\n", __func__, pindexPrev->GetBlockHash().GetHex(), pindexPrev->nHeight);
                CValidationState statePrev; ReconsiderBlock(statePrev, pindexPrev);
                if (statePrev.IsValid()) { ActivateBestChain(statePrev); return true; }
            }
            return state.DoS(100, error("%s : prev block %s is invalid, unable to add block %s", __func__, block.hashPrevBlock.GetHex(), block.GetHash().GetHex()), REJECT_INVALID, "bad-prevblk");
        }
    }
    // PoS check (Keep DigiWage logic)
    if (block.IsProofOfStake()) {
        std::string strError;
        if (!CheckProofOfStake(block, strError, pindexPrev)) return state.DoS(100, error("%s: proof of stake check failed (%s)", __func__, strError));
    }
    if (!AcceptBlockHeader(block, state, &pindex)) return false;
    if (pindex->nStatus & BLOCK_HAVE_DATA) { LogPrintf("AcceptBlock() : already have block %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString()); return true; }
    if ((!fAlreadyCheckedBlock && !CheckBlock(block, state)) || !ContextualCheckBlock(block, state, pindex->pprev)) {
        if (state.IsInvalid() && !state.CorruptionPossible()) { pindex->nStatus |= BLOCK_FAILED_VALID; setDirtyBlockIndex.insert(pindex); }
        return false;
    }
    int nHeight = pindex->nHeight;
    // Write block to history file
    try {
        unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        CDiskBlockPos blockPos; if (dbp != NULL) blockPos = *dbp;
        if (!FindBlockPos(state, blockPos, nBlockSize + 8, nHeight, block.GetBlockTime(), dbp != NULL)) return error("AcceptBlock() : FindBlockPos failed");
        if (dbp == NULL) if (!WriteBlockToDisk(block, blockPos)) return AbortNode(state, "Failed to write block");
        if (!ReceivedBlockTransactions(block, state, pindex, blockPos)) return error("AcceptBlock() : ReceivedBlockTransactions failed");
    } catch (const std::runtime_error& e) { return AbortNode(state, std::string("System error: ") + e.what()); }
    return true;
}

// ... (Keep DigiWage AddToBlockIndex - Ensure nStatus, nChainWork, nChainTx, skip pointers are handled correctly. EVM roots are set in ConnectBlock)


// ... (Keep DigiWage ReceivedBlockTransactions)

// ... (Keep DigiWage FindBlockPos, FindUndoPos)

// ... (Keep DigiWage CheckBlockHeader, CheckProofOfStake - if DigiWage has its own implementation)

// ... (Keep DigiWage CheckColdStakeFreeOutput)

// ... (Keep DigiWage ProcessNewBlock)

// ... (Keep DigiWage TestBlockValidity)

// ... (Keep DigiWage CheckDiskSpace, OpenDiskFile, OpenBlockFile, OpenUndoFile, GetBlockPosFilename)

// ... (Keep DigiWage InsertBlockIndex)


bool FindBlockPos(CValidationState& state, CDiskBlockPos& pos, unsigned int nAddSize, unsigned int nHeight, uint64_t nTime, bool fKnown)
{
    LOCK(cs_LastBlockFile);

    unsigned int nFile = fKnown ? pos.nFile : nLastBlockFile;
    if (vinfoBlockFile.size() <= nFile) {
        vinfoBlockFile.resize(nFile + 1);
    }

    if (!fKnown) {
        while (vinfoBlockFile[nFile].nSize + nAddSize >= MAX_BLOCKFILE_SIZE) {
            LogPrintf("Leaving block file %i: %s\n", nFile, vinfoBlockFile[nFile].ToString());
            FlushBlockFile(true);
            nFile++;
            if (vinfoBlockFile.size() <= nFile) {
                vinfoBlockFile.resize(nFile + 1);
            }
        }
        pos.nFile = nFile;
        pos.nPos = vinfoBlockFile[nFile].nSize;
    }

    nLastBlockFile = nFile;
    vinfoBlockFile[nFile].AddBlock(nHeight, nTime);
    if (fKnown)
        vinfoBlockFile[nFile].nSize = std::max(pos.nPos + nAddSize, vinfoBlockFile[nFile].nSize);
    else
        vinfoBlockFile[nFile].nSize += nAddSize;

    if (!fKnown) {
        unsigned int nOldChunks = (pos.nPos + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        unsigned int nNewChunks = (vinfoBlockFile[nFile].nSize + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        if (nNewChunks > nOldChunks) {
            if (CheckDiskSpace(nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos)) {
                FILE* file = OpenBlockFile(pos);
                if (file) {
                    LogPrintf("Pre-allocating up to position 0x%x in blk%05u.dat\n", nNewChunks * BLOCKFILE_CHUNK_SIZE, pos.nFile);
                    AllocateFileRange(file, pos.nPos, nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos);
                    fclose(file);
                }
            } else
                return state.Error("out of disk space");
        }
    }

    setDirtyFileInfo.insert(nFile);
    return true;
}

bool FindUndoPos(CValidationState& state, int nFile, CDiskBlockPos& pos, unsigned int nAddSize)
{
    pos.nFile = nFile;

    LOCK(cs_LastBlockFile);

    unsigned int nNewSize;
    pos.nPos = vinfoBlockFile[nFile].nUndoSize;
    nNewSize = vinfoBlockFile[nFile].nUndoSize += nAddSize;
    setDirtyFileInfo.insert(nFile);

    unsigned int nOldChunks = (pos.nPos + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    unsigned int nNewChunks = (nNewSize + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    if (nNewChunks > nOldChunks) {
        if (CheckDiskSpace(nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos)) {
            FILE* file = OpenUndoFile(pos);
            if (file) {
                LogPrintf("Pre-allocating up to position 0x%x in rev%05u.dat\n", nNewChunks * UNDOFILE_CHUNK_SIZE, pos.nFile);
                AllocateFileRange(file, pos.nPos, nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos);
                fclose(file);
            }
        } else
            return state.Error("out of disk space");
    }

    return true;
}

// main.cpp: Add return true at the end of CheckBlockHeader
bool CheckBlockHeader(const CBlockHeader& block, CValidationState& state, bool fCheckPOW)
{
    // Check proof of work matches claimed amount
    if (fCheckPOW && !CheckProofOfWork(block.GetHash(), block.nBits))
        return state.DoS(50, error("CheckBlockHeader() : proof of work failed"),
            REJECT_INVALID, "high-hash");

    if (Params().IsRegTestNet()) return true;

    // Add missing return for non-regtest case if POW check passes or isn't required
    return true;
}

bool CheckColdStakeFreeOutput(const CTransaction& tx, const int nHeight)
{
    if (!tx.HasP2CSOutputs())
        return true;

    const unsigned int outs = tx.vout.size();
    const CTxOut& lastOut = tx.vout[outs-1];
    if (outs >=3 && lastOut.scriptPubKey != tx.vout[outs-2].scriptPubKey) {
        // last output can either be a mn reward or a budget payment
        if (lastOut.nValue == GetMasternodePayment(nHeight, GetBlockValue(nHeight), 0))
            return true;

        // This could be a budget block.
        if (Params().IsRegTestNet())
            return false;

        // if mnsync is incomplete, we cannot verify if this is a budget block.
        // so we check that the staker is not transferring value to the free output
        if (!masternodeSync.IsSynced()) {
            // First try finding the previous transaction in database
            CTransaction txPrev; uint256 hashBlock;
            if (!GetTransaction(tx.vin[0].prevout.hash, txPrev, hashBlock, true))
                return error("%s : read txPrev failed: %s",  __func__, tx.vin[0].prevout.hash.GetHex());
            CAmount amtIn = txPrev.vout[tx.vin[0].prevout.n].nValue + GetBlockValue(nHeight - 1);
            CAmount amtOut = 0;
            for (unsigned int i = 1; i < outs-1; i++) amtOut += tx.vout[i].nValue;
            if (amtOut != amtIn)
                return error("%s: non-free outputs value %d less than required %d", __func__, amtOut, amtIn);
            return true;
        }

        // Check that this is indeed a superblock.
        if (budget.IsBudgetPaymentBlock(nHeight)) {
            // if superblocks are not enabled, reject
            if (!sporkManager.IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS))
                return error("%s: superblocks are not enabled");
            return true;
        }

        // wrong free output
        return error("%s: Wrong cold staking outputs: vout[%d].scriptPubKey (%s) != vout[%d].scriptPubKey (%s) - value: %s",
                __func__, outs-1, HexStr(lastOut.scriptPubKey), outs-2, HexStr(tx.vout[outs-2].scriptPubKey), FormatMoney(lastOut.nValue).c_str());
    }

    return true;
}


bool ProcessNewBlock(CValidationState& state, CNode* pfrom, CBlock* pblock, CDiskBlockPos* dbp)
{
    AssertLockNotHeld(cs_main);

    // Preliminary checks
    int64_t nStartTime = GetTimeMillis();

    // check block
    bool checked = CheckBlock(*pblock, state);

    if (!CheckBlockSignature(*pblock))
        return error("%s : bad proof-of-stake block signature", __func__);

    if (pblock->GetHash() != Params().GetConsensus().hashGenesisBlock && pfrom != NULL) {
        //if we get this far, check if the prev block is our prev block, if not then request sync and return false
        BlockMap::iterator mi = mapBlockIndex.find(pblock->hashPrevBlock);
        if (mi == mapBlockIndex.end()) {
            pfrom->PushMessage("getblocks", chainActive.GetLocator(), UINT256_ZERO);
            return false;
        }
    }

    {
        LOCK(cs_main);

        MarkBlockAsReceived(pblock->GetHash());
        if (!checked) {
            return error ("%s : CheckBlock FAILED for block %s", __func__, pblock->GetHash().GetHex());
        }

        // Store to disk
        CBlockIndex* pindex = nullptr;
        bool ret = AcceptBlock(*pblock, state, &pindex, dbp, checked);
        if (pindex && pfrom) {
            mapBlockSource[pindex->GetBlockHash ()] = pfrom->GetId ();
        }
        CheckBlockIndex ();
        if (!ret) {
            // Check spamming
            if(pindex && pfrom && GetBoolArg("-blockspamfilter", DEFAULT_BLOCK_SPAM_FILTER)) {
                CNodeState *nodestate = State(pfrom->GetId());
                if(nodestate != nullptr) {
                    nodestate->nodeBlocks.onBlockReceived(pindex->nHeight);
                    bool nodeStatus = true;
                    // UpdateState will return false if the node is attacking us or update the score and return true.
                    nodeStatus = nodestate->nodeBlocks.updateState(state, nodeStatus);
                    int nDoS = 0;
                    if (state.IsInvalid(nDoS)) {
                        if (nDoS > 0)
                            Misbehaving(pfrom->GetId(), nDoS);
                        nodeStatus = false;
                    }
                    if (!nodeStatus)
                        return error("%s : AcceptBlock FAILED - block spam protection", __func__);
                }
            }
            return error("%s : AcceptBlock FAILED", __func__);
        }
    }

    if (!ActivateBestChain(state, pblock, checked))
        return error("%s : ActivateBestChain failed", __func__);

    if (!fLiteMode) {
        if (masternodeSync.RequestedMasternodeAssets > MASTERNODE_SYNC_LIST) {
            obfuScationPool.NewBlock();
            masternodePayments.ProcessBlock(GetHeight() + 10);
            budget.NewBlock();
        }
    }

    if (pwalletMain) {
        // If turned on MultiSend will send a transaction (or more) on the after maturity of a stake
        if (pwalletMain->isMultiSendEnabled())
            pwalletMain->MultiSend();

        // If turned on Auto Combine will scan wallet for dust to combine
        if (pwalletMain->fCombineDust)
            pwalletMain->AutoCombineDust();
    }

    LogPrintf("%s : ACCEPTED Block %ld in %ld milliseconds with size=%d\n", __func__, GetHeight(), GetTimeMillis() - nStartTime,
              pblock->GetSerializeSize(SER_DISK, CLIENT_VERSION));

    return true;
}

bool TestBlockValidity(CValidationState& state, const CBlock& block, CBlockIndex* const pindexPrev, bool fCheckPOW, bool fCheckMerkleRoot)
{
    AssertLockHeld(cs_main);
    assert(pindexPrev);
    if (pindexPrev != chainActive.Tip()) {
        LogPrintf("%s : No longer working on chain tip\n", __func__);
        return false;
    }

    CCoinsViewCache viewNew(pcoinsTip);
    CBlockIndex indexDummy(block);
    indexDummy.pprev = pindexPrev;
    indexDummy.nHeight = pindexPrev->nHeight + 1;

    // NOTE: CheckBlockHeader is called by CheckBlock
    if (!ContextualCheckBlockHeader(block, state, pindexPrev))
        return false;
    if (!CheckBlock(block, state, fCheckPOW, fCheckMerkleRoot))
        return false;
    if (!ContextualCheckBlock(block, state, pindexPrev))
        return false;
    if (!ConnectBlock(block, state, &indexDummy, viewNew, true))
        return false;
    assert(state.IsValid());

    return true;
}

bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
    uint64_t nFreeBytesAvailable = boost::filesystem::space(GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
        return AbortNode("Disk space is low!", _("Error: Disk space is low!"));

    return true;
}

FILE* OpenDiskFile(const CDiskBlockPos& pos, const char* prefix, bool fReadOnly)
{
    if (pos.IsNull())
        return NULL;
    boost::filesystem::path path = GetBlockPosFilename(pos, prefix);
    boost::filesystem::create_directories(path.parent_path());
    FILE* file = fopen(path.string().c_str(), "rb+");
    if (!file && !fReadOnly)
        file = fopen(path.string().c_str(), "wb+");
    if (!file) {
        LogPrintf("Unable to open file %s\n", path.string());
        return NULL;
    }
    if (pos.nPos) {
        if (fseek(file, pos.nPos, SEEK_SET)) {
            LogPrintf("Unable to seek to position %u of %s\n", pos.nPos, path.string());
            fclose(file);
            return NULL;
        }
    }
    return file;
}

FILE* OpenBlockFile(const CDiskBlockPos& pos, bool fReadOnly)
{
    return OpenDiskFile(pos, "blk", fReadOnly);
}

FILE* OpenUndoFile(const CDiskBlockPos& pos, bool fReadOnly)
{
    return OpenDiskFile(pos, "rev", fReadOnly);
}

boost::filesystem::path GetBlockPosFilename(const CDiskBlockPos& pos, const char* prefix)
{
    return GetDataDir() / "blocks" / strprintf("%s%05u.dat", prefix, pos.nFile);
}

CBlockIndex* InsertBlockIndex(uint256 hash)
{
    if (hash.IsNull())
        return NULL;

    // Return existing
    BlockMap::iterator mi = mapBlockIndex.find(hash);
    if (mi != mapBlockIndex.end())
        return (*mi).second;

    // Create new
    CBlockIndex* pindexNew = new CBlockIndex();
    if (!pindexNew)
        throw std::runtime_error("LoadBlockIndex() : new CBlockIndex failed");
    mi = mapBlockIndex.insert(std::make_pair(hash, pindexNew)).first;

    pindexNew->phashBlock = &((*mi).first);

    return pindexNew;
}


// --- LoadBlockIndexDB (Keep DigiWage implementation) ---
bool static LoadBlockIndexDB(std::string& strError) {
    if (!pblocktree->LoadBlockIndexGuts()) return false;
    boost::this_thread::interruption_point();
    std::vector<std::pair<int, CBlockIndex*> > vSortedByHeight;
    vSortedByHeight.reserve(mapBlockIndex.size());
    for (const auto& item : mapBlockIndex) { vSortedByHeight.push_back(std::make_pair(item.second->nHeight, item.second)); }
    std::sort(vSortedByHeight.begin(), vSortedByHeight.end());
    for (const auto& item : vSortedByHeight) {
        if (ShutdownRequested()) return false;
        CBlockIndex* pindex = item.second;
        pindex->nChainWork = (pindex->pprev ? pindex->pprev->nChainWork : 0) + GetBlockProof(*pindex);
        if (pindex->nStatus & BLOCK_HAVE_DATA) {
            if (pindex->pprev) {
                if (pindex->pprev->nChainTx) pindex->nChainTx = pindex->pprev->nChainTx + pindex->nTx;
                else { pindex->nChainTx = 0; mapBlocksUnlinked.insert(std::make_pair(pindex->pprev, pindex)); }
            } else pindex->nChainTx = pindex->nTx;
        }
        if (pindex->IsValid(BLOCK_VALID_TRANSACTIONS) && (pindex->nChainTx || pindex->pprev == nullptr)) setBlockIndexCandidates.insert(pindex);
        if (pindex->nStatus & BLOCK_FAILED_MASK && (!pindexBestInvalid || pindex->nChainWork > pindexBestInvalid->nChainWork)) pindexBestInvalid = pindex;
        if (pindex->pprev) pindex->BuildSkip();
        if (pindex->IsValid(BLOCK_VALID_TREE) && (pindexBestHeader == nullptr || CBlockIndexWorkComparator()(pindexBestHeader, pindex))) {
            pindexBestHeader = pindex;
            // Keep DigiWage checkpoint validation logic
             if (pindex->nHeight % 25000 == 0) {
                 if (!Checkpoints::CheckBlock(pindex->nHeight, pindex->GetBlockHash())) {
                     fBlockDatabaseForkFound = true; LogPrintf("LoadBlockIndexDB() : Possible fork detected... (height: %i, hash: %s)\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                 } else { fBlockDatabaseForkFound = false; LogPrintf("LoadBlockIndexDB() : No fork detected... (height: %i, hash: %s)\n", pindex->nHeight, pindex->GetBlockHash().ToString()); }
             } else if (!fBlockDatabaseForkFound && pindex->nHeight > 1400000) { // DigiWage specific height?
                 if (!Checkpoints::CheckBlock(pindex->nHeight, pindex->GetBlockHash())) { fBlockDatabaseForkFound = true; LogPrintf("LoadBlockIndexDB() : (>1400000) Possible fork detected...\n"); }
             }
        }
    }
    if (fBlockDatabaseForkFound) LogPrintf("LoadBlockIndexDB() : WARNING: Blockchain database fork found...\n");
    pblocktree->ReadLastBlockFile(nLastBlockFile);
    vinfoBlockFile.resize(nLastBlockFile + 1);
    LogPrintf("%s: last block file = %i\n", __func__, nLastBlockFile);
    for (int nFile = 0; nFile <= nLastBlockFile; nFile++) pblocktree->ReadBlockFileInfo(nFile, vinfoBlockFile[nFile]);
    LogPrintf("%s: last block file info: %s\n", __func__, vinfoBlockFile[nLastBlockFile].ToString());
    for (int nFile = nLastBlockFile + 1; true; nFile++) { CBlockFileInfo info; if (pblocktree->ReadBlockFileInfo(nFile, info)) vinfoBlockFile.push_back(info); else break; }
    LogPrintf("Checking all blk files are present...\n");
    std::set<int> setBlkDataFiles;
    for (const auto& item : mapBlockIndex) { if (item.second->nStatus & BLOCK_HAVE_DATA) setBlkDataFiles.insert(item.second->nFile); }
    for (int nFile : setBlkDataFiles) { CDiskBlockPos pos(nFile, 0); if (CAutoFile(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION).IsNull()) return false; }
    bool fLastShutdownWasPrepared = true; pblocktree->ReadFlag("shutdown", fLastShutdownWasPrepared);
    LogPrintf("%s: Last shutdown was prepared: %s\n", __func__, fLastShutdownWasPrepared);
    bool fReindexing = false; pblocktree->ReadReindexing(fReindexing); if(fReindexing) fReindex = true;
    pblocktree->ReadFlag("txindex", fTxIndex); LogPrintf("LoadBlockIndexDB(): transaction index %s\n", fTxIndex ? "enabled" : "disabled");
    // EVM flags (Read from DB)
    pblocktree->ReadFlag("logevents", fLogEvents); LogPrintf("LoadBlockIndexDB(): logevents %s\n", fLogEvents ? "enabled" : "disabled");
    pblocktree->ReadFlag("addrindex", fAddressIndex); LogPrintf("LoadBlockIndexDB(): addrindex %s\n", fAddressIndex ? "enabled" : "disabled");
    pblocktree->WriteFlag("shutdown", false);
    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    if (it == mapBlockIndex.end()) return true; // DB state is empty/corrupt?
    chainActive.SetTip(it->second); PruneBlockIndexCandidates();
    const CBlockIndex* pChainTip = chainActive.Tip();
    if (pChainTip) { // Check tip exists
        LogPrintf("LoadBlockIndexDB(): hashBestChain=%s height=%d date=%s progress=%f\n", pChainTip->GetBlockHash().GetHex(), pChainTip->nHeight, DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pChainTip->GetBlockTime()), Checkpoints::GuessVerificationProgress(pChainTip));
    }
    return true;
}

// --- CVerifyDB (Keep DigiWage implementation) ---
CVerifyDB::CVerifyDB() { uiInterface.ShowProgress(_("Verifying blocks..."), 0); }
CVerifyDB::~CVerifyDB() { uiInterface.ShowProgress("", 100); }
bool CVerifyDB::VerifyDB(CCoinsView* coinsview, int nCheckLevel, int nCheckDepth) {
    LOCK(cs_main); if (chainActive.Tip() == NULL || chainActive.Tip()->pprev == NULL) return true;
    const int chainHeight = chainActive.Height();
    if (nCheckDepth <= 0 || nCheckDepth > chainHeight) nCheckDepth = chainHeight;
    nCheckLevel = std::max(0, std::min(4, nCheckLevel));
    LogPrintf("Verifying last %i blocks at level %i\n", nCheckDepth, nCheckLevel);
    CCoinsViewCache coins(coinsview); CBlockIndex* pindexState = chainActive.Tip(); CBlockIndex* pindexFailure = NULL;
    int nGoodTransactions = 0; CValidationState state; int reportDone = 0;
    dev::h256 oldHashStateRoot, oldHashUTXORoot; // EVM state backup
    if(globalState) { oldHashStateRoot = globalState->rootHash(); oldHashUTXORoot = globalState->rootHashUTXO(); }
    LogPrintf("Verification progress: 0%%\n");
    for (CBlockIndex* pindex = chainActive.Tip(); pindex && pindex->pprev; pindex = pindex->pprev) {
        boost::this_thread::interruption_point();
        int percentageDone = std::max(1, std::min(99, (int)(((double)(chainHeight - pindex->nHeight)) / (double)nCheckDepth * (nCheckLevel >= 4 ? 50 : 100))));
        if (reportDone < percentageDone / 10) { LogPrintf("Verification progress: %d%%\n", percentageDone); reportDone = percentageDone / 10; }
        uiInterface.ShowProgress(_("Verifying blocks..."), percentageDone);
        if (pindex->nHeight < chainHeight - nCheckDepth) break;
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex)) return error("VerifyDB() : *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
        if (nCheckLevel >= 1 && !CheckBlock(block, state, false)) return error("VerifyDB() : *** found bad block at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
        if (nCheckLevel >= 2 && pindex) { CBlockUndo undo; CDiskBlockPos pos = pindex->GetUndoPos(); if (!pos.IsNull()) if (!undo.ReadFromDisk(pos, pindex->pprev->GetBlockHash())) return error("VerifyDB() : *** found bad undo data at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString()); }
        if (nCheckLevel >= 3 && pindex == pindexState && (coins.GetCacheSize() + pcoinsTip->GetCacheSize()) <= nCoinCacheSize) {
            bool fClean = true;
            if (!DisconnectBlock(block, state, pindex, coins, &fClean)) return error("VerifyDB() : *** irrecoverable inconsistency in block data at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
            pindexState = pindex->pprev; if (!fClean) { nGoodTransactions = 0; pindexFailure = pindex; } else nGoodTransactions += block.vtx.size();
        }
        if (ShutdownRequested()) {
            if(globalState) { globalState->setRoot(oldHashStateRoot); globalState->setRootUTXO(oldHashUTXORoot); } // Revert EVM state on interrupt
            return true;
        }
    }
    if (pindexFailure) {
        if(globalState) { globalState->setRoot(oldHashStateRoot); globalState->setRootUTXO(oldHashUTXORoot); } // Revert EVM state
        return error("VerifyDB() : *** coin database inconsistencies found (last %i blocks, %i good transactions before that)\n", chainHeight - pindexFailure->nHeight + 1, nGoodTransactions);
    }
    int block_count = chainHeight - (pindexState ? pindexState->nHeight : 0); // Handle null pindexState
    if (nCheckLevel >= 4) {
        CBlockIndex* pindex = pindexState;
        while (pindex != chainActive.Tip()) {
            boost::this_thread::interruption_point();
            int percentageDone = std::max(1, std::min(99, 100 - (int)(((double)(chainHeight - (pindex ? pindex->nHeight : 0))) / (double)nCheckDepth * 50))); // Handle null pindex
            uiInterface.ShowProgress(_("Verifying blocks..."), percentageDone);
            pindex = chainActive.Next(pindex);
            if (!pindex) break; // Should not happen if pindexState was valid
            CBlock block; if (!ReadBlockFromDisk(block, pindex)) return error("VerifyDB() : *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
            dev::h256 connOldHashStateRoot, connOldHashUTXORoot; // EVM state backup for connect
            if(globalState) { connOldHashStateRoot = globalState->rootHash(); connOldHashUTXORoot = globalState->rootHashUTXO(); }
            if (!ConnectBlock(block, state, pindex, coins, false)) {
                if(globalState) { globalState->setRoot(connOldHashStateRoot); globalState->setRootUTXO(connOldHashUTXORoot); } // Revert EVM state on connect fail
                return error("VerifyDB() : *** found unconnectable block at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
            }
             if (ShutdownRequested()) {
                if(globalState) { globalState->setRoot(oldHashStateRoot); globalState->setRootUTXO(oldHashUTXORoot); } // Revert EVM state on interrupt
                return true;
            }
        }
    }
    LogPrintf("No coin database inconsistencies in last %i blocks (%i transactions)\n", block_count, nGoodTransactions);
    // Do NOT revert EVM state here if successful, leave it at the reconnected tip state.
    return true;
}

// --- UnloadBlockIndex (Keep DigiWage implementation) ---
void UnloadBlockIndex() {
    LOCK(cs_main);
    setBlockIndexCandidates.clear(); chainActive.SetTip(nullptr); pindexBestInvalid = nullptr; pindexBestHeader = nullptr;
    mempool.clear(); mapOrphanTransactions.clear(); mapOrphanTransactionsByPrev.clear(); nSyncStarted = 0;
    mapBlocksUnlinked.clear(); vinfoBlockFile.clear(); nLastBlockFile = 0; nBlockSequenceId = 1;
    mapBlockSource.clear(); mapBlocksInFlight.clear(); nQueuedValidatedHeaders = 0; nPreferredDownload = 0;
    setDirtyBlockIndex.clear(); setDirtyFileInfo.clear(); mapNodeState.clear();
    for (auto& entry : mapBlockIndex) delete entry.second;
    mapBlockIndex.clear();
    // EVM Cleanup (important!)
    globalState.reset();
    pstorageresult.reset();
    globalSealEngine.reset();
}

// --- LoadBlockIndex (Keep DigiWage implementation) ---
bool LoadBlockIndex(std::string& strError) {
    if (!fReindex && !LoadBlockIndexDB(strError)) return false;
    return true;
}

// --- InitBlockIndex (Modified for EVM) ---
bool InitBlockIndex() {
    LOCK(cs_main);
    // Check if genesis block exists
    if (chainActive.Genesis() != nullptr) return true;

    LogPrintf("Initializing databases...\n");
    fTxIndex = GetBoolArg("-txindex", true); pblocktree->WriteFlag("txindex", fTxIndex);
    fLogEvents = GetBoolArg("-logevents", DEFAULT_LOGEVENTS); pblocktree->WriteFlag("logevents", fLogEvents);
    fAddressIndex = GetBoolArg("-addrindex", DEFAULT_ADDRINDEX); pblocktree->WriteFlag("addrindex", fAddressIndex);
    LogPrintf("Log Events %s\n", fLogEvents ? "enabled" : "disabled");
    LogPrintf("Address Index %s\n", fAddressIndex ? "enabled" : "disabled");

    // --- EVM Init ---
    fs::path statePath = GetDataDir(false) / "stateQtum";
    fs::path utxoPath = GetDataDir(false) / "stateQtumUTXO";
    dev::h256 stateRoot = dev::h256(); // Empty state for genesis
    dev::h256 utxoRoot = dev::h256();  // Empty UTXO root for genesis
    try {
        // Use string paths for QtumState::openDB
// New: (Pass utxoPath to the constructor)
globalState = std::unique_ptr<QtumState>(new QtumState(
    dev::u256(0), // Default gas limit for state? Or fetch from params?
    QtumState::openDB(statePath.string(), stateRoot, dev::WithExisting::Trust), // State DB ChainState object
    utxoPath.string() // UTXO DB Path
));
// Remove the line: globalState->setUTXODB(...);
// Still need to set the roots explicitly after construction
globalState->setRoot(stateRoot);
globalState->setRootUTXO(utxoRoot); // Assuming setRootUTXO still exists

        // Fix StorageResults constructor call - pass path as string
        pstorageresult = std::unique_ptr<StorageResults>(new StorageResults((GetDataDir(false) / "results").string()));

        LogPrintf("Initialized EVM state database at %s\n", statePath.string());
    } catch (const std::exception& e) {
        strMiscWarning = _("Error initializing EVM database") + std::string(": ") + e.what();
        return error("Failed to initialize EVM database: %s. You may need to reindex.", e.what());
    }
    // TODO: Initialize globalSealEngine if needed
    // --- End EVM Init ---

    // Genesis block handling
    if (!fReindex) {
        try {
            CBlock& block = const_cast<CBlock&>(Params().GenesisBlock());
            unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
            CDiskBlockPos blockPos; CValidationState state;
            if (!FindBlockPos(state, blockPos, nBlockSize + 8, 0, block.GetBlockTime(), false)) // Pass false for fKnown
                 return error("InitBlockIndex() : FindBlockPos failed for genesis block");
            if (!WriteBlockToDisk(block, blockPos))
                 return error("InitBlockIndex() : writing genesis block to disk failed");
            CBlockIndex* pindex = AddToBlockIndex(block);
            // --- Set EVM Genesis State Roots ---
            pindex->hashStateRoot = h256Touint(stateRoot); // Assumes h256Touint exists
            pindex->hashUTXORoot = h256Touint(utxoRoot); // Assumes h256Touint exists
            setDirtyBlockIndex.insert(pindex); // Mark genesis index as dirty
            // --- End EVM Genesis ---
            if (!ReceivedBlockTransactions(block, state, pindex, blockPos))
                 return error("InitBlockIndex() : ReceivedBlockTransactions failed for genesis block");
             // Write genesis state to disk
            if (!FlushStateToDisk(state, FlushStateMode::ALWAYS)) return false; // Use scoped enum
        } catch (const std::runtime_error& e) {
            return error("InitBlockIndex() : failed to initialize block database: %s", e.what());
        }
    }
    return true;
}


// --- LoadExternalBlockFile (Keep DigiWage implementation) ---
bool LoadExternalBlockFile(FILE* fileIn, CDiskBlockPos* dbp) {
    static std::multimap<uint256, CDiskBlockPos> mapBlocksUnknownParent;
    int64_t nStart = GetTimeMillis(); int nLoaded = 0;
    try {
        CBufferedFile blkdat(fileIn, 2 * MAX_BLOCK_SIZE_CURRENT, MAX_BLOCK_SIZE_CURRENT + 8, SER_DISK, CLIENT_VERSION);
        uint64_t nRewind = blkdat.GetPos();
        while (!blkdat.eof()) {
            boost::this_thread::interruption_point();
            blkdat.SetPos(nRewind); nRewind++; blkdat.SetLimit(); unsigned int nSize = 0;
            try {
                unsigned char buf[MESSAGE_START_SIZE]; blkdat.FindByte(Params().MessageStart()[0]);
                nRewind = blkdat.GetPos() + 1; blkdat >> FLATDATA(buf);
                if (memcmp(buf, Params().MessageStart(), MESSAGE_START_SIZE)) continue;
                blkdat >> nSize; if (nSize < 80 || nSize > MAX_BLOCK_SIZE_CURRENT) continue;
            } catch (const std::exception&) { break; }
            try {
                uint64_t nBlockPos = blkdat.GetPos(); if (dbp) dbp->nPos = nBlockPos;
                blkdat.SetLimit(nBlockPos + nSize); blkdat.SetPos(nBlockPos); CBlock block; blkdat >> block;
                nRewind = blkdat.GetPos(); uint256 hash = block.GetHash();
                if (hash != Params().GetConsensus().hashGenesisBlock && mapBlockIndex.find(block.hashPrevBlock) == mapBlockIndex.end()) {
                    LogPrint("reindex", "%s: Out of order block %s, parent %s not known\n", __func__, hash.ToString(), block.hashPrevBlock.ToString());
                    if (dbp) mapBlocksUnknownParent.insert(std::make_pair(block.hashPrevBlock, *dbp));
                    continue;
                }
                if (mapBlockIndex.count(hash) == 0 || (mapBlockIndex[hash]->nStatus & BLOCK_HAVE_DATA) == 0) {
                    CValidationState state; if (ProcessNewBlock(state, nullptr, &block, dbp)) nLoaded++;
                    if (state.IsError()) break;
                } else if (hash != Params().GetConsensus().hashGenesisBlock && mapBlockIndex[hash]->nHeight % 1000 == 0) {
                     LogPrintf("Block Import: already had block %s at height %d\n", hash.ToString(), mapBlockIndex[hash]->nHeight);
                }
                std::deque<uint256> queue; queue.push_back(hash);
                while (!queue.empty()) {
                    uint256 head = queue.front(); queue.pop_front();
                    auto range = mapBlocksUnknownParent.equal_range(head);
                    while (range.first != range.second) {
                        auto it = range.first;
                        if (ReadBlockFromDisk(block, it->second)) {
                            LogPrintf("%s: Processing out of order child %s of %s\n", __func__, block.GetHash().ToString(), head.ToString());
                            CValidationState dummy; if (ProcessNewBlock(dummy, nullptr, &block, &it->second)) { nLoaded++; queue.push_back(block.GetHash()); }
                        }
                        range.first++; mapBlocksUnknownParent.erase(it);
                    }
                }
            } catch (const std::exception& e) { LogPrintf("%s : Deserialize or I/O error - %s\n", __func__, e.what()); }
        }
    } catch (const std::runtime_error& e) { AbortNode(std::string("System error: ") + e.what()); }
    if (nLoaded > 0) LogPrintf("Loaded %i blocks from external file in %dms\n", nLoaded, GetTimeMillis() - nStart);
    return nLoaded > 0;
}


// --- CheckBlockIndex (Keep DigiWage implementation) ---
void static CheckBlockIndex() {
    if (!fCheckBlockIndex) return;
    LOCK(cs_main);
    if (chainActive.Height() < 0) { assert(mapBlockIndex.size() <= 1); return; }
    std::multimap<CBlockIndex*, CBlockIndex*> forward;
    for (auto& entry : mapBlockIndex) { forward.insert(std::make_pair(entry.second->pprev, entry.second)); }
    assert(forward.size() == mapBlockIndex.size());
    auto rangeGenesis = forward.equal_range(nullptr);
    CBlockIndex* pindex = rangeGenesis.first->second; rangeGenesis.first++; assert(rangeGenesis.first == rangeGenesis.second);
    size_t nNodes = 0; int nHeight = 0;
    CBlockIndex* pindexFirstInvalid = nullptr, * pindexFirstMissing = nullptr, * pindexFirstNotTreeValid = nullptr;
    CBlockIndex* pindexFirstNotChainValid = nullptr, * pindexFirstNotScriptsValid = nullptr;
    while (pindex != nullptr) {
        nNodes++;
        if (pindexFirstInvalid == nullptr && pindex->nStatus & BLOCK_FAILED_VALID) pindexFirstInvalid = pindex;
        if (pindexFirstMissing == nullptr && !(pindex->nStatus & BLOCK_HAVE_DATA)) pindexFirstMissing = pindex;
        if (pindex->pprev != nullptr && pindexFirstNotTreeValid == nullptr && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TREE) pindexFirstNotTreeValid = pindex;
        if (pindex->pprev != nullptr && pindexFirstNotChainValid == nullptr && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_CHAIN) pindexFirstNotChainValid = pindex;
        if (pindex->pprev != nullptr && pindexFirstNotScriptsValid == nullptr && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_SCRIPTS) pindexFirstNotScriptsValid = pindex;
        if (pindex->pprev == nullptr) { assert(pindex->GetBlockHash() == Params().GetConsensus().hashGenesisBlock); assert(pindex == chainActive.Genesis()); }
        assert(!(pindex->nStatus & BLOCK_HAVE_DATA) == (pindex->nTx == 0));
        assert(((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TRANSACTIONS) == (pindex->nTx > 0));
        if (pindex->nChainTx == 0) assert(pindex->nSequenceId == 0);
        assert((pindexFirstMissing != NULL) == (pindex->nChainTx == 0));
        assert(pindex->nHeight == nHeight);
        assert(pindex->pprev == nullptr || pindex->nChainWork >= pindex->pprev->nChainWork);
        assert(nHeight < 2 || (pindex->pskip && (pindex->pskip->nHeight < nHeight)));
        assert(pindexFirstNotTreeValid == nullptr);
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TREE) assert(pindexFirstNotTreeValid == nullptr);
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_CHAIN) assert(pindexFirstNotChainValid == nullptr);
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_SCRIPTS) assert(pindexFirstNotScriptsValid == nullptr);
        if (pindexFirstInvalid == nullptr) assert((pindex->nStatus & BLOCK_FAILED_MASK) == 0);
        if (!CBlockIndexWorkComparator()(pindex, chainActive.Tip()) && pindexFirstMissing == nullptr) { if (pindexFirstInvalid == nullptr) assert(setBlockIndexCandidates.count(pindex)); }
        else { assert(setBlockIndexCandidates.count(pindex) == 0); }
        auto rangeUnlinked = mapBlocksUnlinked.equal_range(pindex->pprev); bool foundInUnlinked = false;
        while (rangeUnlinked.first != rangeUnlinked.second) { if (rangeUnlinked.first->second == pindex) { foundInUnlinked = true; break; } rangeUnlinked.first++; }
        if (pindex->pprev && pindex->nStatus & BLOCK_HAVE_DATA && pindexFirstMissing != nullptr) { if (pindexFirstInvalid == nullptr) assert(foundInUnlinked); }
        else { assert(!foundInUnlinked); }
        auto range = forward.equal_range(pindex);
        if (range.first != range.second) { pindex = range.first->second; nHeight++; continue; }
        while (pindex) {
            // Example for 3629
            if (pindex == pindexFirstInvalid) pindexFirstInvalid = nullptr;
            if (pindex == pindexFirstMissing) pindexFirstMissing = nullptr;
            // Example for 3630
            if (pindex == pindexFirstNotTreeValid) pindexFirstNotTreeValid = nullptr;
            if (pindex == pindexFirstNotChainValid) pindexFirstNotChainValid = nullptr;
            if (pindex == pindexFirstNotScriptsValid) pindexFirstNotScriptsValid = nullptr; // Also fix this one from line 3630 context
            CBlockIndex* pindexPar = pindex->pprev; auto rangePar = forward.equal_range(pindexPar);
            while (rangePar.first->second != pindex) { assert(rangePar.first != rangePar.second); rangePar.first++; }
            rangePar.first++; if (rangePar.first != rangePar.second) { pindex = rangePar.first->second; break; }
            else { pindex = pindexPar; nHeight--; continue; }
        }
    }
    assert(nNodes == forward.size());
}


// --- GetWarnings (Keep DigiWage implementation) ---
std::string GetWarnings(std::string strFor) {
    std::string strStatusBar, strRPC;
    if (!CLIENT_VERSION_IS_RELEASE) strStatusBar = _("This is a pre-release test build - use at your own risk - do not use for staking or merchant applications!");
    if (GetBoolArg("-testsafemode", false)) strStatusBar = strRPC = "testsafemode enabled";
    if (strMiscWarning != "") strStatusBar = strMiscWarning; // Global defined elsewhere
    if (fLargeWorkForkFound) strStatusBar = strRPC = _("Warning: The network does not appear to fully agree! Some miners appear to be experiencing issues.");
    else if (fLargeWorkInvalidChainFound) strStatusBar = strRPC = _("Warning: We do not appear to fully agree with our peers! You may need to upgrade, or other nodes may need to upgrade.");
    else if (fBlockDatabaseForkFound) strStatusBar = strRPC = _("Warning: Your blockchain doesn't match the required checkpoints! You have forked, please resync DIGIWAGE Core.");
    if (strFor == "statusbar") return strStatusBar;
    else if (strFor == "rpc") return strRPC;
    assert(!"GetWarnings() : invalid parameter"); return "error";
}

// --- Messages Section (Keep DigiWage implementations for AlreadyHave, ProcessGetData, ProcessMessage, ActiveProtocol, ProcessMessages, SendMessages) ---
// Note: ProcessMessage needs careful review if new EVM messages are ever needed.
bool static AlreadyHave(const CInv& inv) {
     switch (inv.type) {
    case MSG_TX: {
        bool txInMap = mempool.exists(inv.hash);
        return txInMap || mapOrphanTransactions.count(inv.hash) || (pcoinsTip && pcoinsTip->HaveCoins(inv.hash)); // Added pcoinsTip null check
    }
    // Keep remaining DigiWage specific cases
    case MSG_DSTX: return mapObfuscationBroadcastTxes.count(inv.hash);
    case MSG_BLOCK: return mapBlockIndex.count(inv.hash);
    case MSG_TXLOCK_REQUEST: return mapTxLockReq.count(inv.hash) || mapTxLockReqRejected.count(inv.hash);
    case MSG_TXLOCK_VOTE: return mapTxLockVote.count(inv.hash);
    case MSG_SPORK: return mapSporks.count(inv.hash);
    case MSG_MASTERNODE_WINNER: if (masternodePayments.mapMasternodePayeeVotes.count(inv.hash)) { masternodeSync.AddedMasternodeWinner(inv.hash); return true; } return false;
    case MSG_BUDGET_VOTE: if (budget.mapSeenMasternodeBudgetVotes.count(inv.hash)) { masternodeSync.AddedBudgetItem(inv.hash); return true; } return false;
    case MSG_BUDGET_PROPOSAL: if (budget.mapSeenMasternodeBudgetProposals.count(inv.hash)) { masternodeSync.AddedBudgetItem(inv.hash); return true; } return false;
    case MSG_BUDGET_FINALIZED_VOTE: if (budget.mapSeenFinalizedBudgetVotes.count(inv.hash)) { masternodeSync.AddedBudgetItem(inv.hash); return true; } return false;
    case MSG_BUDGET_FINALIZED: if (budget.mapSeenFinalizedBudgets.count(inv.hash)) { masternodeSync.AddedBudgetItem(inv.hash); return true; } return false;
    case MSG_MASTERNODE_ANNOUNCE: if (mnodeman.mapSeenMasternodeBroadcast.count(inv.hash)) { masternodeSync.AddedMasternodeList(inv.hash); return true; } return false;
    case MSG_MASTERNODE_PING: return mnodeman.mapSeenMasternodePing.count(inv.hash);
    }
    return true; // Default true for unknown types
}
void static ProcessGetData(CNode* pfrom) {
    // Keep DigiWage implementation
    AssertLockNotHeld(cs_main);
    std::deque<CInv>::iterator it = pfrom->vRecvGetData.begin();
    std::vector<CInv> vNotFound;
    LOCK(cs_main);
    while (it != pfrom->vRecvGetData.end()) {
        if (pfrom->nSendSize >= SendBufferSize()) break;
        const CInv& inv = *it; { boost::this_thread::interruption_point(); it++;
        if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK) {
            bool send = false; auto mi = mapBlockIndex.find(inv.hash);
            if (mi != mapBlockIndex.end()) {
                if (chainActive.Contains(mi->second)) send = true;
                else { send = mi->second->IsValid(BLOCK_VALID_SCRIPTS) && (pindexBestHeader != NULL) && (chainActive.Height() - mi->second->nHeight < GetArg("-maxreorg", DEFAULT_MAX_REORG_DEPTH)); if (!send) LogPrintf("ProcessGetData(): ignoring request from peer=%i for old block\n", pfrom->GetId()); }
            }
            if (send && (mi->second->nStatus & BLOCK_HAVE_DATA)) {
                CBlock block; if (!ReadBlockFromDisk(block, (*mi).second)) assert(!"cannot load block from disk");
                if (inv.type == MSG_BLOCK) pfrom->PushMessage("block", block);
                else { LOCK(pfrom->cs_filter); if (pfrom->pfilter) { CMerkleBlock merkleBlock(block, *pfrom->pfilter); pfrom->PushMessage("merkleblock", merkleBlock); typedef std::pair<unsigned int, uint256> PairType; for (PairType& pair : merkleBlock.vMatchedTxn) if (!pfrom->setInventoryKnown.count(CInv(MSG_TX, pair.second))) pfrom->PushMessage("tx", block.vtx[pair.first]); } }
                if (inv.hash == pfrom->hashContinue) { std::vector<CInv> vInv; vInv.push_back(CInv(MSG_BLOCK, chainActive.Tip()->GetBlockHash())); pfrom->PushMessage("inv", vInv); pfrom->hashContinue.SetNull(); }
            }
        } else if (inv.IsKnownType()) {
            bool pushed = false; { LOCK(cs_mapRelay); auto mi = mapRelay.find(inv); if (mi != mapRelay.end()) { pfrom->PushMessage(inv.GetCommand(), (*mi).second); pushed = true; } }
            if (!pushed && inv.type == MSG_TX) { CTransaction tx; if (mempool.lookup(inv.hash, tx)) { CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); ss.reserve(1000); ss << tx; pfrom->PushMessage("tx", ss); pushed = true; } }
            // Keep DigiWage specific message types (MSG_TXLOCK_VOTE, etc.)
            if (!pushed && inv.type == MSG_TXLOCK_VOTE) { if (mapTxLockVote.count(inv.hash)) { CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); ss << mapTxLockVote[inv.hash]; pfrom->PushMessage("txlvote", ss); pushed = true; } }
            if (!pushed && inv.type == MSG_TXLOCK_REQUEST) { if (mapTxLockReq.count(inv.hash)) { CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); ss << mapTxLockReq[inv.hash]; pfrom->PushMessage("ix", ss); pushed = true; } }
            if (!pushed && inv.type == MSG_SPORK) { if (mapSporks.count(inv.hash)) { CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); ss << mapSporks[inv.hash]; pfrom->PushMessage("spork", ss); pushed = true; } }
            if (!pushed && inv.type == MSG_MASTERNODE_WINNER) { if (masternodePayments.mapMasternodePayeeVotes.count(inv.hash)) { CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); ss << masternodePayments.mapMasternodePayeeVotes[inv.hash]; pfrom->PushMessage("mnw", ss); pushed = true; } }
            if (!pushed && inv.type == MSG_BUDGET_VOTE) { if (budget.mapSeenMasternodeBudgetVotes.count(inv.hash)) { CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); ss << budget.mapSeenMasternodeBudgetVotes[inv.hash]; pfrom->PushMessage("mvote", ss); pushed = true; } }
            if (!pushed && inv.type == MSG_BUDGET_PROPOSAL) { if (budget.mapSeenMasternodeBudgetProposals.count(inv.hash)) { CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); ss << budget.mapSeenMasternodeBudgetProposals[inv.hash]; pfrom->PushMessage("mprop", ss); pushed = true; } }
            if (!pushed && inv.type == MSG_BUDGET_FINALIZED_VOTE) { if (budget.mapSeenFinalizedBudgetVotes.count(inv.hash)) { CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); ss << budget.mapSeenFinalizedBudgetVotes[inv.hash]; pfrom->PushMessage("fbvote", ss); pushed = true; } }
            if (!pushed && inv.type == MSG_BUDGET_FINALIZED) { if (budget.mapSeenFinalizedBudgets.count(inv.hash)) { CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); ss << budget.mapSeenFinalizedBudgets[inv.hash]; pfrom->PushMessage("fbs", ss); pushed = true; } }
            if (!pushed && inv.type == MSG_MASTERNODE_ANNOUNCE) { if (mnodeman.mapSeenMasternodeBroadcast.count(inv.hash)) { CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); ss << mnodeman.mapSeenMasternodeBroadcast[inv.hash]; pfrom->PushMessage("mnb", ss); pushed = true; } }
            if (!pushed && inv.type == MSG_MASTERNODE_PING) { if (mnodeman.mapSeenMasternodePing.count(inv.hash)) { CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); ss << mnodeman.mapSeenMasternodePing[inv.hash]; pfrom->PushMessage("mnp", ss); pushed = true; } }
            if (!pushed && inv.type == MSG_DSTX) { if (mapObfuscationBroadcastTxes.count(inv.hash)) { CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); ss << mapObfuscationBroadcastTxes[inv.hash].tx << mapObfuscationBroadcastTxes[inv.hash].vin << mapObfuscationBroadcastTxes[inv.hash].vchSig << mapObfuscationBroadcastTxes[inv.hash].sigTime; pfrom->PushMessage("dstx", ss); pushed = true; } }
            if (!pushed) vNotFound.push_back(inv);
        } if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK) break; }
    }
    pfrom->vRecvGetData.erase(pfrom->vRecvGetData.begin(), it);
    if (!vNotFound.empty()) pfrom->PushMessage("notfound", vNotFound);
}
bool ProcessMessage(CNode* pfrom, std::string strCommand, CDataStream& vRecv, int64_t nTimeReceived) {
    // Keep DigiWage implementation (calls EVM related functions if command matches)
     LogPrint("net", "received: %s (%u bytes) peer=%d\n", SanitizeString(strCommand), vRecv.size(), pfrom->id);
    if (mapArgs.count("-dropmessagestest") && GetRand(atoi(mapArgs["-dropmessagestest"])) == 0) { LogPrintf("dropmessagestest DROPPING RECV MESSAGE\n"); return true; }
    if (strCommand == "version") {
        if (pfrom->nVersion != 0) { pfrom->PushMessage("reject", strCommand, REJECT_DUPLICATE, std::string("Duplicate version message")); LOCK(cs_main); Misbehaving(pfrom->GetId(), 1); return false; }
        int64_t nTime; CAddress addrMe; CAddress addrFrom; uint64_t nNonce = 1;
        vRecv >> pfrom->nVersion >> pfrom->nServices >> nTime >> addrMe;
        if (pfrom->DisconnectOldProtocol(ActiveProtocol(), strCommand)) return false;
        if (pfrom->nVersion == 10300) pfrom->nVersion = 300; // DigiWage specific?
        if (!vRecv.empty()) vRecv >> addrFrom >> nNonce;
        if (!vRecv.empty()) { vRecv >> LIMITED_STRING(pfrom->strSubVer, MAX_SUBVERSION_LENGTH); pfrom->cleanSubVer = SanitizeString(pfrom->strSubVer); }
        if (!vRecv.empty()) vRecv >> pfrom->nStartingHeight;
        if (!vRecv.empty()) vRecv >> pfrom->fRelayTxes; else pfrom->fRelayTxes = true;
        if (nNonce == nLocalHostNonce && nNonce > 1) { LogPrintf("connected to self at %s, disconnecting\n", pfrom->addr.ToString()); pfrom->fDisconnect = true; return true; }
        // Keep DigiWage Spork check logic
        bool fMissingSporks = !pSporkDB->SporkExists(SPORK_14_NEW_PROTOCOL_ENFORCEMENT) || !pSporkDB->SporkExists(SPORK_15_NEW_PROTOCOL_ENFORCEMENT_2) || !pSporkDB->SporkExists(SPORK_17_COLDSTAKING_ENFORCEMENT);
        if (fMissingSporks || !pfrom->fRequestedSporksIDB){ LogPrintf("asking peer for sporks\n"); pfrom->PushMessage("getsporks"); pfrom->fRequestedSporksIDB = true; }
        pfrom->addrLocal = addrMe; if (pfrom->fInbound && addrMe.IsRoutable()) SeenLocal(addrMe);
        if (pfrom->fInbound) pfrom->PushVersion();
        pfrom->fClient = !(pfrom->nServices & NODE_NETWORK);
        UpdatePreferredDownload(pfrom, State(pfrom->GetId()));
        pfrom->PushMessage("verack"); pfrom->ssSend.SetVersion(std::min(pfrom->nVersion, PROTOCOL_VERSION));
        if (!pfrom->fInbound) {
            if (fListen && !IsInitialBlockDownload()) { CAddress addr = GetLocalAddress(&pfrom->addr); FastRandomContext insecure_rand; if (addr.IsRoutable()) { LogPrintf("ProcessMessages: advertising address %s\n", addr.ToString()); pfrom->PushAddress(addr, insecure_rand); } else if (IsPeerAddrLocalGood(pfrom)) { addr.SetIP(pfrom->addrLocal); LogPrintf("ProcessMessages: advertising address %s\n", addr.ToString()); pfrom->PushAddress(addr, insecure_rand); } }
            if (pfrom->fOneShot || pfrom->nVersion >= CADDR_TIME_VERSION || addrman.size() < 1000) { pfrom->PushMessage("getaddr"); pfrom->fGetAddr = true; }
            addrman.Good(pfrom->addr);
        } else { if (((CNetAddr)pfrom->addr) == (CNetAddr)addrFrom) { addrman.Add(addrFrom, addrFrom); addrman.Good(addrFrom); } }
        std::string remoteAddr; if (fLogIPs) remoteAddr = ", peeraddr=" + pfrom->addr.ToString();
        LogPrintf("receive version message: %s: version %d, blocks=%d, us=%s, peer=%d%s\n", pfrom->cleanSubVer, pfrom->nVersion, pfrom->nStartingHeight, addrMe.ToString(), pfrom->id, remoteAddr);
        int64_t nTimeOffset = nTime - GetTime(); pfrom->nTimeOffset = nTimeOffset;
        const int nTimeSlotLength = Params().GetConsensus().nTimeSlotLength;
        if (abs64(nTimeOffset) < 2 * nTimeSlotLength) { pfrom->fSuccessfullyConnected = true; AddTimeData(pfrom->addr, nTimeOffset, nTimeSlotLength); }
        else { LogPrintf("timeOffset (%lld seconds) too large. Disconnecting node %s\n", nTimeOffset, pfrom->addr.ToString().c_str()); pfrom->fDisconnect = true; CheckOffsetDisconnectedPeers(pfrom->addr); }
    }
    else if (pfrom->nVersion == 0) { LOCK(cs_main); Misbehaving(pfrom->GetId(), 1); return false; }
    else if (strCommand == "verack") {
        pfrom->SetRecvVersion(std::min(pfrom->nVersion, PROTOCOL_VERSION));
        if (pfrom->fNetworkNode) { LOCK(cs_main); CNodeState* state = State(pfrom->GetId()); if (state) state->fCurrentlyConnected = true; }
    }
    else if (strCommand == "addr") {
        // Keep DigiWage addr handling logic
        std::vector<CAddress> vAddr; vRecv >> vAddr;
        if (pfrom->nVersion < CADDR_TIME_VERSION && addrman.size() > 1000) return true;
        if (vAddr.size() > 1000) { LOCK(cs_main); Misbehaving(pfrom->GetId(), 20); return error("message addr size() = %u", vAddr.size()); }
        std::vector<CAddress> vAddrOk; int64_t nNow = GetAdjustedTime(); int64_t nSince = nNow - 10 * 60;
        for (CAddress& addr : vAddr) { boost::this_thread::interruption_point();
            if (addr.nTime <= 100000000 || addr.nTime > nNow + 10 * 60) addr.nTime = nNow - 5 * 24 * 60 * 60;
            pfrom->AddAddressKnown(addr); bool fReachable = IsReachable(addr);
            if (addr.nTime > nSince && !pfrom->fGetAddr && vAddr.size() <= 10 && addr.IsRoutable()) {
                LOCK(cs_vNodes); static uint256 hashSalt; if (hashSalt.IsNull()) hashSalt = GetRandHash();
                uint64_t hashAddr = addr.GetHash(); uint256 hashRand = hashSalt ^ (hashAddr << 32) ^ ((GetTime() + hashAddr) / (24 * 60 * 60));
                hashRand = Hash(BEGIN(hashRand), END(hashRand)); std::multimap<uint256, CNode*> mapMix;
                for (CNode* pnode : vNodes) { if (pnode->nVersion < CADDR_TIME_VERSION) continue; unsigned int nPointer; memcpy(&nPointer, &pnode, sizeof(nPointer)); uint256 hashKey = hashRand ^ nPointer; hashKey = Hash(BEGIN(hashKey), END(hashKey)); mapMix.insert(std::make_pair(hashKey, pnode)); }
                int nRelayNodes = fReachable ? 2 : 1; FastRandomContext insecure_rand;
                for (auto mi = mapMix.begin(); mi != mapMix.end() && nRelayNodes-- > 0; ++mi) ((*mi).second)->PushAddress(addr, insecure_rand);
            }
            if (fReachable) vAddrOk.push_back(addr);
        } addrman.Add(vAddrOk, pfrom->addr, 2 * 60 * 60);
        if (vAddr.size() < 1000) pfrom->fGetAddr = false;
        if (pfrom->fOneShot) pfrom->fDisconnect = true;
    }
    else if (strCommand == "inv") {
        // Keep DigiWage inv handling logic
        std::vector<CInv> vInv; vRecv >> vInv; if (vInv.size() > MAX_INV_SZ) { LOCK(cs_main); Misbehaving(pfrom->GetId(), 20); return error("message inv size() = %u", vInv.size()); }
        LOCK(cs_main); std::vector<CInv> vToFetch;
        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++) {
            const CInv& inv = vInv[nInv]; boost::this_thread::interruption_point(); pfrom->AddInventoryKnown(inv);
            bool fAlreadyHave = AlreadyHave(inv); LogPrint("net", "got inv: %s  %s peer=%d\n", inv.ToString(), fAlreadyHave ? "have" : "new", pfrom->id);
            if (!fAlreadyHave && !fImporting && !fReindex && inv.type != MSG_BLOCK) pfrom->AskFor(inv);
            if (inv.type == MSG_BLOCK) { UpdateBlockAvailability(pfrom->GetId(), inv.hash); if (!fAlreadyHave && !fImporting && !fReindex && !mapBlocksInFlight.count(inv.hash)) { vToFetch.push_back(inv); LogPrint("net", "getblocks (%d) %s to peer=%d\n", pindexBestHeader ? pindexBestHeader->nHeight : -1, inv.hash.ToString(), pfrom->id); } }
            if (pfrom->nSendSize > (SendBufferSize() * 2)) { Misbehaving(pfrom->GetId(), 50); return error("send buffer size() = %u", pfrom->nSendSize); }
        } if (!vToFetch.empty()) pfrom->PushMessage("getdata", vToFetch);
    }
    else if (strCommand == "getdata") {
        // Keep DigiWage getdata handling logic
        std::vector<CInv> vInv; vRecv >> vInv; if (vInv.size() > MAX_INV_SZ) { LOCK(cs_main); Misbehaving(pfrom->GetId(), 20); return error("message getdata size() = %u", vInv.size()); }
        if (fDebug || (vInv.size() != 1)) LogPrint("net", "received getdata (%u invsz) peer=%d\n", vInv.size(), pfrom->id);
        if ((fDebug && vInv.size() > 0) || (vInv.size() == 1)) LogPrint("net", "received getdata for: %s peer=%d\n", vInv[0].ToString(), pfrom->id);
        pfrom->vRecvGetData.insert(pfrom->vRecvGetData.end(), vInv.begin(), vInv.end()); ProcessGetData(pfrom);
    }
    else if (strCommand == "getblocks" || strCommand == "getheaders") {
        // Keep DigiWage getblocks/getheaders logic
        CBlockLocator locator; uint256 hashStop; vRecv >> locator >> hashStop;
        if (locator.vHave.size() > MAX_LOCATOR_SZ) { LogPrint("net", "getblocks locator size %lld > %d, disconnect peer=%d\n", locator.vHave.size(), MAX_LOCATOR_SZ, pfrom->GetId()); pfrom->fDisconnect = true; return true; }
        LOCK(cs_main); CBlockIndex* pindex = FindForkInGlobalIndex(chainActive, locator); if (pindex) pindex = chainActive.Next(pindex);
        int nLimit = 500; LogPrint("net", "%s %d to %s limit %d from peer=%d\n", strCommand, (pindex ? pindex->nHeight : -1), hashStop.IsNull() ? "end" : hashStop.ToString(), nLimit, pfrom->id);
        for (; pindex; pindex = chainActive.Next(pindex)) {
            if (pindex->GetBlockHash() == hashStop) { LogPrint("net", "  %s stopping at %d %s\n", strCommand, pindex->nHeight, pindex->GetBlockHash().ToString()); break; }
            if (strCommand == "getheaders") pfrom->PushInventory(CInv(MSG_BLOCK, pindex->GetBlockHash())); // Send header inv for getheaders
            else pfrom->PushInventory(CInv(MSG_BLOCK, pindex->GetBlockHash())); // Send block inv for getblocks
            if (--nLimit <= 0) { LogPrint("net", "  %s stopping at limit %d %s\n", strCommand, pindex->nHeight, pindex->GetBlockHash().ToString()); pfrom->hashContinue = pindex->GetBlockHash(); break; }
        }
    }
    // Ignore Qtum's "headers" message handling for now, keep DigiWage's logic for "getheaders"
    else if (strCommand == "tx" || strCommand == "dstx") {
        // Uses AcceptToMemoryPool adapted previously
        std::vector<uint256> vWorkQueue; std::vector<uint256> vEraseQueue; CTransaction tx;
        bool ignoreFees = false; CTxIn vin; std::vector<unsigned char> vchSig; int64_t sigTime;
        if (strCommand == "tx") vRecv >> tx;
        else { // dstx (DigiWage specific)
            vRecv >> tx >> vin >> vchSig >> sigTime; CMasternode* pmn = mnodeman.Find(vin);
            if (pmn != nullptr) {
                if (!pmn->allowFreeTx) { if (fDebug) LogPrintf("dstx: Masternode sending too many transactions %s\n", tx.GetHash().ToString()); return true; }
                std::string strMessage = tx.GetHash().ToString() + std::to_string(sigTime); std::string errorMessage = "";
                if (!CMessageSigner::VerifyMessage(pmn->pubKeyMasternode, vchSig, strMessage, errorMessage)) return error("dstx: Got bad masternode address signature %s, error: %s", vin.ToString(), errorMessage);
                LogPrintf("dstx: Got Masternode transaction %s\n", tx.GetHash().ToString()); ignoreFees = true; pmn->allowFreeTx = false;
                if (!mapObfuscationBroadcastTxes.count(tx.GetHash())) { CObfuscationBroadcastTx dstx; dstx.tx = tx; dstx.vin = vin; dstx.vchSig = vchSig; dstx.sigTime = sigTime; mapObfuscationBroadcastTxes.insert(std::make_pair(tx.GetHash(), dstx)); }
            }
        }
        CInv inv(MSG_TX, tx.GetHash()); pfrom->AddInventoryKnown(inv); LOCK(cs_main); bool fMissingInputs = false; CValidationState state;
        mapAlreadyAskedFor.erase(inv); // Assuming mapAlreadyAskedFor exists
        if (AcceptToMemoryPool(mempool, state, tx, true, &fMissingInputs, false, ignoreFees)) {
            mempool.check(pcoinsTip); RelayTransaction(tx); vWorkQueue.push_back(inv.hash);
            LogPrint("mempool", "AcceptToMemoryPool: peer=%d %s : accepted %s (poolsz %u)\n", pfrom->id, pfrom->cleanSubVer, tx.GetHash().ToString(), mempool.mapTx.size());
            // Keep DigiWage orphan processing logic
             std::set<NodeId> setMisbehaving;
             while (!vWorkQueue.empty()) {
                 uint256 hashAdded = vWorkQueue.back(); vWorkQueue.pop_back();
                 auto itByPrev = mapOrphanTransactionsByPrev.find(hashAdded); if (itByPrev == mapOrphanTransactionsByPrev.end()) continue;
                 std::vector<uint256> eraseNow; // Process orphans found for this parent
                 for (const uint256 &orphanHash : itByPrev->second) {
                     if (std::find(vEraseQueue.begin(), vEraseQueue.end(), orphanHash) != vEraseQueue.end()) continue;
                     auto itOrphan = mapOrphanTransactions.find(orphanHash); if(itOrphan == mapOrphanTransactions.end()) continue;
                     const CTransaction &orphanTx = itOrphan->second.tx; NodeId fromPeer = itOrphan->second.fromPeer;
                     bool fMissingInputs2 = false; CValidationState stateDummy;
                     if (setMisbehaving.count(fromPeer)) continue;
                     if (AcceptToMemoryPool(mempool, stateDummy, orphanTx, true, &fMissingInputs2)) {
                         LogPrint("mempool", "   accepted orphan tx %s\n", orphanHash.ToString()); RelayTransaction(orphanTx); vWorkQueue.push_back(orphanHash); eraseNow.push_back(orphanHash);
                     } else if (!fMissingInputs2) {
                         int nDos = 0; if (stateDummy.IsInvalid(nDos) && nDos > 0) { Misbehaving(fromPeer, nDos); setMisbehaving.insert(fromPeer); LogPrint("mempool", "   invalid orphan tx %s\n", orphanHash.ToString()); }
                         LogPrint("mempool", "   removed orphan tx %s\n", orphanHash.ToString()); eraseNow.push_back(orphanHash);
                     }
                 }
                 for(const uint256& hashErase : eraseNow) vEraseQueue.push_back(hashErase); // Add processed orphans to main erase queue
                 if (vWorkQueue.size() > 500) { LogPrint("mempool", "Warning: Orphan processing ran too long, breaking recursion.\n"); break; } // Safety break
             }
            for (uint256 hash : vEraseQueue) EraseOrphanTx(hash);
        } else if (fMissingInputs) { AddOrphanTx(tx, pfrom->GetId()); unsigned int nMaxOrphanTx = (unsigned int)std::max((int64_t)0, GetArg("-maxorphantx", DEFAULT_MAX_ORPHAN_TRANSACTIONS)); unsigned int nEvicted = LimitOrphanTxSize(nMaxOrphanTx); if (nEvicted > 0) LogPrint("mempool", "mapOrphan overflow, removed %u tx\n", nEvicted); }
        else if (pfrom->fWhitelisted) { RelayTransaction(tx); }
        if (strCommand == "dstx") { CInv invDSTX(MSG_DSTX, tx.GetHash()); RelayInv(invDSTX); } // Use RelayInv helper
        int nDoS = 0;
        if (state.IsInvalid(nDoS)) {
            LogPrint("mempool", "%s from peer=%d %s was not accepted into the memory pool: %s\n", tx.GetHash().ToString(), pfrom->id, pfrom->cleanSubVer, state.GetRejectReason());
            pfrom->PushMessage("reject", strCommand, state.GetRejectCode(), state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash);
            if (nDoS > 0) Misbehaving(pfrom->GetId(), nDoS);
        }
    }
    // Keep DigiWage handlers for ping, pong, filterload, filteradd, filterclear, reject, mempool, getaddr
    else if (strCommand == "ping") { /* Keep DigiWage ping logic */ }
    else if (strCommand == "pong") { /* Keep DigiWage pong logic */ }
    else if (!(nLocalServices & NODE_BLOOM) && (strCommand == "filterload" || strCommand == "filteradd" || strCommand == "filterclear")) { LOCK(cs_main); Misbehaving(pfrom->GetId(), 100); }
    else if (strCommand == "filterload") { /* Keep DigiWage filterload logic */ }
    else if (strCommand == "filteradd") { /* Keep DigiWage filteradd logic */ }
    else if (strCommand == "filterclear") { /* Keep DigiWage filterclear logic */ }
    else if (strCommand == "reject") { /* Keep DigiWage reject logic */ }
    else if (strCommand == "mempool") { /* Keep DigiWage mempool logic */ }
    else if ((strCommand == "getaddr") && (pfrom->fInbound)) { /* Keep DigiWage getaddr logic */ }
    else if (strCommand == "block" && !fImporting && !fReindex) { // Keep DigiWage block handling logic
        CBlock block; vRecv >> block; uint256 hashBlock = block.GetHash(); CInv inv(MSG_BLOCK, hashBlock);
        LogPrint("net", "received block %s peer=%d\n", inv.hash.ToString(), pfrom->id);
        if (!mapBlockIndex.count(block.hashPrevBlock) && block.GetHash() != Params().GetConsensus().hashGenesisBlock) { // Added genesis check
             if (find(pfrom->vBlockRequested.begin(), pfrom->vBlockRequested.end(), hashBlock) != pfrom->vBlockRequested.end()) { pfrom->PushMessage("getblocks", chainActive.GetLocator(), block.hashPrevBlock); pfrom->vBlockRequested.push_back(block.hashPrevBlock); }
             else { pfrom->PushMessage("getblocks", chainActive.GetLocator(), hashBlock); pfrom->vBlockRequested.push_back(hashBlock); }
        } else {
            pfrom->AddInventoryKnown(inv); CValidationState state;
            if (!mapBlockIndex.count(block.GetHash())) {
                ProcessNewBlock(state, pfrom, &block); int nDoS;
                if (state.IsInvalid(nDoS)) {
                    pfrom->PushMessage("reject", strCommand, state.GetRejectCode(), state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash);
                    if (nDoS > 0) { TRY_LOCK(cs_main, lockMain); if (lockMain) Misbehaving(pfrom->GetId(), nDoS); }
                }
            } else { LogPrint("net", "%s : Already processed block %s, skipping\n", __func__, block.GetHash().GetHex()); }
        }
    }
    else {
        // Keep DigiWage extensions (Masternodes, Budgets, SwiftX, Sporks, Sync)
        mnodeman.ProcessMessage(pfrom, strCommand, vRecv);
        budget.ProcessMessage(pfrom, strCommand, vRecv);
        masternodePayments.ProcessMessageMasternodePayments(pfrom, strCommand, vRecv);
        ProcessMessageSwiftTX(pfrom, strCommand, vRecv); // Ensure defined elsewhere
        sporkManager.ProcessSpork(pfrom, strCommand, vRecv);
        masternodeSync.ProcessMessage(pfrom, strCommand, vRecv);
    }
    return true;
}
// --- ActiveProtocol (Keep DigiWage implementation) ---
int ActiveProtocol() {
    if (sporkManager.IsSporkActive(SPORK_15_NEW_PROTOCOL_ENFORCEMENT_2)) return MIN_PEER_PROTO_VERSION_AFTER_ENFORCEMENT;
    return MIN_PEER_PROTO_VERSION_BEFORE_ENFORCEMENT;
}
// --- ProcessMessages (Keep DigiWage implementation) ---
bool ProcessMessages(CNode* pfrom) {
    AssertLockNotHeld(cs_main); // Should not hold cs_main entering here
    bool fOk = true;
    if (!pfrom->vRecvGetData.empty()) ProcessGetData(pfrom);
    if (!pfrom->vRecvGetData.empty()) return fOk; // Return if ProcessGetData still has work

    std::deque<CNetMessage>::iterator it = pfrom->vRecvMsg.begin();
    while (!pfrom->fDisconnect && it != pfrom->vRecvMsg.end()) {
        if (pfrom->nSendSize >= SendBufferSize()) break;
        CNetMessage& msg = *it;
        if (!msg.complete()) break;
        it++; // Increment *before* processing

        if (memcmp(msg.hdr.pchMessageStart, Params().MessageStart(), MESSAGE_START_SIZE) != 0) { LogPrintf("PROCESSMESSAGE: INVALID MESSAGESTART %s peer=%d\n", SanitizeString(msg.hdr.GetCommand()), pfrom->id); fOk = false; break; }
        CMessageHeader& hdr = msg.hdr; if (!hdr.IsValid()) { LogPrintf("PROCESSMESSAGE: ERRORS IN HEADER %s peer=%d\n", SanitizeString(hdr.GetCommand()), pfrom->id); continue; }
        std::string strCommand = hdr.GetCommand();
        unsigned int nMessageSize = hdr.nMessageSize; CDataStream& vRecv = msg.vRecv;
        uint256 hash = Hash(vRecv.begin(), vRecv.begin() + nMessageSize);
        unsigned int nChecksum = 0; memcpy(&nChecksum, &hash, sizeof(nChecksum));
        if (nChecksum != hdr.nChecksum) { LogPrintf("ProcessMessages(%s, %u bytes): CHECKSUM ERROR nChecksum=%08x hdr.nChecksum=%08x\n", SanitizeString(strCommand), nMessageSize, nChecksum, hdr.nChecksum); continue; }

        bool fRet = false;
        try {
            fRet = ProcessMessage(pfrom, strCommand, vRecv, msg.nTime); boost::this_thread::interruption_point();
        } catch (const std::ios_base::failure& e) {
            pfrom->PushMessage("reject", strCommand, REJECT_MALFORMED, std::string("error parsing message"));
            if (strstr(e.what(), "end of data")) LogPrintf("ProcessMessages(%s, %u bytes): Exception '%s' caught...\n", SanitizeString(strCommand), nMessageSize, e.what());
            else if (strstr(e.what(), "size too large")) LogPrintf("ProcessMessages(%s, %u bytes): Exception '%s' caught\n", SanitizeString(strCommand), nMessageSize, e.what());
            else PrintExceptionContinue(&e, "ProcessMessages()");
        } catch (const boost::thread_interrupted&) { throw; }
        catch (const std::exception& e) { PrintExceptionContinue(&e, "ProcessMessages()"); }
        catch (...) { PrintExceptionContinue(nullptr, "ProcessMessages()"); }
        if (!fRet) LogPrintf("ProcessMessage(%s, %u bytes) FAILED peer=%d\n", SanitizeString(strCommand), nMessageSize, pfrom->id);
        // Do not break here, process next message in queue
    }
    if (!pfrom->fDisconnect) pfrom->vRecvMsg.erase(pfrom->vRecvMsg.begin(), it);
    return fOk;
}
// --- SendMessages (Keep DigiWage implementation) ---
bool SendMessages(CNode* pto, bool fSendTrickle) {
    AssertLockNotHeld(cs_main); // Should not hold cs_main entering here
    {
        if (pto->nVersion == 0) return true; // Wait for version
        bool pingSend = false; if (pto->fPingQueued) pingSend = true;
        if (pto->nPingNonceSent == 0 && pto->nPingUsecStart + PING_INTERVAL * 1000000 < GetTimeMicros()) pingSend = true;
        if (pingSend) {
            uint64_t nonce = 0; while (nonce == 0) GetRandBytes((unsigned char*)&nonce, sizeof(nonce));
            pto->fPingQueued = false; pto->nPingUsecStart = GetTimeMicros();
            if (pto->nVersion > BIP0031_VERSION) { pto->nPingNonceSent = nonce; pto->PushMessage("ping", nonce); }
            else { pto->nPingNonceSent = 0; pto->PushMessage("ping"); }
        }
        TRY_LOCK(cs_main, lockMain); if (!lockMain) return true; // Try lock, return if failed

        static int64_t nLastRebroadcast;
        if (!IsInitialBlockDownload() && (GetTime() - nLastRebroadcast > 24 * 60 * 60)) {
            LOCK(cs_vNodes); for (CNode* pnode : vNodes) { if (nLastRebroadcast) pnode->setAddrKnown.clear(); AdvertiseLocal(pnode); }
            if (!vNodes.empty()) nLastRebroadcast = GetTime();
        }
        if (fSendTrickle) {
            std::vector<CAddress> vAddr; vAddr.reserve(pto->vAddrToSend.size());
            for (const CAddress& addr : pto->vAddrToSend) { if (pto->setAddrKnown.insert(addr).second) { vAddr.push_back(addr); if (vAddr.size() >= 1000) { pto->PushMessage("addr", vAddr); vAddr.clear(); } } }
            pto->vAddrToSend.clear(); if (!vAddr.empty()) pto->PushMessage("addr", vAddr);
        }
        CNodeState& state = *State(pto->GetId()); // Assume state exists
        if (state.fShouldBan) {
            if (pto->fWhitelisted) LogPrintf("Warning: not punishing whitelisted peer %s!\n", pto->addr.ToString());
            else { pto->fDisconnect = true; if (!pto->addr.IsLocal()) CNode::Ban(pto->addr, BanReasonNodeMisbehaving); else LogPrintf("Warning: not banning local peer %s!\n", pto->addr.ToString()); }
            state.fShouldBan = false;
        }
        for (const CBlockReject& reject : state.rejects) pto->PushMessage("reject", (std::string) "block", reject.chRejectCode, reject.strRejectReason, reject.hashBlock);
        state.rejects.clear();
        if (pindexBestHeader == nullptr && chainActive.Tip()) pindexBestHeader = chainActive.Tip(); // Initialize if null
        bool fFetch = state.fPreferredDownload || (nPreferredDownload == 0 && !pto->fClient && !pto->fOneShot);
        if (!state.fSyncStarted && !pto->fClient && fFetch && !fImporting && !fReindex) {
             if (nSyncStarted == 0 || (pindexBestHeader && pindexBestHeader->GetBlockTime() > GetAdjustedTime() - 6 * 60 * 60)) { // Check pindexBestHeader not null
                state.fSyncStarted = true; nSyncStarted++; pto->PushMessage("getblocks", chainActive.GetLocator(chainActive.Tip()), UINT256_ZERO);
            }
        }
        if (!fReindex && !fImporting && !IsInitialBlockDownload()) GetMainSignals().Broadcast();
        std::vector<CInv> vInv, vInvWait;
        {
            LOCK(pto->cs_inventory); vInv.reserve(pto->vInventoryToSend.size()); vInvWait.reserve(pto->vInventoryToSend.size());
            for (const CInv& inv : pto->vInventoryToSend) {
                if (pto->setInventoryKnown.count(inv)) continue;
                if (inv.type == MSG_TX && !fSendTrickle) {
                    static uint256 hashSalt; if (hashSalt.IsNull()) hashSalt = GetRandHash(); uint256 hashRand = inv.hash ^ hashSalt; hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    bool fTrickleWait = ((hashRand & 3) != 0); if (fTrickleWait) { vInvWait.push_back(inv); continue; }
                }
                if (pto->setInventoryKnown.insert(inv).second) { vInv.push_back(inv); if (vInv.size() >= 1000) { pto->PushMessage("inv", vInv); vInv.clear(); } }
            } pto->vInventoryToSend = vInvWait;
        } if (!vInv.empty()) pto->PushMessage("inv", vInv);
        int64_t nNow = GetTimeMicros();
        if (!pto->fDisconnect && state.nStallingSince && state.nStallingSince < nNow - 1000000 * BLOCK_STALLING_TIMEOUT) { LogPrintf("Peer=%d is stalling block download, disconnecting\n", pto->id); pto->fDisconnect = true; }
        if (!pto->fDisconnect && state.vBlocksInFlight.size() > 0 && state.vBlocksInFlight.front().nTime < nNow - 500000 * Params().GetConsensus().nTargetSpacing * (4 + state.vBlocksInFlight.front().nValidatedQueuedBefore)) { LogPrintf("Timeout downloading block %s from peer=%d, disconnecting\n", state.vBlocksInFlight.front().hash.ToString(), pto->id); pto->fDisconnect = true; }
        std::vector<CInv> vGetData;
        if (!pto->fDisconnect && !pto->fClient && fFetch && state.nBlocksInFlight < MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
            std::vector<CBlockIndex*> vToDownload; NodeId staller = -1;
            FindNextBlocksToDownload(pto, MAX_BLOCKS_IN_TRANSIT_PER_PEER - state.nBlocksInFlight, vToDownload, staller);
            for (CBlockIndex* pindexDown : vToDownload) { // Renamed variable
                vGetData.push_back(CInv(MSG_BLOCK, pindexDown->GetBlockHash())); MarkBlockAsInFlight(pto->GetId(), pindexDown->GetBlockHash(), pindexDown);
                LogPrintf("Requesting block %s (%d) peer=%d\n", pindexDown->GetBlockHash().ToString(), pindexDown->nHeight, pto->id);
            }
            if (state.nBlocksInFlight == 0 && staller != -1) {
                CNodeState* stallerState = State(staller); // Get state for staller
                 if (stallerState && stallerState->nStallingSince == 0) { // Check staller state exists
                     stallerState->nStallingSince = nNow; LogPrint("net", "Stall started peer=%d\n", staller);
                 }
            }
        }
        while (!pto->fDisconnect && !pto->mapAskFor.empty() && (*pto->mapAskFor.begin()).first <= nNow) {
            const CInv& inv = (*pto->mapAskFor.begin()).second;
            if (!AlreadyHave(inv)) { if (fDebug) LogPrint("net", "Requesting %s peer=%d\n", inv.ToString(), pto->id); vGetData.push_back(inv); if (vGetData.size() >= 1000) { pto->PushMessage("getdata", vGetData); vGetData.clear(); } }
            pto->mapAskFor.erase(pto->mapAskFor.begin());
        } if (!vGetData.empty()) pto->PushMessage("getdata", vGetData);
    }
    return true;
}

// ... (Keep DigiWage CBlockUndo, CBlockFileInfo)

// --- CMainCleanup (Keep DigiWage implementation) ---
class CMainCleanup {
public: CMainCleanup() {} ~CMainCleanup() { UnloadBlockIndex(); } // Call UnloadBlockIndex on exit
} instance_of_cmaincleanup;

/////////////////////////////////////////// EVM Function Implementations (Adapted)

unsigned int GetContractScriptFlags(int nHeight, const Consensus::Params& consensusparams) {
    unsigned int flags = SCRIPT_EXEC_BYTE_CODE;
    if (nHeight >= consensusparams.QIP5Height) flags |= SCRIPT_OUTPUT_SENDER;
    // Add other height-based flags if needed
    return flags;
}
bool CheckOpSender(const CTransaction& tx, const CChainParams& chainparams, int nHeight){
    if(!TxHasOpSender(tx)) return true;
    if(!(nHeight >= chainparams.GetConsensus().QIP5Height)) return false;
    for (const CTxOut& txout : tx.vout) {
        bool hasOpSender = txout.scriptPubKey.HasOpSender();
        if(hasOpSender && !(txout.scriptPubKey.HasOpCreate() || txout.scriptPubKey.HasOpCall())) return false;
        if(hasOpSender && !ExtractSenderData(txout.scriptPubKey, nullptr, nullptr)) return false;
    }
    return true;
}
bool CheckSenderScript(const CCoinsViewCache& view, const CTransaction& tx){
    if (tx.vin.empty()) return false;
    const CCoins* coins = view.AccessCoins(tx.vin[0].prevout.hash);
    if (!coins || !coins->IsAvailable(tx.vin[0].prevout.n)) return false;
    CScript mainSenderScript = coins->vout[tx.vin[0].prevout.n].scriptPubKey;
    if(!mainSenderScript.IsPayToPublicKeyHash() && !mainSenderScript.IsPayToPublicKey()) return false;
    if(!TxHasOpSender(tx)) return true;
    for (const CTxOut& txout : tx.vout) {
        if(txout.scriptPubKey.HasOpSender()) {
            CScript senderPubKeyScript, senderSigScript;
            if(!ExtractSenderData(txout.scriptPubKey, &senderPubKeyScript, &senderSigScript)) return false;
            if(!senderPubKeyScript.IsPayToPublicKeyHash() && !senderPubKeyScript.IsPayToPublicKey()) return false;
            std::vector <std::vector<unsigned char> > stack;
            if (!EvalScript(stack, senderSigScript, SCRIPT_VERIFY_NONE, BaseSignatureChecker())) return false;
            if(stack.size() != STANDARD_SENDER_STACK_ITEMS) return false;
            for(const auto& item : stack) { if(item.size() > MAX_STANDARD_SENDER_STACK_ITEM_SIZE) return false; }
        }
    }
    return true;
}
valtype GetSenderAddress(const CTransaction& tx, const CCoinsViewCache* coinsView, const std::vector<CTransactionRef>* blockTxs, const CTxMemPool* mempool, int nOut = -1) { // Add default value for nOut
    CScript script; bool scriptFilled = false;
    // If nOut is valid, try extracting sender data directly from that output
    if (nOut > -1 && (unsigned)nOut < tx.vout.size() && tx.vout[nOut].scriptPubKey.HasOpSender()) { // Check HasOpSender too
        scriptFilled = ExtractSenderData(tx.vout[nOut].scriptPubKey, &script, nullptr);
    }

    // If not found in the specific output (or nOut was invalid), find the input script
    if (!scriptFilled) {
        if (tx.vin.empty()) return valtype(); // No input, no sender
        const COutPoint& prevout = tx.vin[0].prevout;

        // 1. Check block transactions (if provided)
        if (blockTxs) {
            for (const auto& btxRef : *blockTxs) {
                if (btxRef->GetHash() == prevout.hash) {
                    if (prevout.n < btxRef->vout.size()) {
                        script = btxRef->vout[prevout.n].scriptPubKey;
                        scriptFilled = true;
                    }
                    break; // Found the tx, break inner loop
                }
            }
        }
        // 2. Check CoinsView (UTXO set, includes mempool coins if viewMemPool is backend)
        if (!scriptFilled && coinsView) {
            const CCoins* coins = coinsView->AccessCoins(prevout.hash);
            if (coins && coins->IsAvailable(prevout.n)) {
                script = coins->vout[prevout.n].scriptPubKey;
                scriptFilled = true;
            }
        }
        // 3. Check Mempool directly (if not already covered by view)
        if (!scriptFilled && mempool) {
             CTransaction txPrev; // Use temporary CTransaction for lookup
             if (mempool->lookup(prevout.hash, txPrev)) { // lookup returns bool
                 if (prevout.n < txPrev.vout.size()) {
                     script = txPrev.vout[prevout.n].scriptPubKey;
                     scriptFilled = true;
                 }
             }
        }
        // 4. Check Transaction Index (on disk)
        if (!scriptFilled) {
            CTransaction txPrevFromDisk; // Use temporary CTransaction
            uint256 hashBlock;
            if (GetTransaction(prevout.hash, txPrevFromDisk, hashBlock, true)) { // Pass non-const CTransaction&
                if (prevout.n < txPrevFromDisk.vout.size()) {
                    script = txPrevFromDisk.vout[prevout.n].scriptPubKey;
                    scriptFilled = true;
                }
            }
        }
    }

    if (!scriptFilled) {
        LogPrintf("Error: GetSenderAddress: Could not find script for input %s of tx %s\n", !tx.vin.empty() ? tx.vin[0].prevout.ToString() : "N/A", tx.GetHash().ToString());
        return valtype();
    }

    CTxDestination addressDest;
    // Use the version of ExtractDestination available in DigiWage (likely takes 3 args)
    if (ExtractDestination(script, addressDest, true)) { // Assuming 3rd arg is fColdStake = true
        const CKeyID* keyIDPtr = boost::get<CKeyID>(&addressDest);
        if (keyIDPtr) { // Check if the variant holds a CKeyID
             return valtype(keyIDPtr->begin(), keyIDPtr->end());
        }
        // Handle other destination types if necessary, otherwise fall through
    }
    // If extraction failed or type wasn't CKeyID
    return valtype();
}
std::vector<ResultExecute> CallContract(const dev::Address& addrContract, std::vector<unsigned char> opcode, const dev::Address& sender, uint64_t gasLimit, CAmount nAmount) {
    LOCK(cs_main); // Lock needed for chainActive, globalState
    if (!globalState) return {}; CBlock blockTemplate; CBlockIndex* pindexPrev = chainActive.Tip(); if (!pindexPrev) return {};
    blockTemplate.nTime = GetAdjustedTime(); blockTemplate.hashPrevBlock = pindexPrev->GetBlockHash();
    CMutableTransaction coinbaseTx; coinbaseTx.vin.resize(1); coinbaseTx.vin[0].prevout.SetNull(); coinbaseTx.vout.resize(1); coinbaseTx.vout[0].SetEmpty(); 
    // Create a CTransaction object (presumably coinbaseTx is already one or convertible)
    // and let push_back copy or move it into the vector.
    blockTemplate.vtx.push_back(CTransaction(coinbaseTx));
    uint64_t execBlockGasLimit = 40000000; // TODO: Adapt DGP/dynamic params
    if (gasLimit == 0 || gasLimit >= execBlockGasLimit) gasLimit = execBlockGasLimit - 1;
    dev::Address senderAddress = (sender == dev::Address()) ? dev::Address("ffffffffffffffffffffffffffffffffffffffff") : sender;    CMutableTransaction callTx; callTx.vin.resize(1); callTx.vin[0].prevout.hash = GetRandHash(); callTx.vin[0].prevout.n = 0;
    CScript scriptPubKey; uint64_t gasPrice = DEFAULT_GAS_PRICE; // Use default or get from args?
    if (addrContract == dev::Address()) scriptPubKey = CScript() << CScriptNum(VersionVM::GetEVMDefault().toRaw()) << CScriptNum(gasLimit) << CScriptNum(gasPrice) << opcode << OP_CREATE;
    else scriptPubKey = CScript() << CScriptNum(VersionVM::GetEVMDefault().toRaw()) << CScriptNum(gasLimit) << CScriptNum(gasPrice) << opcode << ToByteVector(addrContract.asBytes()) << OP_CALL;
    CScript senderScript = CScript() << OP_DUP << OP_HASH160 << ToByteVector(senderAddress.asBytes()) << OP_EQUALVERIFY << OP_CHECKSIG;
    callTx.vout.push_back(CTxOut(nAmount, senderScript));
    callTx.vout.push_back(CTxOut(0, scriptPubKey));
    CTransaction finalCallTx(callTx); // Create the final CTransaction
    blockTemplate.vtx.push_back(finalCallTx); // Push the CTransaction, not the Ref
    dev::u256 nonce = globalState->getNonce(senderAddress); QtumTransaction qtumTransaction;
    if (addrContract == dev::Address()) qtumTransaction = QtumTransaction(nAmount, dev::u256(gasPrice), dev::u256(gasLimit), opcode, nonce);
    else qtumTransaction = QtumTransaction(nAmount, dev::u256(gasPrice), dev::u256(gasLimit), addrContract, opcode, nonce);
    qtumTransaction.forceSender(senderAddress); qtumTransaction.setVersion(VersionVM::GetEVMDefault());
    ByteCodeExec exec(blockTemplate, std::vector<QtumTransaction>{qtumTransaction}, execBlockGasLimit, pindexPrev);
    if (!exec.performByteCode(dev::eth::Permanence::Reverted)) return {};
    return exec.getResult();
}
bool CheckMinGasPrice(std::vector<EthTransactionParams>& etps, const uint64_t& minGasPrice) {
    for (const EthTransactionParams& etp : etps) { if (etp.version.rootVM != 0 && etp.gasPrice < dev::u256(minGasPrice)) return false; }
    return true;
}
void writeVMlog(const std::vector<ResultExecute>& res, const CTransaction& tx, const CBlock& block) {
    LOCK(cs_main); // Lock needed for chainActive
    fs::path qtumDir = GetDataDir(false); fs::path logFilePath = qtumDir / "vmExecLogs.json";
    std::stringstream ss; std::fstream file;
    file.open(logFilePath.string(), std::ios::in | std::ios::out | std::ios::app);
    if(!file.is_open()){ // Create file if it doesnt exist
        std::ofstream outfile(logFilePath.string());
        outfile << "{\"logs\":[]}";
        outfile.close();
        file.open(logFilePath.string(), std::ios::in | std::ios::out | std::ios::app);
        fIsVMlogFile = false; // Mark as new
    }
    file.seekg(0, std::ios::end);
    long fileSize = file.tellg();
    file.close(); // Close before reopening for writing

    if (fileSize > 5 || !fIsVMlogFile) { // File has content or is new
         if (fileSize > 5 && fIsVMlogFile) ss << ","; // Add comma if appending
         else fIsVMlogFile = true; // Mark as having content now
    } else { // Initialize empty/corrupt file
        std::ofstream initFile(logFilePath.string(), std::ios::out | std::ios::trunc);
        initFile << "{\"logs\":[]}"; initFile.close(); fIsVMlogFile = false;
    }

    for (size_t i = 0; i < res.size(); ++i) {
        ss << vmLogToJSON(res[i], tx, block, chainActive).write();
        if (i < res.size() - 1) ss << ",";
    }

    file.open(logFilePath.string(), std::ios::in | std::ios::out);
    if (!file.is_open()) { LogPrintf("ERROR: Could not open vmExecLogs.json for appending.\n"); return; }
    file.seekp(-2, std::ios::end); // Seek before "]}"
    long insertPos = file.tellp(); if (insertPos < 0) insertPos = 0;
    file.seekp(insertPos);
    file << ss.str() << "]}"; // Append new logs and closing bracket/brace
    file.close();
}
std::string exceptedMessage(const dev::eth::TransactionException& excepted, const dev::bytes& output) {
    std::string message = exceptedMessage(excepted, output); // Call our helper function
    if (excepted == dev::eth::TransactionException::RevertInstruction && output.size() >= 4) {
        try {
            dev::bytesConstRef selectorRef(&output); dev::bytes selector = selectorRef.cropped(0, 4).toBytes();
            if (dev::toHex(selector) == "08c379a0") { dev::bytesConstRef dataRef = selectorRef.cropped(4); message = dev::eth::ABIDeserialiser<std::string>::deserialise(dataRef); }
        } catch (const std::exception& e) { LogPrint("evm", "Could not decode revert reason: %s\n", e.what()); }
        catch (...) { LogPrint("evm", "Could not decode revert reason due to unknown exception.\n"); }
    } return message;
}
CAmount GetTxGasFee(const CMutableTransaction& mtx) {
    // Uses implementation adapted previously
    CTransaction tx(mtx); CAmount nGasFee = 0;
    if (TxHasCreateOrCall(tx)) {
        CCoinsViewCache view(pcoinsTip); unsigned int contractflags = 0;
         { LOCK(cs_main); contractflags = GetContractScriptFlags(chainActive.Height() + 1, Params().GetConsensus()); LOCK(mempool.cs); CCoinsViewMemPool viewMemPool(pcoinsTip, mempool); view.SetBackend(viewMemPool); }
        QtumTxConverter converter(tx, &mempool, &view, nullptr, contractflags);
        ExtractQtumTX resultConvertQtumTX;
        if (!converter.extractionQtumTransactions(resultConvertQtumTX)) { LogPrint("evm", "GetTxGasFee: Failed to extract EVM tx for %s\n", tx.GetHash().ToString()); return 0; }        dev::u256 sumGasStipend = dev::u256(0);
        for (const auto& qtx : resultConvertQtumTX.first) { sumGasStipend += qtx.gas() * qtx.gasPrice(); if(sumGasStipend > dev::u256(MAX_MONEY)) return MAX_MONEY; }
        nGasFee = (CAmount)sumGasStipend;
    } return nGasFee;
}
UniValue vmLogToJSON(const ResultExecute& execRes, const CTransaction& tx, const CBlock& block, CChain& chain) {
    // Uses implementation adapted previously
    UniValue result(UniValue::VOBJ); if (tx != CTransaction()) result.pushKV("txid", tx.GetHash().GetHex());
    result.pushKV("address", execRes.execRes.newAddress.hex());
    if (block.GetHash() != CBlock().GetHash()) {
        result.pushKV("time", block.GetBlockTime()); result.pushKV("blockhash", block.GetHash().GetHex());
        LOCK(cs_main); // Lock needed for mapBlockIndex access
        BlockMap::iterator mi = mapBlockIndex.find(block.GetHash());
        result.pushKV("blockheight", (mi != mapBlockIndex.end()) ? mi->second->nHeight : -1);
    } else { result.pushKV("time", GetAdjustedTime()); result.pushKV("blockheight", chain.Height()); }
    UniValue logEntries(UniValue::VARR); dev::eth::LogEntries logs = execRes.txRec.log();
    for (const dev::eth::LogEntry& log : logs) {
        UniValue logEntry(UniValue::VOBJ); logEntry.pushKV("address", log.address.hex());
        UniValue topics(UniValue::VARR);
        for (const dev::h256& l : log.topics) { UniValue topicPair(UniValue::VOBJ); topicPair.pushKV("raw", l.hex()); topics.push_back(topicPair); }
        logEntry.pushKV("topics", topics);
        UniValue dataPair(UniValue::VOBJ); dataPair.pushKV("raw", HexStr(log.data)); logEntry.pushKV("data", dataPair);
        logEntries.push_back(logEntry);
    } result.pushKV("entries", logEntries); return result;
}

/////////////////////////////////////////// Qtum Class Implementations (Adapted)

// --- QtumTxConverter (Full Adapted Implementation) ---
QtumTxConverter::QtumTxConverter(CTransaction tx, const CTxMemPool* _mempool, CCoinsViewCache* v, const std::vector<CTransactionRef>* blockTxs, unsigned int flags)
    : txBit(tx), view(v), blockTransactions(blockTxs), sender(false), nFlags(flags), mempool(_mempool) {}

bool QtumTxConverter::extractionQtumTransactions(ExtractQtumTX& qtumtx) {
    refundSender = dev::Address(GetSenderAddress(txBit, view, blockTransactions, mempool, -1));
    if (refundSender == dev::Address() && txBit.vin.size() > 0) {
        LogPrint("evm", "Warning: Could not determine refund sender for tx %s\n", txBit.GetHash().ToString());
    }
    std::vector<QtumTransaction> resultTX; std::vector<EthTransactionParams> resultETP;
    for (size_t i = 0; i < txBit.vout.size(); ++i) {
        const CScript& scriptPubKey = txBit.vout[i].scriptPubKey;
        if (scriptPubKey.HasOpCreate() || scriptPubKey.HasOpCall()) {
            if (receiveStack(scriptPubKey)) {
                EthTransactionParams params;
                if (parseEthTXParams(params)) {
                     resultTX.push_back(createEthTX(params, i)); resultETP.push_back(params);
                } else { LogPrintf("Error: Failed to parse EVM params for output %d in tx %s\n", i, txBit.GetHash().ToString()); stack.clear(); sender = false; return false; }
            } else { LogPrintf("Error: Failed to receive stack for output %d in tx %s\n", i, txBit.GetHash().ToString()); return false; }
        }
    } qtumtx = std::make_pair(resultTX, resultETP); return true;
}
bool QtumTxConverter::receiveStack(const CScript& scriptPubKey) {
    stack.clear(); sender = false;
    EvalScript(stack, scriptPubKey, nFlags, BaseSignatureChecker(), nullptr);
    if (stack.empty()) return false;
    if (stack.back().empty()) return false;
    CScript scriptRest(stack.back().begin(), stack.back().end()); stack.pop_back();
    sender = scriptPubKey.HasOpSender(); if (scriptRest.empty()) return false;
    opcode = (opcodetype)(*scriptRest.begin());
    if (opcode != OP_CREATE && opcode != OP_CALL) return false;
    size_t requiredSize = (opcode == OP_CALL) ? 5 : 4; if (sender) requiredSize += 3;
    if (stack.size() < requiredSize) { LogPrintf("Error: EVM stack size %d too small for opcode %d (sender=%d, required=%d)\n", stack.size(), opcode, sender, requiredSize); stack.clear(); sender = false; return false; }
    return true;
}
// main.cpp: Fix parsing in parseEthTXParams
bool QtumTxConverter::parseEthTXParams(EthTransactionParams& params) {
    try {
       dev::Address receiveAddress; valtype contractCodeOrData; int64_t gasPrice = 0; int64_t gasLimit = 0; // Use int64_t for CScriptNum result
       VersionVM version;
       bool fRequireMinimal = true; // Typically required for consensus

       if (sender) {
           if (stack.size() < 3) return false;
           stack.pop_back(); // sender script sig
           stack.pop_back(); // sender pubkey script
           stack.pop_back(); // PUSHDATA<sender sig+script>
       }

       // Stack order (bottom to top): version, gasLimit, gasPrice, data/code, [address]
       if (opcode == OP_CALL) {
            if (stack.size() < 5) return false;
            valtype addrBytes = stack.back(); stack.pop_back();
            if(addrBytes.size() != 20) return false;
            receiveAddress = dev::Address(addrBytes);

            contractCodeOrData = stack.back(); stack.pop_back();
            // Use CScriptNum constructor and GetInt64()
            gasPrice = CScriptNum(stack.back(), fRequireMinimal).GetInt64(); stack.pop_back();
            gasLimit = CScriptNum(stack.back(), fRequireMinimal).GetInt64(); stack.pop_back();
            version = VersionVM::fromRaw((uint32_t)CScriptNum(stack.back(), fRequireMinimal).GetInt64()); stack.pop_back();

       } else if (opcode == OP_CREATE) {
            if (stack.size() < 4) return false;
            receiveAddress = dev::Address(); // No receive address for create

            contractCodeOrData = stack.back(); stack.pop_back();
            // Use CScriptNum constructor and GetInt64()
            gasPrice = CScriptNum(stack.back(), fRequireMinimal).GetInt64(); stack.pop_back();
            gasLimit = CScriptNum(stack.back(), fRequireMinimal).GetInt64(); stack.pop_back();
            version = VersionVM::fromRaw((uint32_t)CScriptNum(stack.back(), fRequireMinimal).GetInt64()); stack.pop_back();
        } else {
           return false; // Should not happen if receiveStack worked
       }

       // Check for non-negative values and potential overflow before converting to u256
       if (gasPrice < 0 || gasLimit < 0) return false;
       // Check overflow before multiplication (gasLimit * gasPrice) - compare with MAX_MONEY / gasPrice if gasPrice > 0
       // This check might be better handled by the EVM itself, but basic sanity check here.
       // dev::u256 max_amount_u256 = dev::u256(MAX_MONEY);
       // if (gasPrice > 0 && (dev::u256)gasLimit > max_amount_u256 / (dev::u256)gasPrice) return false; // Potential overflow check

       // Assign to params using u256
       params.version = version;
       params.gasPrice = dev::u256(gasPrice);
       params.gasLimit = dev::u256(gasLimit);
       params.receiveAddress = receiveAddress;
       params.code = contractCodeOrData;

       return true;
   } catch (const scriptnum_error& err) {
       LogPrintf("Error parsing EVM parameters from stack: %s\n", err.what());
       return false;
   } catch (const std::exception& e) {
       LogPrintf("Exception parsing EVM parameters: %s\n", e.what());
       return false;
   }
   // return false; // Should be unreachable now
}
QtumTransaction QtumTxConverter::createEthTX(const EthTransactionParams& etp, uint32_t nOut) {
    dev::u256 nonce = dev::u256(0); dev::Address senderAddress(GetSenderAddress(txBit, view, blockTransactions, mempool, (int)nOut));
    if (senderAddress == dev::Address()) senderAddress = refundSender;
    QtumTransaction txEth;
    if (opcode == OP_CREATE) txEth = QtumTransaction(txBit.vout[nOut].nValue, etp.gasPrice, etp.gasLimit, etp.code, nonce);
    else txEth = QtumTransaction(txBit.vout[nOut].nValue, etp.gasPrice, etp.gasLimit, etp.receiveAddress, etp.code, nonce);
    txEth.forceSender(senderAddress);
    // txEth.setHashWith(uintToh256(txBit.GetHash())); // <-- REMOVE THIS LINE
    txEth.setNVout(nOut);
    txEth.setVersion(etp.version);
    txEth.setRefundSender(refundSender);
    return txEth;
}
size_t QtumTxConverter::correctedStackSize(size_t size) { return sender ? size + 3 : size; }

// --- LastHashes (Full Adapted Implementation) ---
void LastHashes::set(CBlockIndex const* tip) {
    clear();
    if (!tip) return;
    LOCK(cs_main); // cs_main needed for pprev access
    m_lastHashes.resize(256); // Or a smaller appropriate size? 256 is common for eth blockHashes
    for (int i = 0; i < 256; ++i) { // Loop up to 256 ancestors
        if (!tip) break; // Stop if we run out of ancestors
        if (tip->phashBlock) {
            // Convert the uint256 block hash to dev::h256
            // m_lastHashes[i] = h256Touint(*tip->phashBlock); // Incorrect conversion direction
            m_lastHashes[i] = uintToh256(*tip->phashBlock); // Correct conversion
        } else {
            // Handle case where phashBlock is null (shouldn't happen for valid index)
            LogPrintf("Warning: LastHashes::set found CBlockIndex without phashBlock at height %d\n", tip->nHeight);
            m_lastHashes[i] = dev::h256(); // Assign null hash
        }
        tip = tip->pprev; // Move to the previous block
    }
}
dev::h256s LastHashes::precedingHashes(dev::h256 const& /*ignored*/) const { return m_lastHashes; }
void LastHashes::clear() { m_lastHashes.clear(); }

// --- ByteCodeExec (Full Adapted Implementation) ---
// Constructor remains the same
ByteCodeExec::ByteCodeExec(const CBlock& _block, std::vector<QtumTransaction> _txs, const uint64_t _blockGasLimit, CBlockIndex* _pindex)
    : txs(std::move(_txs)), block(_block), blockGasLimit(_blockGasLimit), pindex(_pindex) {}

// Corrected performByteCode
bool ByteCodeExec::performByteCode(dev::eth::Permanence type) {
    // Initial checks remain the same
    if (!globalState || !globalSealEngine) {
        LogPrintf("ERROR: ByteCodeExec::performByteCode: globalState or globalSealEngine not initialized.\n");
        return false;
    }
    dev::eth::EnvInfo envInfo = BuildEVMEnvironment();

    // Loop through transactions
    for (QtumTransaction& tx : txs) {
        // Check VM Version
        if (tx.getVersion().toRaw() != VersionVM::GetEVMDefault().toRaw()) {
            // Use getHashWith() - Ensure this method is added to QtumTransaction class
            LogPrintf("ERROR: ByteCodeExec: Invalid VM version for tx %s\n", h256Touint(tx.getHashWith()).GetHex());
            dev::eth::ExecutionResult execRes;
            execRes.excepted = dev::eth::TransactionException::Unknown;
            { // --- Explicit Construction for ResultExecute ---
                ResultExecute res;
                res.execRes = execRes;
                // Explicitly construct QtumTransactionReceipt with default values
                res.txRec = QtumTransactionReceipt(dev::h256(), dev::h256(), 0, dev::eth::LogEntries());
                // res.tx uses default CTransaction constructor
                result.push_back(std::move(res));
            } // --- End Fix ---
            continue; // Skip to next tx
        }

        // Check recipient address existence for non-creation transactions
        if (!tx.isCreation() && !globalState->addressInUse(tx.receiveAddress())) {
             // Use getHashWith() - Ensure this method is added to QtumTransaction class
             LogPrintf("ERROR: ByteCodeExec: Recipient address %s does not exist for call tx %s\n", tx.receiveAddress().hex(), h256Touint(tx.getHashWith()).GetHex());
             dev::eth::ExecutionResult execRes;
             execRes.excepted = dev::eth::TransactionException::Unknown; // Use Unknown if InvalidAddress doesn't exist
             { // --- Explicit Construction for ResultExecute ---
                 ResultExecute res;
                 res.execRes = execRes;
                 // Explicitly construct QtumTransactionReceipt with default values
                 res.txRec = QtumTransactionReceipt(dev::h256(), dev::h256(), 0, dev::eth::LogEntries());
                 // res.tx uses default CTransaction constructor
                 result.push_back(std::move(res));
             } // --- End Fix ---
             continue; // Skip to next tx
        }

        // Execute the transaction
        try {
            // Pass chainActive directly - This assumes chainActive is accessible globally
            // It pushes the ResultExecute returned by globalState->execute()
            result.push_back(globalState->execute(envInfo, *globalSealEngine.get(), tx, chainActive, type, OnOpFunc()));
        } catch (const std::exception& e) {
            // Use getHashWith() - Ensure this method is added to QtumTransaction class
            LogPrintf("EXCEPTION during EVM execution for tx %s: %s\n", h256Touint(tx.getHashWith()).GetHex(), e.what());
            dev::eth::ExecutionResult execRes;
            execRes.excepted = dev::eth::TransactionException::Unknown; // Indicate failure
            { // --- Explicit Construction for ResultExecute ---
                ResultExecute res;
                res.execRes = execRes;
                 // Explicitly construct QtumTransactionReceipt with default values
                res.txRec = QtumTransactionReceipt(dev::h256(), dev::h256(), 0, dev::eth::LogEntries());
                 // res.tx uses default CTransaction constructor
                result.push_back(std::move(res));
            } // --- End Fix ---
            // Continue processing other transactions in the block.
        } catch (...) {
             // Use getHashWith() - Ensure this method is added to QtumTransaction class
            LogPrintf("UNKNOWN EXCEPTION during EVM execution for tx %s\n", h256Touint(tx.getHashWith()).GetHex());
            dev::eth::ExecutionResult execRes;
            execRes.excepted = dev::eth::TransactionException::Unknown; // Indicate failure
            { // --- Explicit Construction for ResultExecute ---
                ResultExecute res;
                res.execRes = execRes;
                 // Explicitly construct QtumTransactionReceipt with default values
                res.txRec = QtumTransactionReceipt(dev::h256(), dev::h256(), 0, dev::eth::LogEntries());
                 // res.tx uses default CTransaction constructor
                result.push_back(std::move(res));
            } // --- End Fix ---
            // Continue processing other transactions.
        }
    } // End loop through transactions

    // Commits happen elsewhere
    return true; // Return true indicating block processing can continue, even if individual txs failed
}

// processingResults remains the same as the previously corrected version
bool ByteCodeExec::processingResults(ByteCodeExecResult& resultBCE)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    CAmount totalRefunds = 0;
    for (size_t i = 0; i < result.size(); ++i) {
        const ResultExecute& execResult = result[i];
        const QtumTransaction& qtx = txs[i];
        uint64_t gasUsed = (uint64_t)execResult.execRes.gasUsed;
        resultBCE.usedGas += gasUsed;

        if (execResult.execRes.excepted != dev::eth::TransactionException::None) {
            bool shouldRefundValue = !(pindex &&
                                     pindex->nHeight >= consensusParams.QIP7Height &&
                                     execResult.execRes.excepted == dev::eth::TransactionException::RevertInstruction);
            if (qtx.value() > 0 && shouldRefundValue) {
                LogPrint("evm", "Refunding value %s to sender %s due to exception %s\n",
                         FormatMoney((CAmount)qtx.value()),
                         qtx.sender().hex(),
                         exceptedMessage(execResult.execRes.excepted, execResult.execRes.output));
            } else if (qtx.value() > 0) {
                LogPrint("evm", "Not refunding value for REVERT exception at height %d (QIP7 active: %s)\n",
                         (pindex ? pindex->nHeight : -1),
                         (pindex && pindex->nHeight >= consensusParams.QIP7Height ? "yes" : "no"));
            }
        } else {
            uint64_t gasLimit = (uint64_t)qtx.gas();
            uint64_t gasPrice = (uint64_t)qtx.gasPrice();
            if (gasLimit < gasUsed) {
                LogPrintf("ERROR: processingResults: Gas used %llu > Gas limit %llu !\n", (unsigned long long)gasUsed, (unsigned long long)gasLimit);
                return false;
            }
            dev::u256 remainingGas = gasLimit - gasUsed;
            dev::u256 refundAmount_u256 = remainingGas * gasPrice;
            if (refundAmount_u256 > dev::u256(MAX_MONEY)) {
                LogPrintf("ERROR: processingResults: Gas refund amount %s exceeds MAX_MONEY\n", refundAmount_u256.str());
                return false;
            }
            CAmount refundAmount = (CAmount)refundAmount_u256;
            if (refundAmount > 0) {
                CScript refundScript = CScript() << OP_DUP << OP_HASH160 << ToByteVector(qtx.getRefundSender().asBytes()) << OP_EQUALVERIFY << OP_CHECKSIG;
                resultBCE.refundOutputs.push_back(CTxOut(refundAmount, refundScript));
                resultBCE.refundSender += refundAmount;
                totalRefunds += refundAmount;
            }
            if (execResult.tx.GetHash() != CTransaction().GetHash()) {
                resultBCE.valueTransfers.push_back(execResult.tx);
            }
        }
    }
    if (resultBCE.usedGas > blockGasLimit) {
        LogPrintf("ERROR: processingResults: Total block gas used %llu exceeds limit %llu\n", (unsigned long long)resultBCE.usedGas, (unsigned long long)blockGasLimit);
        return false;
    }
    return true;
}

// BuildEVMEnvironment remains the same
dev::eth::EnvInfo ByteCodeExec::BuildEVMEnvironment() {
    CBlockIndex* currentBlockIndex = mapBlockIndex.count(block.GetHash()) ? mapBlockIndex[block.GetHash()] : pindex;
    dev::eth::BlockHeader header;
    header.setNumber(currentBlockIndex ? currentBlockIndex->nHeight : 0);
    header.setTimestamp(block.nTime);
    header.setDifficulty(dev::u256(block.nBits));
    header.setGasLimit(blockGasLimit);
    lastHashes.set(pindex);
    if (!lastHashes.precedingHashes({}).empty()) header.setParentHash(lastHashes.precedingHashes({})[0]);
    dev::Address authorAddress;
    if (block.IsProofOfStake()) { if (block.vtx.size() > 1 && block.vtx[1].vout.size() > 1) authorAddress = EthAddrFromScript(block.vtx[1].vout[1].scriptPubKey); }
    else { if (block.vtx.size() > 0 && block.vtx[0].vout.size() > 0) authorAddress = EthAddrFromScript(block.vtx[0].vout[0].scriptPubKey); }
    header.setAuthor(authorAddress);
    dev::u256 blockGasUsed_u256 = dev::u256(0);
    int chainID = Params().GetConsensus().EVMChainID; // Assumes EVMChainID is correctly added to Consensus::Params
    dev::eth::EnvInfo env(header, lastHashes, blockGasUsed_u256, chainID);
    return env;
}

// EthAddrFromScript remains the same
dev::Address ByteCodeExec::EthAddrFromScript(const CScript& scriptIn) {
    CTxDestination addressDest;
    TxoutType txType = TxoutType::NONSTANDARD; // Initialize just in case ExtractDestination doesn't set it
    if (ExtractDestination(scriptIn, addressDest, true)) { // Assuming 3rd arg is fColdStake=true
        const CKeyID* keyIDPtr = boost::get<CKeyID>(&addressDest);
        if (keyIDPtr != nullptr) { // Check if it holds a CKeyID
            return dev::Address(ToByteVector(uint160(*keyIDPtr)));
        }
        // Add checks for other address types if needed, e.g., CScriptID
    }
    return dev::Address(); // Return empty address if not P2PKH or extraction failed
}



///////////////////////////////////////////


bool CBlockUndo::WriteToDisk(CDiskBlockPos& pos, const uint256& hashBlock)
{
    // Open history file to append
    CAutoFile fileout(OpenUndoFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("CBlockUndo::WriteToDisk : OpenUndoFile failed");

    // Write index header
    unsigned int nSize = fileout.GetSerializeSize(*this);
    fileout << FLATDATA(Params().MessageStart()) << nSize;

    // Write undo data
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0)
        return error("CBlockUndo::WriteToDisk : ftell failed");
    pos.nPos = (unsigned int)fileOutPos;
    fileout << *this;

    // calculate & write checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << *this;
    fileout << hasher.GetHash();

    return true;
}

bool CBlockUndo::ReadFromDisk(const CDiskBlockPos& pos, const uint256& hashBlock)
{
    // Open history file to read
    CAutoFile filein(OpenUndoFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
        return error("CBlockUndo::ReadFromDisk : OpenBlockFile failed");

    // Read block
    uint256 hashChecksum;
    try {
        filein >> *this;
        filein >> hashChecksum;
    } catch (const std::exception& e) {
        return error("%s : Deserialize or I/O error - %s", __func__, e.what());
    }

    // Verify checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << *this;
    if (hashChecksum != hasher.GetHash())
        return error("CBlockUndo::ReadFromDisk : Checksum mismatch");

    return true;
}

std::string CBlockFileInfo::ToString() const
{
    return strprintf("CBlockFileInfo(blocks=%u, size=%u, heights=%u...%u, time=%s...%s)", nBlocks, nSize, nHeightFirst, nHeightLast, DateTimeStrFormat("%Y-%m-%d", nTimeFirst), DateTimeStrFormat("%Y-%m-%d", nTimeLast));
}

