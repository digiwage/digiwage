#ifndef DIGIWAGE_DIGIWAGE_POS_H
#define DIGIWAGE_DIGIWAGE_POS_H

#include <uint256.h>

class CBlock;
class CBlockIndex;
class CCoinsViewCache;
class BlockValidationState;

bool CheckDigiwageKernel(const CBlock& block, const CBlockIndex* prev,
                         CCoinsViewCache& view, BlockValidationState& state,
                         uint256& proof_hash);

#endif
