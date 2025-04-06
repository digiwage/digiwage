// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2017-2019 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "utilmoneystr.h"

#include "primitives/transaction.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "rpc/protocol.h" // For JSONRPCError
#include "rpc/server.h"   // For MoneyRange if needed (or move MoneyRange check)
#include "main.h"   // For MoneyRange if defined there

#include <limits> // For std::numeric_limits
#include <boost/lexical_cast.hpp> // For lexical_cast

bool ParseFixedPoint(const std::string& val, int decimals, int64_t* amount_out)
{
    int64_t R_MAX = std::numeric_limits<int64_t>::max() / 1; // No scaling needed here compared to original bitcoin which used 100000000
    if (val.empty())
        return false;

    size_t ptr = 0;
    int64_t sign = 1;
    if (val.size() > ptr && val[ptr] == '-') {
        sign = -1;
        ptr++;
    } else if (val.size() > ptr && val[ptr] == '+') {
        ptr++;
    }

    int64_t part_int = 0;
    while (val.size() > ptr && std::isdigit(val[ptr])) {
        if (part_int > R_MAX / 10)
            return false; // Overflow
        part_int = part_int * 10 + (val[ptr] - '0');
        ptr++;
    }

    int64_t part_frac = 0;
    int decimals_cnt = 0;
    if (val.size() > ptr && val[ptr] == '.') {
        ptr++;
        int64_t units_mult = 1;
        for (int i = 0; i < decimals; ++i) {
            units_mult *= 10;
        }
        int64_t frac_mult = units_mult / 10;
        while (val.size() > ptr && std::isdigit(val[ptr]) && decimals_cnt < decimals) {
            if (frac_mult == 0) break; // Stop if we exceed desired decimals
            part_frac += (val[ptr] - '0') * frac_mult;
            frac_mult /= 10;
            ptr++;
            decimals_cnt++;
        }
    }

    // Ensure remaining characters are whitespace or end-of-string
    while (val.size() > ptr && std::isspace(val[ptr])) {
        ptr++;
    }
    if (val.size() != ptr) {
        return false; // Trailing garbage
    }

    // Calculate the final amount in smallest units (satoshis)
    int64_t coin_mult = 1;
    for (int i = 0; i < decimals; ++i) {
        if (coin_mult > R_MAX / 10) return false; // Check overflow before multiplication
        coin_mult *= 10;
    }

    if (part_int > R_MAX / coin_mult) return false; // Check overflow before multiplication
    int64_t amount = part_int * coin_mult;

    if (part_frac > R_MAX - amount) return false; // Check overflow before addition
    amount += part_frac;

    if (amount_out) {
        *amount_out = sign * amount;
    }

    return true;
}

CAmount AmountFromValue(const UniValue& value)
{
    if (!value.isNum() && !value.isStr())
        throw JSONRPCError(RPC_TYPE_ERROR, "Amount must be a number or string");
    CAmount nAmount;
    if (!ParseFixedPoint(value.getValStr(), 8, &nAmount))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");
    if (!MoneyRange(nAmount))
        throw JSONRPCError(RPC_TYPE_ERROR, "Amount out of range");
    return nAmount;
}

UniValue ValueFromAmount(const CAmount& amount)
{
    // This function might already exist as FormatMoney or similar
    // If not, implement it to return UniValue, e.g.:
    bool sign = amount < 0;
    int64_t n_abs = (sign ? -amount : amount);
    int64_t quotient = n_abs / COIN;
    int64_t remainder = n_abs % COIN;
    // Use VNUM for precision if UniValue supports it well, else use string
    // return UniValue(UniValue::VNUM, strprintf("%s%d.%08d", sign ? "-" : "", quotient, remainder));
    // Or stick to string formatting from FormatMoney
    return FormatMoney(amount);
}


std::string FormatMoney(const CAmount& n, bool fPlus)
{
    // Note: not using straight sprintf here because we do NOT want
    // localized number formatting.
    int64_t n_abs = (n > 0 ? n : -n);
    int64_t quotient = n_abs / COIN;
    int64_t remainder = n_abs % COIN;
    std::string str = strprintf("%d.%08d", quotient, remainder);

    // Right-trim excess zeros before the decimal point:
    int nTrim = 0;
    for (int i = str.size() - 1; (str[i] == '0' && isdigit(str[i - 2])); --i)
        ++nTrim;
    if (nTrim)
        str.erase(str.size() - nTrim, nTrim);

    if (n < 0)
        str.insert((unsigned int)0, 1, '-');
    else if (fPlus && n > 0)
        str.insert((unsigned int)0, 1, '+');
    return str;
}


bool ParseMoney(const std::string& str, CAmount& nRet)
{
    return ParseMoney(str.c_str(), nRet);
}

bool ParseMoney(const char* pszIn, CAmount& nRet)
{
    std::string strWhole;
    int64_t nUnits = 0;
    const char* p = pszIn;
    while (isspace(*p))
        p++;
    for (; *p; p++) {
        if (*p == '.') {
            p++;
            int64_t nMult = CENT * 10;
            while (isdigit(*p) && (nMult > 0)) {
                nUnits += nMult * (*p++ - '0');
                nMult /= 10;
            }
            break;
        }
        if (isspace(*p))
            break;
        if (!isdigit(*p))
            return false;
        strWhole.insert(strWhole.end(), *p);
    }
    for (; *p; p++)
        if (!isspace(*p))
            return false;
    if (strWhole.size() > 10) // guard against 63 bit overflow
        return false;
    if (nUnits < 0 || nUnits > COIN)
        return false;
    int64_t nWhole = atoi64(strWhole);
    CAmount nValue = nWhole * COIN + nUnits;

    nRet = nValue;
    return true;
}
