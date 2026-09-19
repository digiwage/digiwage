#ifndef DIGIWAGE_DIGIWAGE_STAKEMODIFIER_H
#define DIGIWAGE_DIGIWAGE_STAKEMODIFIER_H

#include <cstdint>

class CBlockIndex;

// `first_modifier` must come from verified historical state. The original
// source converted a string-literal address to uint64_t at height one.
bool ComputeDigiwageStakeModifier(const CBlockIndex* prev, uint64_t first_modifier,
                                  uint64_t& modifier, bool& generated);
bool GetDigiwageOldModifier(const CBlockIndex* origin, const CBlockIndex* tip,
                           uint64_t& modifier);

#endif
