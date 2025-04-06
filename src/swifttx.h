// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2016-2019 The DIGIWAGE developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SWIFTTX_H
#define SWIFTTX_H

#include "chain.h"
#include "net.h"
#include "primitives/transaction.h"
#include "sync.h"
#include "uint256.h"
#include "key.h"
#include "keystore.h"
#include "script/standard.h"
#include "serialize.h" // <<< ADDED for ADD_SERIALIZE_METHODS, READWRITE >>>
#include "timedata.h" // <<< ADDED for GetTime() >>>

#include <map>
#include <string>
#include <vector>

// Define constants if they are not defined elsewhere (check chainparams?)
#ifndef SWIFTTX_SIGNATURES_REQUIRED
#define SWIFTTX_SIGNATURES_REQUIRED 6
#endif
#ifndef SWIFTTX_SIGNATURES_TOTAL
#define SWIFTTX_SIGNATURES_TOTAL 10
#endif
// Define MIN_SWIFTTX_PROTO_VERSION if not defined elsewhere
#ifndef MIN_SWIFTTX_PROTO_VERSION
#define MIN_SWIFTTX_PROTO_VERSION 70210 // Example, adjust if needed
#endif
// Define FORGE_MASTERNODES_VERSION_2 if needed for versioning
#ifndef FORGE_MASTERNODES_VERSION_2
#define FORGE_MASTERNODES_VERSION_2 70900 // Example, adjust if needed
#endif


class CConsensusVote;
class CTransaction;
class CTransactionLock;
class CTxIn;
class CNode;
class CDataStream;
class CPubKey;
class CKey;

extern std::map<uint256, CTransaction> mapTxLockReq;
extern std::map<uint256, CTransaction> mapTxLockReqRejected;
extern std::map<uint256, CConsensusVote> mapTxLockVote;
extern std::map<uint256, CTransactionLock> mapTxLocks;
extern std::map<COutPoint, uint256> mapLockedInputs;
extern std::map<uint256, int64_t> mapUnknownVotes; //track votes with no tx for DOS
extern int nCompleteTXLocks;

bool IsIXTXValid(const CTransaction& txCollateral);
// <<< MATCH Definition in swifttx.cpp: Pass by value >>>
int64_t CreateNewLock(CTransaction tx);
// <<< REMOVED default argument from declaration >>>
void RelayTransactionLockReq(const CTransaction& tx, bool fPriority /* = false */);
bool SignLockRequest(const CTransaction& tx, CTransactionLock& txLock);
void ProcessMessageSwiftTX(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
int GetTransactionLockSignatures(uint256 txHash);
bool ProcessConsensusVote(CNode* pnode, CConsensusVote& ctx);
void CleanTransactionLocksList();
void CheckTransactionLocks(int nHeight);
bool CheckForConflictingLocks(CTransaction& tx);
int64_t GetAverageVoteTime();

// VOTE MESSAGES

class CConsensusVote
{
public:
    CTxIn vinMasternode;
    uint256 txHash;
    int nBlockHeight;
    std::vector<unsigned char> vchMasterNodeSignature;
    int64_t nTime;
    // Version, introduced with version 2 messages. Not used at the moment.
    // Can be used to structure the data differently in the future. Malleability protection.
    int nMessVersion;


    CConsensusVote(): vinMasternode(), txHash(), nBlockHeight(0), vchMasterNodeSignature(), nTime(0), nMessVersion(1) {};

    CConsensusVote(CTxIn& vinMasternodeIn, uint256 txHashIn, int nBlockHeightIn):
        vinMasternode(vinMasternodeIn), txHash(txHashIn), nBlockHeight(nBlockHeightIn), vchMasterNodeSignature(), nTime(GetTime()), nMessVersion(1) {};

    uint256 GetHash() const;

    uint256 GetSignatureHash() const;

    std::string GetStrMessage() const;

    bool Sign(std::string strPrivateKey, bool fNewSigs);
    bool CheckSignature() const;
    bool CheckSignature(CPubKey& pubKeyMasternode) const;
    bool Relay() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    // <<< Add nType and nVersion arguments to match ADD_SERIALIZE_METHODS macro >>>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        // <<< REMOVED ser_action.supportVersions check >>>
        // <<< USE s.nVersion directly >>>
        if (s.nVersion >= FORGE_MASTERNODES_VERSION_2) {
             READWRITE(nMessVersion);
        } else {
             if (ser_action.ForRead()) {
                 nMessVersion = 1;
             }
        }
        READWRITE(vinMasternode);
        READWRITE(txHash);
        READWRITE(nBlockHeight);
        READWRITE(vchMasterNodeSignature);
        READWRITE(nTime);
    }
};


class CTransactionLock
{
public:
    int nBlockHeight;
    int64_t nExpiration;
    int64_t nTimeout; //when voting stops
    uint256 txHash;
    std::vector<CConsensusVote> vecConsensusVotes;

    CTransactionLock(): nBlockHeight(0), nExpiration(0), nTimeout(0), txHash(), vecConsensusVotes() {}

    bool SignaturesValid();
    int CountSignatures();
    void AddSignature(CConsensusVote& cv);
    uint256 GetHash() const { return txHash; }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) { // Added nType, nVersion
        READWRITE(nBlockHeight);
        READWRITE(nExpiration);
        READWRITE(nTimeout);
        READWRITE(txHash);
        READWRITE(vecConsensusVotes);
    }
};

#endif // SWIFTTX_H