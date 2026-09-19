#ifndef DIGIWAGE_DIGIWAGE_POS_H
#define DIGIWAGE_DIGIWAGE_POS_H

#include <consensus/amount.h>
#include <uint256.h>

class CBlock;
class CBlockIndex;
class COutPoint;
class CCoinsViewCache;
class BlockValidationState;

bool CheckDigiwageKernel(const CBlock& block, const CBlockIndex* prev,
                         CCoinsViewCache& view, BlockValidationState& state,
                         uint256& proof_hash);

// Shared kernel test used by both block validation and the staker, so a
// kernel the wallet finds is exactly one that consensus accepts.
bool CheckDigiwageStakeKernel(const CBlockIndex* prev, unsigned int nBits,
                              const CBlockIndex* origin, CAmount value,
                              const COutPoint& prevout, uint32_t nTimeBlock,
                              uint256& proof_hash);

#endif
