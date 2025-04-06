// Copyright (c) 2020 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "zerocoin_verify.h"

#include "chainparams.h"
#include "consensus/consensus.h"
#include "guiinterface.h"        // for ui_interface
#include "init.h"                // for ShutdownRequested()
#include "main.h"
#include "txdb.h"

bool RecalculateWAGESupply(int nHeightStart, bool fSkipZC)
{
    const Consensus::Params& consensus = Params().GetConsensus();
    const int chainHeight = chainActive.Height();
    if (nHeightStart > chainHeight)
        return false;

    CBlockIndex* pindex = chainActive[nHeightStart];
    CAmount nSupplyPrev = pindex->pprev->nMoneySupply;

    // Reset nBurnedCoins on disk
    nBurnedCoins = 0;
    pblocktree->WriteInt("burned", 0);

    uiInterface.ShowProgress(_("Recalculating WAGE supply... "), 0);
    while (true) {
        if (pindex->nHeight % 5000 == 0) {
            LogPrintf("%s : block %d...\n", __func__, pindex->nHeight);
            int percent = std::max(1, std::min(99, (int)((double)((pindex->nHeight - nHeightStart) * 100) / (chainHeight - nHeightStart))));
            uiInterface.ShowProgress(_("Recalculating WAGE supply... "), percent);
        }

        CBlock block;
        assert(ReadBlockFromDisk(block, pindex));

        CAmount nValueIn = 0;
        CAmount nValueOut = 0;
        bool fBlockHasZerocoin = false;
        for (const CTransaction& tx : block.vtx) {
            for (unsigned int i = 0; i < tx.vin.size(); i++) {
                if (tx.IsCoinBase())
                    break;

                if (tx.vin[i].IsZerocoinSpend()) {
                    nValueIn += tx.vin[i].nSequence * COIN;
                    nBurnedCoins -= tx.vin[i].nSequence * COIN; // Zerocoin spends 're-introduce' supply to WAGE
                    fBlockHasZerocoin = true;
                    continue;
                }

                COutPoint prevout = tx.vin[i].prevout;
                CTransaction txPrev;
                uint256 hashBlock;
                assert(GetTransaction(prevout.hash, txPrev, hashBlock, true));
                nValueIn += txPrev.vout[prevout.n].nValue;
            }

            for (unsigned int i = 0; i < tx.vout.size(); i++) {
                if (i == 0 && tx.IsCoinStake())
                    continue;

                /// NOTE!
                // We consider Zerocoin Mints 'burned' due to being permanently frozen,
                // however, since there were zerocoin spends in the past, the burned ZC
                // will be deducted within the above vin[i].IsZerocoinSpend() check,
                // which ensures ONLY unspent Zerocoin is classed as burned.
                if (tx.vout[i].IsZerocoinMint()) {
                    nBurnedCoins += tx.vout[i].GetZerocoinMinted();
                    fBlockHasZerocoin = true;
                }

                if (!tx.vout[i].scriptPubKey.IsUnspendable())
                    nValueOut += tx.vout[i].nValue;    // Transacted coins
                // Else, classed as a "burn" or fee
            }
        }

        // Rewrite money supply
        CAmount nBlockValue = nValueOut - nValueIn;
        CAmount nFees = GetBlockValue(pindex->nHeight) - nBlockValue;
        if (nFees > 0) {
            // We ignore fee-calcs for blocks with Zerocoin mints/spends, as this is handled in-line, above
            if (!fBlockHasZerocoin)
                nBurnedCoins += nFees;
        }
        pindex->nMoneySupply = nSupplyPrev + nBlockValue;
        nSupplyPrev = pindex->nMoneySupply;

        // Rewrite zWAGE supply too
        if (!fSkipZC && pindex->nHeight >= consensus.height_start_ZC) {
            UpdateZWAGESupply(block, pindex);
        }

        assert(pblocktree->WriteBlockIndex(CDiskBlockIndex(pindex)));

        // Stop if shutdown was requested
        if (ShutdownRequested()) return false;

        if (pindex->nHeight < chainHeight)
            pindex = chainActive.Next(pindex);
        else
            break;
    }
    // Write burned supply to disk
    pblocktree->WriteInt("burned", std::round(nBurnedCoins / COIN));

    // Finished!
    uiInterface.ShowProgress("", 100);
    return true;
}

bool UpdateZWAGESupply(const CBlock& block, CBlockIndex* pindex)
{
    const Consensus::Params& consensus = Params().GetConsensus();
    if (pindex->nHeight < consensus.height_start_ZC)
        return true;

    // Reset the supply to previous block
    pindex->mapZerocoinSupply = pindex->pprev->mapZerocoinSupply;

    // If we're past the final accumulator; skip this entirely
    if (pindex->nHeight > consensus.height_last_ZC_AccumCheckpoint)
        return true;

    // Add mints to zWAGE supply
    std::list<CZerocoinMint> listMints;
    BlockToZerocoinMintList(block, listMints, true);
    for (const auto& m : listMints) {
        pindex->mapZerocoinSupply.at(m.GetDenomination())++;
    }

    // Remove spends from zWAGE supply
    std::list<libzerocoin::CoinDenomination> listDenomsSpent = ZerocoinSpendListFromBlock(block, true);
    for (const auto& denom : listDenomsSpent) {
        pindex->mapZerocoinSupply.at(denom)--;
    }

    for (const auto& denom : libzerocoin::zerocoinDenomList)
        LogPrint("zero", "%s coins for denomination %d pubcoin %s\n", __func__, denom, pindex->mapZerocoinSupply.at(denom));

    return true;
}