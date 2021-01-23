// Copyright (c) 2020 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIWAGE_CONSENSUS_ZEROCOIN_VERIFY_H
#define DIGIWAGE_CONSENSUS_ZEROCOIN_VERIFY_H

#include "consensus/consensus.h"
#include "main.h"
#include "script/interpreter.h"
#include "zpivchain.h"

// Public coin spend
bool RecalculateWAGESupply(int nHeightStart, bool fSkipZC = true);
bool UpdateZWAGESupply(const CBlock& block, CBlockIndex* pindex);

#endif //DIGIWAGE_CONSENSUS_ZEROCOIN_VERIFY_H
