#ifndef POSUTILS_H
#define POSUTILS_H

#include <uint256.h>
#include <consensus/amount.h>

struct CStakeCache{
    CStakeCache(uint32_t blockFromTime_, CAmount amount_, int blockFromHeight_ = -1) : blockFromTime(blockFromTime_), amount(amount_), blockFromHeight(blockFromHeight_){
    }
    uint32_t blockFromTime;
    CAmount amount;
    int blockFromHeight;
};

struct Delegation
{
    Delegation():
        fee(0),
        blockHeight(0) {}

    bool IsNull() const
    {
        return staker == uint160() &&
                fee == 0 &&
                blockHeight == 0 &&
                PoD == std::vector<unsigned char>();
    }

    uint160 staker;
    uint8_t fee;
    uint32_t blockHeight;
    std::vector<unsigned char> PoD; //Proof Of Delegation
};

inline bool operator==(const Delegation& lhs, const Delegation& rhs)
{
    return lhs.staker == rhs.staker &&
           lhs.fee == rhs.fee &&
           lhs.blockHeight == rhs.blockHeight &&
           lhs.PoD == rhs.PoD;
}

inline bool operator!=(const Delegation& lhs, const Delegation& rhs)
{
    return !(lhs == rhs);
}

#endif
