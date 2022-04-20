// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2017-2020 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHECKPOINTS_H
#define BITCOIN_CHECKPOINTS_H

#include "uint256.h"

#include <map>

class CBlockIndex;

/** 
 * Block-chain checkpoints are compiled-in sanity checks.
 * They are updated every release or three.
 */
namespace Checkpoints
{

//! Returns true if block passes checkpoint checks
bool CheckBlock(int nHeight, const uint256& hash, bool fMatchesCheckpoint = false);

//! Return conservative estimate of total number of blocks, 0 if unknown
int GetTotalBlocksEstimate();

double GuessVerificationProgress(const CBlockIndex* pindex, bool fSigchecks = true);

extern bool fEnabled;

} //namespace Checkpoints

#endif // BITCOIN_CHECKPOINTS_H
