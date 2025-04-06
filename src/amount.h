// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2017-2019 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_AMOUNT_H
#define BITCOIN_AMOUNT_H

#include <cstdint>
#include "serialize.h" // Ensure serialize.h is available and included if needed by CFeeRate
#include <stdlib.h>
#include <string>

/** Amount in zens (Can be negative) */
typedef int64_t CAmount;

static const CAmount COIN = 100000000;
static const CAmount CENT = 1000000;
// --- FIX: Define MAX_MONEY ---
// Set to the total supply in zens (e.g., 21 million * COIN for Bitcoin-like)
// Adjust the number (21000000) based on Digiwage's total supply if different.
static const CAmount MAX_MONEY = 21000000 * COIN;
// -----------------------------

/** Check if an amount is valid (within range) */
inline bool MoneyRange(const CAmount& nValue) { return (nValue >= 0 && nValue <= MAX_MONEY); }


/**
 * Fee rate in zens per kilobyte: CAmount / kB
 */
class CFeeRate
{
private:
    CAmount nZensPerK; // unit is zens-per-1,000-bytes
public:
    CFeeRate() : nZensPerK(0) {}
    explicit CFeeRate(const CAmount& _nZensPerK) : nZensPerK(_nZensPerK) {}
    CFeeRate(const CAmount& nFeePaid, size_t nSize); // Needs definition in amount.cpp
    CFeeRate(const CFeeRate& other) { nZensPerK = other.nZensPerK; }

    CAmount GetFee(size_t size) const;                  // Needs definition in amount.cpp
    CAmount GetFeePerK() const { return GetFee(1000); } // zens-per-1000-bytes

    friend bool operator<(const CFeeRate& a, const CFeeRate& b) { return a.nZensPerK < b.nZensPerK; }
    friend bool operator>(const CFeeRate& a, const CFeeRate& b) { return a.nZensPerK > b.nZensPerK; }
    friend bool operator==(const CFeeRate& a, const CFeeRate& b) { return a.nZensPerK == b.nZensPerK; }
    friend bool operator<=(const CFeeRate& a, const CFeeRate& b) { return a.nZensPerK <= b.nZensPerK; }
    friend bool operator>=(const CFeeRate& a, const CFeeRate& b) { return a.nZensPerK >= b.nZensPerK; }
    std::string ToString() const; // Needs definition in amount.cpp

    ADD_SERIALIZE_METHODS; // Requires serialize.h

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(nZensPerK);
    }
};

#endif //  BITCOIN_AMOUNT_H