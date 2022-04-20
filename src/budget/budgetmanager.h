// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The DIGIWAGE developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BUDGET_MANAGER_H
#define BUDGET_MANAGER_H

#include "budget/budgetproposal.h"
#include "budget/finalizedbudget.h"
#include "validationinterface.h"

class CValidationState;

#define ORPHAN_VOTES_CACHE_LIMIT 10000

//
// Budget Manager : Contains all proposals for the budget
//
class CBudgetManager : public CValidationInterface
{
protected:
    // map budget hash --> CollTx hash.
    // hold unconfirmed finalized-budgets collateral txes until they mature enough to use
    std::map<uint256, uint256> mapUnconfirmedFeeTx;                         // guarded by cs_budgets

    // map CollTx hash --> budget hash
    // keep track of collaterals for valid budgets/proposals (for reorgs)
    std::map<uint256, uint256> mapFeeTxToProposal;                          // guarded by cs_proposals
    std::map<uint256, uint256> mapFeeTxToBudget;                            // guarded by cs_budgets

    std::map<uint256, CBudgetProposal> mapProposals;                        // guarded by cs_proposals
    std::map<uint256, CFinalizedBudget> mapFinalizedBudgets;                // guarded by cs_budgets

    std::map<uint256, CBudgetVote> mapSeenProposalVotes;                    // guarded by cs_votes
    typedef std::pair<std::vector<CBudgetVote>, int64_t> PropVotesAndLastVoteReceivedTime;
    std::map<uint256, PropVotesAndLastVoteReceivedTime> mapOrphanProposalVotes;        // guarded by cs_votes
    std::map<uint256, CFinalizedBudgetVote> mapSeenFinalizedBudgetVotes;    // guarded by cs_finalizedvotes
    typedef std::pair<std::vector<CFinalizedBudgetVote>, int64_t> BudVotesAndLastVoteReceivedTime;
    std::map<uint256, BudVotesAndLastVoteReceivedTime> mapOrphanFinalizedBudgetVotes;  // guarded by cs_finalizedvotes

    // Memory Only. Updated in NewBlock (blocks arrive in order)
    std::atomic<int> nBestHeight;

    // Spam protection
    // who's asked for the complete budget sync and the last time
    std::map<CNetAddr, int64_t> mAskedUsForBudgetSync; // guarded by cs_budgets and cs_proposals.

    // Returns a const pointer to the budget with highest vote count
    const CFinalizedBudget* GetBudgetWithHighestVoteCount(int chainHeight) const;
    int GetHighestVoteCount(int chainHeight) const;
    // Get the payee and amount for the budget with the highest vote count
    bool GetPayeeAndAmount(int chainHeight, CScript& payeeRet, CAmount& nAmountRet) const;
    // Marks synced all votes in proposals and finalized budgets
    void SetSynced(bool synced);

public:
    // critical sections to protect the inner data structures (must be locked in this order)
    mutable RecursiveMutex cs_budgets;
    mutable RecursiveMutex cs_proposals;
    mutable RecursiveMutex cs_finalizedvotes;
    mutable RecursiveMutex cs_votes;

    // budget finalization
    std::string strBudgetMode = "";

    CBudgetManager() {}

    void UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload) override;

    bool HaveProposal(const uint256& propHash) const { LOCK(cs_proposals); return mapProposals.count(propHash); }
    bool HaveSeenProposalVote(const uint256& voteHash) const { LOCK(cs_votes); return mapSeenProposalVotes.count(voteHash); }
    bool HaveFinalizedBudget(const uint256& budgetHash) const { LOCK(cs_budgets); return mapFinalizedBudgets.count(budgetHash); }
    bool HaveSeenFinalizedBudgetVote(const uint256& voteHash) const { LOCK(cs_finalizedvotes); return mapSeenFinalizedBudgetVotes.count(voteHash); }

    // Clears and reloads seen votes in the maps, and clears orphan votes
    void ReloadMapSeen();

    void AddSeenProposalVote(const CBudgetVote& vote);
    void AddSeenFinalizedBudgetVote(const CFinalizedBudgetVote& vote);

    void RemoveStaleVotesOnProposal(CBudgetProposal* prop);
    void RemoveStaleVotesOnFinalBudget(CFinalizedBudget* fbud);

    // Use const operator std::map::at(), thus existence must be checked before calling.
    CDataStream GetProposalVoteSerialized(const uint256& voteHash) const;
    CDataStream GetProposalSerialized(const uint256& propHash) const;
    CDataStream GetFinalizedBudgetVoteSerialized(const uint256& voteHash) const;
    CDataStream GetFinalizedBudgetSerialized(const uint256& budgetHash) const;

    bool AddAndRelayProposalVote(const CBudgetVote& vote, std::string& strError);

    // sets strProposal of a CFinalizedBudget reference
    void SetBudgetProposalsStr(CFinalizedBudget& finalizedBudget) const;

    // checks finalized budget proposals (existence, payee, amount) for the finalized budget
    // in the map, with given nHash. Returns error string if any, or "OK" otherwise
    std::string GetFinalizedBudgetStatus(const uint256& nHash) const;

    void ResetSync() { SetSynced(false); }
    void MarkSynced() { SetSynced(true); }
    // Respond to full budget sync requests and internally triggered partial budget items relay
    void Sync(CNode* node, bool fPartial);
    // Respond to single budget item requests (proposals / budget finalization)
    void SyncSingleItem(CNode* pfrom, const uint256& nProp);
    void SetBestHeight(int height) { nBestHeight.store(height, std::memory_order_release); };
    int GetBestHeight() const { return nBestHeight.load(std::memory_order_acquire); }

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    /// Process the message and returns the ban score (0 if no banning is needed)
    int ProcessMessageInner(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void NewBlock();

    int ProcessBudgetVoteSync(const uint256& nProp, CNode* pfrom);
    int ProcessProposal(CBudgetProposal& proposal);
    int ProcessFinalizedBudget(CFinalizedBudget& finalbudget, CNode* pfrom);

    bool ProcessProposalVote(CBudgetVote& proposal, CNode* pfrom, CValidationState& state);
    bool ProcessFinalizedBudgetVote(CFinalizedBudgetVote& vote, CNode* pfrom, CValidationState& state);

    // functions returning a pointer in the map. Need cs_proposals/cs_budgets locked from the caller
    CBudgetProposal* FindProposal(const uint256& nHash);
    CFinalizedBudget* FindFinalizedBudget(const uint256& nHash);
    // const functions, copying the budget object to a reference and returning true if found
    bool GetProposal(const uint256& nHash, CBudgetProposal& bp) const;
    bool GetFinalizedBudget(const uint256& nHash, CFinalizedBudget& fb) const;
    // finds the proposal with the given name, with highest net yes count.
    const CBudgetProposal* FindProposalByName(const std::string& strProposalName) const;

    // Returns true if there is at least one proposal stored.
    bool HasAnyProposal() const { return WITH_LOCK(cs_proposals, return !mapProposals.empty()); }

    static CAmount GetTotalBudget(int nHeight);
    std::vector<CBudgetProposal> GetBudget();
    // Get all the budget proposals sorted by votes (highest to lowest)
    std::vector<CBudgetProposal*> GetAllProposalsOrdered();
    std::vector<CFinalizedBudget*> GetFinalizedBudgets();
    bool GetExpectedPayeeAmount(int chainHeight, CAmount& nAmountRet) const;
    bool IsBudgetPaymentBlock(int nBlockHeight) const;
    bool IsBudgetPaymentBlock(int nBlockHeight, int& nCountThreshold) const;
    bool AddProposal(CBudgetProposal& budgetProposal);
    bool AddFinalizedBudget(CFinalizedBudget& finalizedBudget, CNode* pfrom = nullptr);
    void ForceAddFinalizedBudget(const uint256& nHash, const uint256& feeTxId, const CFinalizedBudget& finalizedBudget);
    uint256 SubmitFinalBudget();

    bool UpdateProposal(const CBudgetVote& vote, CNode* pfrom, std::string& strError);
    bool UpdateFinalizedBudget(const CFinalizedBudgetVote& vote, CNode* pfrom, std::string& strError);
    TrxValidationStatus IsTransactionValid(const CTransaction& txNew, const uint256& nBlockHash, int nBlockHeight) const;
    std::string GetRequiredPaymentsString(int nBlockHeight);
    bool FillBlockPayee(CMutableTransaction& txCoinbase, CMutableTransaction& txCoinstake, const int nHeight, bool fProofOfStake) const;

    // Only initialized masternodes: sign and submit votes on valid finalized budgets
    void VoteOnFinalizedBudgets();

    int CountProposals() { LOCK(cs_proposals); return mapProposals.size(); }

    void CheckOrphanVotes();
    void Clear()
    {
        {
            LOCK(cs_proposals);
            mapProposals.clear();
            mapFeeTxToProposal.clear();
        }
        {
            LOCK(cs_budgets);
            mapFinalizedBudgets.clear();
            mapFeeTxToBudget.clear();
            mapUnconfirmedFeeTx.clear();
        }
        {
            LOCK(cs_votes);
            mapSeenProposalVotes.clear();
            mapOrphanProposalVotes.clear();
        }
        {
            LOCK(cs_finalizedvotes);
            mapSeenFinalizedBudgetVotes.clear();
            mapOrphanFinalizedBudgetVotes.clear();
        }
        {
            LOCK2(cs_budgets, cs_proposals);
            mAskedUsForBudgetSync.clear();
        }

        LogPrintf("Budget object cleared\n");
    }
    void CheckAndRemove();
    std::string ToString() const;

    // Remove proposal/budget by FeeTx (called when a block is disconnected)
    void RemoveByFeeTxId(const uint256& feeTxId);

    SERIALIZE_METHODS(CBudgetManager, obj)
    {
        {
            LOCK(obj.cs_proposals);
            READWRITE(obj.mapProposals, obj.mapFeeTxToProposal);
        }
        {
            LOCK(obj.cs_votes);
            READWRITE(obj.mapSeenProposalVotes, obj.mapOrphanProposalVotes);
        }
        {
            LOCK(obj.cs_budgets);
            READWRITE(obj.mapFinalizedBudgets, obj.mapFeeTxToBudget, obj.mapUnconfirmedFeeTx);
        }
        {
            LOCK(obj.cs_finalizedvotes);
            READWRITE(obj.mapSeenFinalizedBudgetVotes, obj.mapOrphanFinalizedBudgetVotes);
        }
    }
};

extern CBudgetManager g_budgetman;

#endif // BUDGET_MANAGER_H
