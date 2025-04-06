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

#ifndef BITCOIN_MAIN_H
#define BITCOIN_MAIN_H

#if defined(HAVE_CONFIG_H)
#include "config/digiwage-config.h"
#endif

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "coins.h"
#include "consensus/validation.h"
#include "net.h"
#include "pow.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "zpiv/zerocoin.h" // DigiWage specific
#include "zpiv/zpivmodule.h" // DigiWage specific
#include "script/script.h"
#include "script/sigcache.h"
#include "script/standard.h"
#include "sync.h"
#include "tinyformat.h"
#include "txmempool.h"
#include "uint256.h"
#include "undo.h"
#include "consensus/consensus.h" // Added from qtum validation.h

#include <algorithm>
#include <atomic>
#include <exception>
#include <map>
#include <memory> // Added for shared_ptr / unique_ptr etc.
#include <optional> // Added from qtum validation.h
#include <set>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include "libzerocoin/CoinSpend.h" // DigiWage specific

#include <boost/unordered_map.hpp>

/////////////////////////////////////////// qtum / EVM Includes
#include <qtum/qtumstate.h>
#include <libethereum/ChainParams.h> // From libethereum
#include <libethereum/LastBlockHashesFace.h> // From libethereum
#include <libethashseal/GenesisInfo.h> // Consider if SealEngine is needed/ported
#include <script/solver.h> // Qtum specific script solver?
#include <qtum/storageresults.h> // For contract execution results
///////////////////////////////////////////

// --- Fix for CTransactionRef ---
// Define CTransactionRef if it doesn't exist (common in older codebases)
// Ensure CTransaction is fully defined before this line (it is via primitives/transaction.h)
#ifndef CTransactionRef
// Use a shared_ptr for automatic memory management and reference counting.
// It points to a const CTransaction, indicating the underlying transaction shouldn't be modified through this ref.
typedef std::shared_ptr<const CTransaction> CTransactionRef;

// Helper function to create a CTransactionRef (similar to MakeTransactionRef in newer code)
inline CTransactionRef MakeTransactionRef(CTransaction&& tx) {
    return std::make_shared<const CTransaction>(std::move(tx));
}
inline CTransactionRef MakeTransactionRef(const CTransaction& tx) {
    return std::make_shared<const CTransaction>(tx);
}
#endif
// --- End CTransactionRef fix ---

// --- Fix for SCRIPT_EXEC_BYTE_CODE ---
// Define script execution flag used by Qtum's EVM integration if not already defined
#ifndef SCRIPT_EXEC_BYTE_CODE
#define SCRIPT_EXEC_BYTE_CODE (1U << 4)
#endif
// --- End SCRIPT_EXEC_BYTE_CODE fix ---


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// +++ ADD THIS ENUM DEFINITION HERE ++++++++++++++++++++++++
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
/** Values used by FlushStateToDisk */
enum class FlushStateMode {
    NONE,            //!< Used when StateToDisk is called after function that doesn't necessitate flushing
    IF_NEEDED,       //!< Flush state representing latest validated block to disk only if necessary
    PERIODIC,        //!< Flush state periodically dictated by -dbcache size
    ALWAYS,          //!< Flush state unconditionally
    SHUTDOWN         //!< Flush state during shutdown
};
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// +++ END OF ADDED CODE ++++++++++++++++++++++++++++++++++++
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++




class CBlockIndex;
class CBlockTreeDB;
class CZerocoinDB; // DigiWage specific
class CSporkDB; // DigiWage specific (or general Dash-like)
class CBloomFilter;
class CInv;
class CScriptCheck;
class CValidationInterface;
class CValidationState;
class CBlockUndo; // Forward declare
class QtumTxConverter; // Forward declare Qtum class
class ByteCodeExec; // Forward declare Qtum class
class LastHashes; // Forward declare Qtum class
struct EthTransactionParams; // Forward declare Qtum struct
struct ResultExecute; // Forward declare Qtum struct

struct CBlockTemplate;
struct CNodeStateStats;

/** Default for -blockmaxsize and -blockminsize, which control the range of sizes the mining code will create **/
static const unsigned int DEFAULT_BLOCK_MAX_SIZE = 750000;
static const unsigned int DEFAULT_BLOCK_MIN_SIZE = 0;
/** Default for -blockprioritysize, maximum space for zero/low-fee transactions **/
static const unsigned int DEFAULT_BLOCK_PRIORITY_SIZE = 50000;
/** The maximum size for transactions we're willing to relay/mine */
static const unsigned int MAX_STANDARD_TX_SIZE = 100000;
static const unsigned int MAX_ZEROCOIN_TX_SIZE = 150000; // DigiWage specific
/** Maximum number of signature check operations in an IsStandard() P2SH script */
static const unsigned int MAX_P2SH_SIGOPS = 15;
/** Default for -maxorphantx, maximum number of orphan transactions kept in memory */
static const unsigned int DEFAULT_MAX_ORPHAN_TRANSACTIONS = 100;
/** The maximum size of a blk?????.dat file (since 0.8) */
static const unsigned int MAX_BLOCKFILE_SIZE = 0x8000000; // 128 MiB
/** The pre-allocation chunk size for blk?????.dat files (since 0.8) */
static const unsigned int BLOCKFILE_CHUNK_SIZE = 0x1000000; // 16 MiB
/** The pre-allocation chunk size for rev?????.dat files (since 0.8) */
static const unsigned int UNDOFILE_CHUNK_SIZE = 0x100000; // 1 MiB
/** Maximum number of script-checking threads allowed */
static const int MAX_SCRIPTCHECK_THREADS = 16;
/** -par default (number of script-checking threads, 0 = auto) */
static const int DEFAULT_SCRIPTCHECK_THREADS = 0;
/** Number of blocks that can be requested at any given time from a single peer. */
static const int MAX_BLOCKS_IN_TRANSIT_PER_PEER = 512;
/** Timeout in seconds during which a peer must stall block download progress before being disconnected. */
static const unsigned int BLOCK_STALLING_TIMEOUT = 2;
/** Number of headers sent in one getheaders result. We rely on the assumption that if a peer sends
 *  less than this number, we reached their tip. Changing this value is a protocol upgrade. */
static const unsigned int MAX_HEADERS_RESULTS = 2000;
/** Size of the "block download window": how far ahead of our current height do we fetch? */
static const unsigned int BLOCK_DOWNLOAD_WINDOW = 1024;
/** Time to wait (in seconds) between writing blockchain state to disk. */
static const unsigned int DATABASE_WRITE_INTERVAL = 3600;
/** Maximum length of reject messages. */
static const unsigned int MAX_REJECT_MESSAGE_LENGTH = 111;

/** Enable bloom filter */
 static const bool DEFAULT_PEERBLOOMFILTERS = true;

/** If the tip is older than this (in seconds), the node is considered to be in initial block download. */
static const int64_t DEFAULT_MAX_TIP_AGE = 24 * 60 * 60;

/** Default for -blockspamfilter, use header spam filter */
static const bool DEFAULT_BLOCK_SPAM_FILTER = true; // Keep DigiWage's?
/** Default for -blockspamfiltermaxsize, maximum size of the list of indexes in the block spam filter */
static const unsigned int DEFAULT_BLOCK_SPAM_FILTER_MAX_SIZE = 100; // Keep DigiWage's?
/** Default for -blockspamfiltermaxavg, maximum average size of an index occurrence in the block spam filter */
static const unsigned int DEFAULT_BLOCK_SPAM_FILTER_MAX_AVG = 10; // Keep DigiWage's?

/////////////////////////////////////////// qtum / EVM Constants
/** Minimum gas limit that is allowed in a transaction within a block - prevent various types of tx and mempool spam **/
static const uint64_t MINIMUM_GAS_LIMIT = 10000;
static const uint64_t MEMPOOL_MIN_GAS_LIMIT = 22000; // Minimum gas limit for mempool acceptance
// static const uint64_t ADD_DELEGATION_MIN_GAS_LIMIT = 2200000; // Qtum PoS specific - Review if needed
static const bool DEFAULT_ADDRINDEX = false; // Qtum specific default? DigiWage might have its own.
static const bool DEFAULT_LOGEVENTS = false; // Qtum specific default for event logging
static const uint64_t DEFAULT_GAS_LIMIT_OP_CREATE = 2500000;
static const uint64_t DEFAULT_GAS_LIMIT_OP_SEND = 250000;
static const CAmount DEFAULT_GAS_PRICE = 0.00000040 * COIN; // 40 Satoshi or 0.4 gwei equiv if COIN=1e8
static const CAmount MAX_RPC_GAS_PRICE = 0.00000100 * COIN; // 100 Satoshi or 1 gwei equiv if COIN=1e8
static const size_t MAX_CONTRACT_VOUTS = 1000; // Max contract execution results VOUTs
// static const CAmount DEFAULT_STAKING_MIN_UTXO_VALUE = 100 * COIN; // Qtum PoS specific - Review if needed
///////////////////////////////////////////

struct BlockHasher {
    size_t operator()(const uint256& hash) const { return hash.GetCheapHash(); }
};

extern CScript COINBASE_FLAGS;
extern RecursiveMutex cs_main;
extern CTxMemPool mempool;
typedef boost::unordered_map<uint256, CBlockIndex*, BlockHasher> BlockMap;
extern BlockMap mapBlockIndex;
extern uint64_t nLastBlockTx;
extern uint64_t nLastBlockSize;
extern const std::string strMessageMagic;
extern int64_t nTimeBestReceived;
extern CAmount nBurnedCoins; // DigiWage burn tracking?

// Best block section
extern Mutex g_best_block_mutex;
extern std::condition_variable g_best_block_cv;
extern uint256 g_best_block;

extern std::atomic<bool> fImporting;
extern std::atomic<bool> fReindex;
extern int nScriptCheckThreads;
extern bool fTxIndex; // Keep DigiWage default? Qtum uses different indexing flags.
extern bool fIsBareMultisigStd;
extern bool fCheckBlockIndex;
extern unsigned int nCoinCacheSize;
extern CFeeRate minRelayTxFee;
extern int64_t nMaxTipAge; // Use DEFAULT_MAX_TIP_AGE
extern bool fVerifyingBlocks;

extern bool fLargeWorkForkFound; // DigiWage specific?
extern bool fLargeWorkInvalidChainFound; // DigiWage specific?
extern bool fBlockDatabaseForkFound; // DigiWage specific?

extern std::map<uint256, int64_t> mapRejectedBlocks;

/** Best header we've seen so far (used for getheaders queries' starting points). */
extern CBlockIndex* pindexBestHeader;

/** Minimum disk space required - used in CheckDiskSpace() */
static const uint64_t nMinDiskSpace = 52428800;

/////////////////////////////////////////// qtum / EVM Global State
extern std::unique_ptr<QtumState> globalState; // EVM state database
extern std::shared_ptr<dev::eth::SealEngineFace> globalSealEngine; // Interface for consensus engine (like ethash), might not be needed if adapting DigiWage PoW/PoS
extern std::unique_ptr<StorageResults> pstorageresult; // For storing contract execution results
extern bool fRecordLogOpcodes; // Flag for debugging EVM execution
extern bool fIsVMlogFile; // Flag for directing VM log output
extern bool fGettingValuesDGP; // Flag for Distributed Governance Protocol (Qtum specific, review if needed)
extern bool fLogEvents; // Flag for event logging
extern bool fAddressIndex; // Flag for address index
///////////////////////////////////////////

/////////////////////////////////////////// qtum / EVM Structs & Typedefs
using valtype = std::vector<unsigned char>; // Typdef for byte vectors used in EVM

// Structure to hold parameters for an EVM transaction (extracted from script)
struct EthTransactionParams{
    VersionVM version; // VM version rules
    dev::u256 gasLimit;
    dev::u256 gasPrice;
    valtype code; // Contract code or data payload
    dev::Address receiveAddress; // Contract address or recipient

    // Comparison operator (from Qtum's validation.h)
    // IMPORTANT: VersionVM::toRaw() MUST be const for this to compile.
    bool operator!=(const EthTransactionParams& etp) const {
        if(this->version.toRaw() != etp.version.toRaw() || this->gasLimit != etp.gasLimit ||
           this->gasPrice != etp.gasPrice || this->code != etp.code ||
           this->receiveAddress != etp.receiveAddress)
            return true;
        return false;
    }
};

// Pair to hold extracted contract Txs and EVM params from a standard Tx
using ExtractQtumTX = std::pair<std::vector<QtumTransaction>, std::vector<EthTransactionParams>>;

// Structure to hold results from executing EVM bytecode (gas used, refunds, etc.)
struct ByteCodeExecResult{
    uint64_t usedGas = 0;
    CAmount refundSender = 0; // Gas refund amount
    std::vector<CTxOut> refundOutputs; // Potential refund outputs (e.g., for gas)
    std::vector<CTransaction> valueTransfers; // Internal value transfers generated by contract
};
///////////////////////////////////////////


/** Register with a network node to receive its signals */
void RegisterNodeSignals(CNodeSignals& nodeSignals);
/** Unregister a network node */
void UnregisterNodeSignals(CNodeSignals& nodeSignals);

/**
 * Process an incoming block. This only returns after the best known valid
 * block is made active. Note that it does not, however, guarantee that the
 * specific block passed to it has been checked for validity!
 * [...] // Keep DigiWage doc comment
 */
bool ProcessNewBlock(CValidationState& state, CNode* pfrom, CBlock* pblock, CDiskBlockPos* dbp = NULL);
/** Check whether enough disk space is available for an incoming block */
bool CheckDiskSpace(uint64_t nAdditionalBytes = 0);
/** Open a block file (blk?????.dat) */
FILE* OpenBlockFile(const CDiskBlockPos& pos, bool fReadOnly = false);
/** Open an undo file (rev?????.dat) */
FILE* OpenUndoFile(const CDiskBlockPos& pos, bool fReadOnly = false);
/** Translation to a filesystem path */
boost::filesystem::path GetBlockPosFilename(const CDiskBlockPos& pos, const char* prefix);
/** Import blocks from an external file */
bool LoadExternalBlockFile(FILE* fileIn, CDiskBlockPos* dbp = NULL);
/** Initialize a new block tree database + block data on disk */
bool InitBlockIndex();
/** Load the block tree and coins database from disk */
bool LoadBlockIndex(std::string& strError);
/** Unload database information */
void UnloadBlockIndex();
/** See whether the protocol update is enforced for connected nodes */
int ActiveProtocol(); // DigiWage specific?
/** Process protocol messages received from a given node */
bool ProcessMessages(CNode* pfrom);
/** Send queued protocol messages to be sent to a give node. */
bool SendMessages(CNode* pto, bool fSendTrickle);
/** Run an instance of the script checking thread */
void ThreadScriptCheck();

/** Check whether we are doing an initial block download (synchronizing from disk or network) */
bool IsInitialBlockDownload();
/** Format a string that describes several potential problems detected by the core */
std::string GetWarnings(std::string strFor);
/** Retrieve a transaction (from memory pool, or from disk, if possible) */
bool GetTransaction(const uint256& hash, CTransaction& tx, uint256& hashBlock, bool fAllowSlow = false, CBlockIndex* blockIndex = nullptr);
/** Retrieve an output (from memory pool, or from disk, if possible) */
bool GetOutput(const uint256& hash, unsigned int index, CValidationState& state, CTxOut& out);
/** Find the best known block, and make it the tip of the block chain */

// ***TODO*** (DigiWage specific)
double ConvertBitsToDouble(unsigned int nBits);
int64_t GetMasternodePayment(int nHeight, int64_t blockValue, int nMasternodeCount);
unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader* pblock, bool fProofOfStake);

/** Activate the best chain */
bool ActivateBestChain(CValidationState& state, CBlock* pblock = NULL, bool fAlreadyChecked = false);
/** Get the block reward amount for a given height */
CAmount GetBlockValue(int nHeight);

/** Create a new block index entry for a given block hash */
CBlockIndex* InsertBlockIndex(uint256 hash);
/** Get statistics from node state */
bool GetNodeStateStats(NodeId nodeid, CNodeStateStats& stats);
/** Increase a node's misbehavior score. */
void Misbehaving(NodeId nodeid, int howmuch);
/** Flush all state, indexes and buffers to disk. */
void FlushStateToDisk();

/** (try to) add transaction to memory pool **/
bool AcceptToMemoryPool(CTxMemPool& pool, CValidationState& state, const CTransaction& tx, bool fLimitFree, bool* pfMissingInputs, bool fRejectInsaneFee = false, bool ignoreFees = false);

bool AcceptableInputs(CTxMemPool& pool, CValidationState& state, const CTransaction& tx, bool fLimitFree, bool* pfMissingInputs, bool fRejectInsaneFee = false, bool isDSTX = false); // DigiWage specific?

int GetInputAge(CTxIn& vin); // DigiWage specific?
int GetIXConfirmations(uint256 nTXHash); // DigiWage specific (InstantSend)?

struct CNodeStateStats {
    int nMisbehavior;
    int nSyncHeight;
    int nCommonHeight;
    std::vector<int> vHeightInFlight;
};

CAmount GetMinRelayFee(const CTransaction& tx, unsigned int nBytes, bool fAllowFree);

/** Check transaction inputs [...] */ // Keep DigiWage doc comment
bool AreInputsStandard(const CTransaction& tx, const CCoinsViewCache& mapInputs);

/** Check whether all inputs of this transaction are valid [...] */ // Keep DigiWage doc comment
bool CheckInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view, bool fScriptChecks, unsigned int flags, bool cacheStore, std::vector<CScriptCheck>* pvChecks = NULL);

/** Apply the effects of this transaction on the UTXO set represented by view */
void UpdateCoins(const CTransaction& tx, CValidationState& state, CCoinsViewCache& inputs, CTxUndo& txundo, int nHeight);

bool IsTransactionInChain(const uint256& txId, int& nHeightTx, CTransaction& tx);
bool IsTransactionInChain(const uint256& txId, int& nHeightTx);
bool IsBlockHashInChain(const uint256& hashBlock);

/** Check if transaction will be final in the next block [...] */ // Keep DigiWage doc comment
bool CheckFinalTx(const CTransaction& tx, int flags = -1);

/** Check for standard transaction types */
bool IsStandardTx(const CTransaction& tx, std::string& reason);

/** Closure representing one script verification */ // Keep DigiWage CScriptCheck class
class CScriptCheck
{
private:
    CScript scriptPubKey;
    const CTransaction* ptxTo;
    unsigned int nIn;
    unsigned int nFlags;
    bool cacheStore;
    ScriptError error;

public:
    CScriptCheck() : ptxTo(0), nIn(0), nFlags(0), cacheStore(false), error(SCRIPT_ERR_UNKNOWN_ERROR) {}
    CScriptCheck(const CCoins& txFromIn, const CTransaction& txToIn, unsigned int nInIn, unsigned int nFlagsIn, bool cacheIn) : scriptPubKey(txFromIn.vout[txToIn.vin[nInIn].prevout.n].scriptPubKey),
                                                                                                                                ptxTo(&txToIn), nIn(nInIn), nFlags(nFlagsIn), cacheStore(cacheIn), error(SCRIPT_ERR_UNKNOWN_ERROR) {}

    bool operator()(); // Implementation remains in main.cpp

    void swap(CScriptCheck& check)
    {
        scriptPubKey.swap(check.scriptPubKey);
        std::swap(ptxTo, check.ptxTo);
        std::swap(nIn, check.nIn);
        std::swap(nFlags, check.nFlags);
        std::swap(cacheStore, check.cacheStore);
        std::swap(error, check.error);
    }

    ScriptError GetScriptError() const { return error; }
};


/** Functions for disk access for blocks */
bool WriteBlockToDisk(CBlock& block, CDiskBlockPos& pos);
bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos);
bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex);


/** Functions for validating blocks and updating the block tree */

/** Undo the effects of this block [...] */ // Keep DigiWage doc comment
bool DisconnectBlock(CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& coins, bool* pfClean = NULL);

/** Reprocess a number of blocks [...] */ // Keep DigiWage doc comment
bool DisconnectBlocks(int nBlocks);
void ReprocessBlocks(int nBlocks);

/** Apply the effects of this block [...] */ // Keep DigiWage doc comment
bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& coins, bool fJustCheck, bool fAlreadyChecked = false);

/** Context-independent validity checks */
bool CheckBlockHeader(const CBlockHeader& block, CValidationState& state, bool fCheckPOW = true);
bool CheckBlock(const CBlock& block, CValidationState& state, bool fCheckPOW = true, bool fCheckMerkleRoot = true, bool fCheckSig = true);
bool CheckWork(const CBlock block, CBlockIndex* const pindexPrev); // DigiWage specific?

/** Context-dependent validity checks */
bool ContextualCheckBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex* pindexPrev);
bool ContextualCheckBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindexPrev);

/** Check a block is completely valid from start to finish [...] */ // Keep DigiWage doc comment
bool TestBlockValidity(CValidationState& state, const CBlock& block, CBlockIndex* pindexPrev, bool fCheckPOW = true, bool fCheckMerkleRoot = true);

/** Store block on disk. [...] */ // Keep DigiWage doc comment
bool AcceptBlock(CBlock& block, CValidationState& state, CBlockIndex** pindex, CDiskBlockPos* dbp = NULL, bool fAlreadyCheckedBlock = false);
bool AcceptBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex** ppindex = NULL);


/** RAII wrapper for VerifyDB [...] */ // Keep DigiWage CVerifyDB class
class CVerifyDB
{
public:
    CVerifyDB();
    ~CVerifyDB();
    bool VerifyDB(CCoinsView* coinsview, int nCheckLevel, int nCheckDepth);
};

/** Find the last common block between the parameter chain and a locator. */
CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator);

/** Mark a block as invalid. */
bool InvalidateBlock(CValidationState& state, CBlockIndex* pindex);

/** Remove invalidity status from a block and its descendants. */
bool ReconsiderBlock(CValidationState& state, CBlockIndex* pindex);

/** The currently-connected chain of blocks. */
extern CChain chainActive;

/** Global variable that points to the active CCoinsView (protected by cs_main) */
extern CCoinsViewCache* pcoinsTip;

/** Global variable that points to the active block tree (protected by cs_main) */
extern CBlockTreeDB* pblocktree;

/** Global variable that points to the zerocoin database (protected by cs_main) */
extern CZerocoinDB* zerocoinDB; // DigiWage specific

/** Global variable that points to the spork database (protected by cs_main) */
extern CSporkDB* pSporkDB; // DigiWage specific


/////////////////////////////////////////// qtum / EVM Class Declarations (Implementation in main.cpp)

// --- QtumTxConverter ---
// Implementation needs adaptation to DigiWage globals.
class QtumTxConverter{
public:
    QtumTxConverter(CTransaction tx, const CTxMemPool* _mempool, CCoinsViewCache* v = NULL, const std::vector<CTransactionRef>* blockTxs = NULL, unsigned int flags = SCRIPT_EXEC_BYTE_CODE);
    bool extractionQtumTransactions(ExtractQtumTX& qtumTx);
private:
    bool receiveStack(const CScript& scriptPubKey);
    bool parseEthTXParams(EthTransactionParams& params);
    QtumTransaction createEthTX(const EthTransactionParams& etp, const uint32_t nOut);
    size_t correctedStackSize(size_t size);

    const CTransaction txBit;
    const CCoinsViewCache* view;
    std::vector<valtype> stack;
    opcodetype opcode;
    const std::vector<CTransactionRef>* blockTransactions;
    bool sender; // Was likely used with CheckSenderScript logic
    dev::Address refundSender; // Stores sender address for potential gas refunds
    unsigned int nFlags;
    const CTxMemPool* mempool;
};

// --- LastHashes ---
// Implementation needs adaptation to DigiWage globals.
class LastHashes: public dev::eth::LastBlockHashesFace
{
public:
    explicit LastHashes() {} // Default constructor ok?
    void set(CBlockIndex const* tip);
    dev::h256s precedingHashes(dev::h256 const&) const override;
    void clear() override;
private:
    dev::h256s m_lastHashes GUARDED_BY(cs_main); // Add locking if needed
};

// --- ByteCodeExec ---
// Implementation needs adaptation to DigiWage globals.
class ByteCodeExec {
public:
    ByteCodeExec(const CBlock& _block, std::vector<QtumTransaction> _txs, const uint64_t _blockGasLimit, CBlockIndex* _pindex);
    bool performByteCode(dev::eth::Permanence type = dev::eth::Permanence::Committed);
    bool processingResults(ByteCodeExecResult& result);
    std::vector<ResultExecute>& getResult(){ return result; }
private:
    dev::eth::EnvInfo BuildEVMEnvironment();
    dev::Address EthAddrFromScript(const CScript& scriptIn);

    std::vector<QtumTransaction> txs;
    std::vector<ResultExecute> result;
    const CBlock& block;
    const uint64_t blockGasLimit;
    CBlockIndex* pindex; // Previous block index
    LastHashes lastHashes;
    // Removed CChain& chain; reference
};

///////////////////////////////////////////

/////////////////////////////////////////// qtum / EVM Function Declarations

/** Get consensus flags relevant to contract execution for a given height */
unsigned int GetContractScriptFlags(int nHeight, const Consensus::Params& consensusparams);

/** Execute a contract call (locally, without creating a transaction) */
std::vector<ResultExecute> CallContract(const dev::Address& addrContract, std::vector<unsigned char> opcode, const dev::Address& sender = dev::Address(), uint64_t gasLimit=0, CAmount nAmount=0);

/** Check OP_SENDER constraints */
bool CheckOpSender(const CTransaction& tx, const CChainParams& chainparams, int nHeight);

/** Check if the sender script in a contract transaction is valid */
bool CheckSenderScript(const CCoinsViewCache& view, const CTransaction& tx);

/** Check if transactions meet the minimum gas price requirement */
bool CheckMinGasPrice(std::vector<EthTransactionParams>& etps, const uint64_t& minGasPrice);

/** Write VM execution logs */
void writeVMlog(const std::vector<ResultExecute>& res, const CTransaction& tx = CTransaction(), const CBlock& block = CBlock());

/** Format EVM exception messages */
std::string exceptedMessage(const dev::eth::TransactionException& excepted, const dev::bytes& output);

/** Calculate the gas fee for a transaction */
CAmount GetTxGasFee(const CMutableTransaction& tx);

///////////////////////////////////////////


#endif // BITCOIN_MAIN_H