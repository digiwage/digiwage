// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2016-2020 The DIGIWAGE developers // Updated year
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_SCRIPT_H
#define BITCOIN_SCRIPT_SCRIPT_H

#include <assert.h>
#include <climits>
#include <limits>
#include "pubkey.h" // Ensure pubkey.h is available and included correctly
#include <stdexcept>
#include <stdint.h>
#include <string.h> // For memcpy (used internally by ReadLE*)
#include <string>
#include <vector>
#include <algorithm>
#include "crypto/common.h" // For ReadLE16, ReadLE32 - Make sure this path is correct

typedef std::vector<unsigned char> valtype;

static const unsigned int MAX_SCRIPT_ELEMENT_SIZE = 520; // bytes

// Maximum script length in bytes
static const int MAX_SCRIPT_SIZE = 10000;

// Threshold for nLockTime: below this value it is interpreted as block number,
// otherwise as UNIX timestamp.
static const unsigned int LOCKTIME_THRESHOLD = 500000000; // Tue Nov  5 00:53:20 1985 UTC

template <typename T>
std::vector<unsigned char> ToByteVector(const T& in)
{
    return std::vector<unsigned char>(in.begin(), in.end());
}

/** Script opcodes */
enum opcodetype : unsigned char // Explicitly use unsigned char for type safety
{
    // push value
    OP_0 = 0x00,
    OP_FALSE = OP_0,
    OP_PUSHDATA1 = 0x4c,
    OP_PUSHDATA2 = 0x4d,
    OP_PUSHDATA4 = 0x4e,
    OP_1NEGATE = 0x4f,
    OP_RESERVED = 0x50,
    OP_1 = 0x51,
    OP_TRUE=OP_1,
    OP_2 = 0x52,
    OP_3 = 0x53,
    OP_4 = 0x54,
    OP_5 = 0x55,
    OP_6 = 0x56,
    OP_7 = 0x57,
    OP_8 = 0x58,
    OP_9 = 0x59,
    OP_10 = 0x5a,
    OP_11 = 0x5b,
    OP_12 = 0x5c,
    OP_13 = 0x5d,
    OP_14 = 0x5e,
    OP_15 = 0x5f,
    OP_16 = 0x60,

    // control
    OP_NOP = 0x61,
    OP_VER = 0x62,
    OP_IF = 0x63,
    OP_NOTIF = 0x64,
    OP_VERIF = 0x65,
    OP_VERNOTIF = 0x66,
    OP_ELSE = 0x67,
    OP_ENDIF = 0x68,
    OP_VERIFY = 0x69,
    OP_RETURN = 0x6a,

    // stack ops
    OP_TOALTSTACK = 0x6b,
    OP_FROMALTSTACK = 0x6c,
    OP_2DROP = 0x6d,
    OP_2DUP = 0x6e,
    OP_3DUP = 0x6f,
    OP_2OVER = 0x70,
    OP_2ROT = 0x71,
    OP_2SWAP = 0x72,
    OP_IFDUP = 0x73,
    OP_DEPTH = 0x74,
    OP_DROP = 0x75,
    OP_DUP = 0x76,
    OP_NIP = 0x77,
    OP_OVER = 0x78,
    OP_PICK = 0x79,
    OP_ROLL = 0x7a,
    OP_ROT = 0x7b,
    OP_SWAP = 0x7c,
    OP_TUCK = 0x7d,

    // splice ops
    OP_CAT = 0x7e, // Disabled in later Bitcoin versions
    OP_SUBSTR = 0x7f, // Disabled in later Bitcoin versions
    OP_LEFT = 0x80, // Disabled in later Bitcoin versions
    OP_RIGHT = 0x81, // Disabled in later Bitcoin versions
    OP_SIZE = 0x82,

    // bit logic
    OP_INVERT = 0x83, // Disabled in later Bitcoin versions
    OP_AND = 0x84, // Disabled in later Bitcoin versions
    OP_OR = 0x85, // Disabled in later Bitcoin versions
    OP_XOR = 0x86, // Disabled in later Bitcoin versions
    OP_EQUAL = 0x87,
    OP_EQUALVERIFY = 0x88,
    OP_RESERVED1 = 0x89,
    OP_RESERVED2 = 0x8a,

    // numeric
    OP_1ADD = 0x8b,
    OP_1SUB = 0x8c,
    OP_2MUL = 0x8d, // Disabled in later Bitcoin versions
    OP_2DIV = 0x8e, // Disabled in later Bitcoin versions
    OP_NEGATE = 0x8f,
    OP_ABS = 0x90,
    OP_NOT = 0x91,
    OP_0NOTEQUAL = 0x92,

    OP_ADD = 0x93,
    OP_SUB = 0x94,
    OP_MUL = 0x95, // Disabled in later Bitcoin versions
    OP_DIV = 0x96, // Disabled in later Bitcoin versions
    OP_MOD = 0x97, // Disabled in later Bitcoin versions
    OP_LSHIFT = 0x98, // Disabled in later Bitcoin versions
    OP_RSHIFT = 0x99, // Disabled in later Bitcoin versions

    OP_BOOLAND = 0x9a,
    OP_BOOLOR = 0x9b,
    OP_NUMEQUAL = 0x9c,
    OP_NUMEQUALVERIFY = 0x9d,
    OP_NUMNOTEQUAL = 0x9e,
    OP_LESSTHAN = 0x9f,
    OP_GREATERTHAN = 0xa0,
    OP_LESSTHANOREQUAL = 0xa1,
    OP_GREATERTHANOREQUAL = 0xa2,
    OP_MIN = 0xa3,
    OP_MAX = 0xa4,

    OP_WITHIN = 0xa5,

    // crypto
    OP_RIPEMD160 = 0xa6,
    OP_SHA1 = 0xa7,
    OP_SHA256 = 0xa8,
    OP_HASH160 = 0xa9,
    OP_HASH256 = 0xaa,
    OP_CODESEPARATOR = 0xab,
    OP_CHECKSIG = 0xac,
    OP_CHECKSIGVERIFY = 0xad,
    OP_CHECKMULTISIG = 0xae,
    OP_CHECKMULTISIGVERIFY = 0xaf,

    // expansion
    OP_NOP1 = 0xb0,
    OP_NOP2 = 0xb1,
    OP_CHECKLOCKTIMEVERIFY = OP_NOP2, // NOTE: Uses NOP2 value
    OP_NOP3 = 0xb2,
    // OP_CHECKSEQUENCEVERIFY = OP_NOP3, // Not present in original DigiWage, keep NOP3
    OP_NOP4 = 0xb3,
    OP_NOP5 = 0xb4,
    OP_NOP6 = 0xb5,
    OP_NOP7 = 0xb6,
    OP_NOP8 = 0xb7,
    OP_NOP9 = 0xb8,
    OP_NOP10 = 0xb9,

    // zerocoin (Keep DigiWage specific values)
    OP_ZEROCOINMINT = 0xc1,
    OP_ZEROCOINSPEND = 0xc2, // DigiWage zPIV spend

    // EVM opcodes (Using DigiWage specific values)
    OP_SPEND = 0xc3, // Note: This is likely *NOT* EVM related, but a DigiWage-specific opcode. Keep the name if used internally.
    OP_SENDER = 0xc4, // DigiWage specific value for EVM sender opcode
    OP_CREATE = 0xc5, // DigiWage specific value for EVM create opcode
    OP_CALL = 0xc6, // DigiWage specific value for EVM call opcode

    // cold staking (Keep DigiWage specific value)
    OP_CHECKCOLDSTAKEVERIFY = 0xd1,

    // template matching params
    OP_SMALLINTEGER = 0xfa,
    OP_PUBKEYS = 0xfb,
    OP_PUBKEYHASH = 0xfd,
    OP_PUBKEY = 0xfe,

    OP_INVALIDOPCODE = 0xff,
};

const char* GetOpName(opcodetype opcode); // Implementation expected in script.cpp

class scriptnum_error : public std::runtime_error
{
public:
    explicit scriptnum_error(const std::string& str) : std::runtime_error(str) {}
};

class CScriptNum
{
/**
 * Numeric opcodes (OP_1ADD, etc) are restricted to operating on 4-byte integers.
 * The semantics are subtle, though: operands must be in the range [-2^31 +1...2^31 -1],
 * but results may overflow (and are valid as long as they are not used in a subsequent
 * numeric operation). CScriptNum enforces those semantics by storing results as
 * an int64 and allowing out-of-range values to be returned as a vector of bytes but
 * throwing an exception if arithmetic is done or the result is interpreted as an integer.
 */
public:
    explicit CScriptNum(const int64_t& n)
    {
        m_value = n;
    }

    static const size_t nDefaultMaxNumSize = 4;

    explicit CScriptNum(const std::vector<unsigned char>& vch, bool fRequireMinimal,
            const size_t nMaxNumSize = nDefaultMaxNumSize)
    {
        if (vch.size() > nMaxNumSize) {
            throw scriptnum_error("script number overflow");
        }
        if (fRequireMinimal && vch.size() > 0) {
            // Check that the number is encoded with the minimum possible
            // number of bytes.
            //
            // If the most-significant-byte - excluding the sign bit - is zero
            // then we're not minimal. Note how this test also rejects the
            // negative-zero encoding, 0x80.
            if ((vch.back() & 0x7f) == 0) {
                // One exception: if there's more than one byte and the most
                // significant bit of the second-most-significant-byte is set
                // it would conflict with the sign bit. An example of this case
                // is +-255, which encode to 0xff00 and 0xff80 respectively.
                // (big-endian).
                if (vch.size() <= 1 || (vch[vch.size() - 2] & 0x80) == 0) {
                    throw scriptnum_error("non-minimally encoded script number");
                }
            }
        }
        m_value = set_vch(vch);
    }

    inline bool operator==(const int64_t& rhs) const    { return m_value == rhs; }
    inline bool operator!=(const int64_t& rhs) const    { return m_value != rhs; }
    inline bool operator<=(const int64_t& rhs) const    { return m_value <= rhs; }
    inline bool operator< (const int64_t& rhs) const    { return m_value <  rhs; }
    inline bool operator>=(const int64_t& rhs) const    { return m_value >= rhs; }
    inline bool operator> (const int64_t& rhs) const    { return m_value >  rhs; }

    inline bool operator==(const CScriptNum& rhs) const { return operator==(rhs.m_value); }
    inline bool operator!=(const CScriptNum& rhs) const { return operator!=(rhs.m_value); }
    inline bool operator<=(const CScriptNum& rhs) const { return operator<=(rhs.m_value); }
    inline bool operator< (const CScriptNum& rhs) const { return operator< (rhs.m_value); }
    inline bool operator>=(const CScriptNum& rhs) const { return operator>=(rhs.m_value); }
    inline bool operator> (const CScriptNum& rhs) const { return operator> (rhs.m_value); }

    inline CScriptNum operator+(   const int64_t& rhs)    const { return CScriptNum(m_value + rhs);}
    inline CScriptNum operator-(   const int64_t& rhs)    const { return CScriptNum(m_value - rhs);}
    inline CScriptNum operator+(   const CScriptNum& rhs) const { return operator+(rhs.m_value);   }
    inline CScriptNum operator-(   const CScriptNum& rhs) const { return operator-(rhs.m_value);   }

    inline CScriptNum& operator+=( const CScriptNum& rhs)       { return operator+=(rhs.m_value);  }
    inline CScriptNum& operator-=( const CScriptNum& rhs)       { return operator-=(rhs.m_value);  }

    // Prefix unary minus operator
    inline CScriptNum operator-() const
    {
        // Check for possible overflow when negating the minimum value
        if (m_value == std::numeric_limits<int64_t>::min()) {
            throw scriptnum_error("cannot negate minimum int64_t value");
        }
        return CScriptNum(-m_value);
    }

    inline CScriptNum& operator=( const int64_t& rhs)
    {
        m_value = rhs;
        return *this;
    }

    inline CScriptNum& operator+=( const int64_t& rhs)
    {
        // Check for potential overflow before addition
        if (rhs > 0 && m_value > std::numeric_limits<int64_t>::max() - rhs) {
            throw scriptnum_error("addition overflow");
        }
        if (rhs < 0 && m_value < std::numeric_limits<int64_t>::min() - rhs) {
            throw scriptnum_error("addition underflow");
        }
        m_value += rhs;
        return *this;
    }

    inline CScriptNum& operator-=( const int64_t& rhs)
    {
        // Check for potential overflow before subtraction
        if (rhs > 0 && m_value < std::numeric_limits<int64_t>::min() + rhs) {
            throw scriptnum_error("subtraction underflow");
        }
        if (rhs < 0 && m_value > std::numeric_limits<int64_t>::max() + rhs) {
            throw scriptnum_error("subtraction overflow");
        }
        m_value -= rhs;
        return *this;
    }

    int getint() const
    {
        if (m_value > std::numeric_limits<int>::max())
            return std::numeric_limits<int>::max();
        else if (m_value < std::numeric_limits<int>::min())
            return std::numeric_limits<int>::min();
        return static_cast<int>(m_value); // Use static_cast
    }

    int64_t GetInt64() const { return m_value; } // Keep this helpful getter

    std::vector<unsigned char> getvch() const
    {
        return serialize(m_value);
    }

    static std::vector<unsigned char> serialize(const int64_t& value)
    {
        if(value == 0)
            return std::vector<unsigned char>();

        std::vector<unsigned char> result;
        const bool neg = value < 0;
        // Use uint64_t for absolute value to handle INT64_MIN correctly
        uint64_t absvalue = neg ? static_cast<uint64_t>(-(value + 1)) + 1 : static_cast<uint64_t>(value);

        while(absvalue > 0) // Use > 0 instead of non-zero check
        {
            result.push_back(absvalue & 0xff);
            absvalue >>= 8;
        }

        //    - If the most significant byte is >= 0x80 and the value is positive, push a
        //    new zero-byte to make the significant byte < 0x80 again.

        //    - If the most significant byte is >= 0x80 and the value is negative, push a
        //    new 0x80 byte that will be popped off when converting back to an integral.

        //    - If the most significant byte is < 0x80 and the value is negative, add
        //    0x80 to it, since it will be subtracted and interpreted as a negative when
        //    converting back to an integral.

        if (result.back() & 0x80) {
            result.push_back(neg ? 0x80 : 0);
        } else if (neg) {
            result.back() |= 0x80;
        }
        return result;
    }

private:
    static int64_t set_vch(const std::vector<unsigned char>& vch)
    {
      if (vch.empty())
          return 0;

      int64_t result = 0;
      for (size_t i = 0; i != vch.size(); ++i)
          result |= static_cast<int64_t>(vch[i]) << (8*i); // Use parentheses for clarity

      // If the input vector's most significant byte is 0x80, remove it from
      // the result's msb and return a negative.
      if (vch.back() & 0x80) {
          // Correctly handle the negation for INT64_MIN case
          uint64_t temp = result & ~(0x80ULL << (8 * (vch.size() - 1)));
          if (temp == 0 && vch.size() > 1 && (vch[vch.size()-2] & 0x80)) {
              // This handles the specific case for INT64_MIN which serializes to {0x00, ..., 0x80}
               return std::numeric_limits<int64_t>::min();
          }
          // Correctly cast the negated value back to int64_t
          return -static_cast<int64_t>(temp);
      }

      return result;
    }

    int64_t m_value;
};


/** Serialized script, used inside transaction inputs and outputs */
class CScript : public std::vector<unsigned char>
{
protected:
    CScript& push_int64(int64_t n)
    {
        if (n == -1 || (n >= 1 && n <= 16))
        {
            push_back(n + (OP_1 - 1));
        }
        else if (n == 0)
        {
            push_back(OP_0);
        }
        else
        {
            *this << CScriptNum(n).getvch(); // Use CScriptNum constructor and getvch
        }
        return *this;
    }
public:
    CScript() = default; // Use default constructor
    CScript(const CScript& b) : std::vector<unsigned char>(b.begin(), b.end()) { }
    CScript(const_iterator pbegin, const_iterator pend) : std::vector<unsigned char>(pbegin, pend) { }
    // Use Span<const unsigned char> constructor if available (more modern C++)
    // CScript(Span<const unsigned char> b) : std::vector<unsigned char>(b.begin(), b.end()) {}
    CScript(const unsigned char* pbegin, const unsigned char* pend) : std::vector<unsigned char>(pbegin, pend) { }

    // Define explicit copy assignment operator to avoid deprecated warning
    CScript& operator=(const CScript& b) {
        static_cast<std::vector<unsigned char>&>(*this) = static_cast<const std::vector<unsigned char>&>(b);
        return *this;
    }
    // Optionally define move constructor and move assignment operator for efficiency
    CScript(CScript&& b) noexcept : std::vector<unsigned char>(std::move(b)) {}
    CScript& operator=(CScript&& b) noexcept {
        static_cast<std::vector<unsigned char>&>(*this) = std::move(static_cast<std::vector<unsigned char>&>(b));
        return *this;
    }


    CScript& operator+=(const CScript& b)
    {
        insert(end(), b.begin(), b.end());
        return *this;
    }

    friend CScript operator+(const CScript& a, const CScript& b)
    {
        CScript ret = a;
        ret += b;
        return ret;
    }

    // Constructors for easy script creation
    explicit CScript(int64_t b)        { operator<<(b); }
    explicit CScript(opcodetype b)     { operator<<(b); }
    explicit CScript(const CScriptNum& b) { operator<<(b); }
    explicit CScript(const std::vector<unsigned char>& b) { operator<<(b); }


    CScript& operator<<(int64_t b) { return push_int64(b); }

    CScript& operator<<(opcodetype opcode)
    {
        // No change needed here, but ensure opcode value is checked
        if (opcode < 0 || opcode > 0xff) // Should not happen with enum : unsigned char
            throw std::runtime_error("CScript::operator<<() : invalid opcode");
        insert(end(), static_cast<unsigned char>(opcode));
        return *this;
    }

    CScript& operator<<(const CScriptNum& b)
    {
        *this << b.getvch();
        return *this;
    }

    CScript& operator<<(const std::vector<unsigned char>& b)
    {
        if (b.size() < OP_PUSHDATA1)
        {
            insert(end(), (unsigned char)b.size());
        }
        else if (b.size() <= 0xff)
        {
            insert(end(), OP_PUSHDATA1);
            insert(end(), (unsigned char)b.size());
        }
        else if (b.size() <= 0xffff)
        {
            insert(end(), OP_PUSHDATA2);
            uint16_t nSize = b.size(); // Use uint16_t
            // Write little-endian
            unsigned char size_bytes[2];
            WriteLE16(size_bytes, nSize); // Use WriteLE helper
            insert(end(), size_bytes, size_bytes + sizeof(nSize));
        }
        else
        {
            insert(end(), OP_PUSHDATA4);
            uint32_t nSize = b.size(); // Use uint32_t
            // Write little-endian
            unsigned char size_bytes[4];
            WriteLE32(size_bytes, nSize); // Use WriteLE helper
            insert(end(), size_bytes, size_bytes + sizeof(nSize));
        }
        insert(end(), b.begin(), b.end());
        return *this;
    }

    // Remove the problematic CScript << CScript operator
    // CScript& operator<<(const CScript& b) = delete; // Explicitly delete it

    CScript& operator<<(const CPubKey& key)
    {
        if (!key.IsValid()) {
             throw std::runtime_error("CScript::operator<<: Pushing an invalid pubkey");
        }
        // Push the key bytes directly using the vector overload
        // Assuming CPubKey provides begin()/end() iterators or a way to get its bytes
        std::vector<unsigned char> vchKey(key.begin(), key.end());
        return (*this) << vchKey;
    }


    bool GetOp(iterator& pc, opcodetype& opcodeRet, std::vector<unsigned char>& vchRet)
    {
         // Wrapper so it can be called with either iterator or const_iterator
         const_iterator pc2 = pc;
         bool fRet = GetOp2(pc2, opcodeRet, &vchRet);
         pc = begin() + (pc2 - begin());
         return fRet;
    }

    bool GetOp(iterator& pc, opcodetype& opcodeRet)
    {
         const_iterator pc2 = pc;
         bool fRet = GetOp2(pc2, opcodeRet, nullptr); // Use nullptr instead of NULL
         pc = begin() + (pc2 - begin());
         return fRet;
    }

    bool GetOp(const_iterator& pc, opcodetype& opcodeRet, std::vector<unsigned char>& vchRet) const
    {
        return GetOp2(pc, opcodeRet, &vchRet);
    }

    bool GetOp(const_iterator& pc, opcodetype& opcodeRet) const
    {
        return GetOp2(pc, opcodeRet, nullptr); // Use nullptr instead of NULL
    }

    // Keep DigiWage's GetOp2 implementation, but ensure ReadLE* usage is correct
    bool GetOp2(const_iterator& pc, opcodetype& opcodeRet, std::vector<unsigned char>* pvchRet) const
    {
        opcodeRet = OP_INVALIDOPCODE;
        if (pvchRet)
            pvchRet->clear();
        if (pc >= end())
            return false;

        // Read instruction
        if (end() - pc < 1)
            return false;
        unsigned int opcode = *pc++;

        // Immediate operand
        if (opcode <= OP_PUSHDATA4)
        {
            unsigned int nSize = 0;
            if (opcode < OP_PUSHDATA1)
            {
                nSize = opcode;
            }
            else if (opcode == OP_PUSHDATA1)
            {
                if (end() - pc < 1)
                    return false;
                nSize = *pc++;
            }
            else if (opcode == OP_PUSHDATA2)
            {
                if (end() - pc < 2)
                    return false;
                nSize = ReadLE16(&*pc); // Assumes ReadLE16 takes const unsigned char*
                pc += 2;
            }
            else if (opcode == OP_PUSHDATA4)
            {
                if (end() - pc < 4)
                    return false;
                nSize = ReadLE32(&*pc); // Assumes ReadLE32 takes const unsigned char*
                pc += 4;
            }
            // Avoid integer wrap-around (check if adding nSize would exceed the end iterator)
             if (nSize > static_cast<uint32_t>(end() - pc)) { // Use uint32_t for comparison, safer cast
                 return false;
             }
            if (pvchRet)
                pvchRet->assign(pc, pc + nSize);
            pc += nSize;
        }

        opcodeRet = (opcodetype)opcode;
        return true;
    }

    /** Encode/decode small integers: */
    static int DecodeOP_N(opcodetype opcode)
    {
        // Opcodes >= OP_1NEGATE are not numbers
        if (opcode < OP_0 || opcode > OP_16) {
             throw std::runtime_error("DecodeOP_N: non-numeric opcode");
        }
        if (opcode == OP_0)
            return 0;
        return (int)opcode - (int)(OP_1 - 1);
    }
    static opcodetype EncodeOP_N(int n)
    {
        if (n < 0 || n > 16) {
            throw std::runtime_error("EncodeOP_N: out of range");
        }
        if (n == 0)
            return OP_0;
        return (opcodetype)(OP_1+n-1);
    }

    // Keep DigiWage's FindAndDelete
    int FindAndDelete(const CScript& b)
    {
        int nFound = 0;
        if (b.empty())
            return nFound;
        CScript result;
        iterator pc = begin(), pc2 = begin();
        opcodetype opcode;
        std::vector<unsigned char> vchPushValue; // Needed for robust iteration
        do
        {
            result.insert(result.end(), pc2, pc);
            // Robust check: Compare elements only if sizes match
            while (static_cast<size_t>(end() - pc) >= b.size() &&
                   std::equal(b.begin(), b.end(), pc)) // Use std::equal for comparison
            {
                pc = pc + b.size();
                ++nFound;
            }
            pc2 = pc;
        }
        // Robust iteration: Pass pvchRet to GetOp to handle pushdata correctly
        while (GetOp(pc, opcode, vchPushValue)); // Use the version that extracts push data

        if (nFound > 0) {
            result.insert(result.end(), pc2, end());
            *this = result;
        }
        return nFound;
    }

    // Keep DigiWage's Find
    int Find(opcodetype op) const
    {
        int nFound = 0;
        opcodetype opcode;
        std::vector<unsigned char> vchPushValue; // Needed for robust iteration
        for (const_iterator pc = begin(); pc != end() && GetOp(pc, opcode, vchPushValue); ) { // Use version with pvchRet
            if (opcode == op)
                ++nFound;
        }
        return nFound;
    }

    unsigned int GetSigOpCount(bool fAccurate) const; // Implementation expected in script.cpp
    unsigned int GetSigOpCount(const CScript& scriptSig) const; // Implementation expected in script.cpp

    // Keep DigiWage's script type checks
    bool IsNormalPaymentScript() const;
    bool IsPayToScriptHash() const;
    bool IsPayToColdStaking() const;
    bool IsPayToPublicKeyHash() const; // Added declaration based on EVM helpers
    bool IsPayToPublicKey() const;     // Added declaration based on EVM helpers
    bool StartsWithOpcode(const opcodetype opcode) const; // Needs implementation or make inline
    bool IsZerocoinMint() const;
    bool IsZerocoinSpend() const;

    /** Called by IsStandardTx and P2SH/BIP62 VerifyScript (which makes it consensus-critical). */
    bool IsPushOnly(const_iterator pc) const;
    bool IsPushOnly() const;

    bool IsUnspendable() const
    {
        return (size() > 0 && (*begin() == OP_RETURN || *begin() == OP_INVALIDOPCODE)) || (size() > MAX_SCRIPT_SIZE);
    }

    // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    // +++ ADDED HELPER METHODS FROM QTUM +++++++++++++++++++++++
    // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    bool HasOpCreate() const
    {
        return Find(OP_CREATE) > 0; // Uses corrected Find method
    }

    bool HasOpCall() const
    {
        return Find(OP_CALL) > 0; // Uses corrected Find method
    }

    // Note: OP_SPEND (0xc3) in DigiWage is likely NOT the EVM OP_SPEND.
    // This function checks for DigiWage's zPIV spend opcode.
    bool HasOpSpend() const
    {
        return IsZerocoinSpend(); // Use the specific DigiWage zPIV check
    }

    bool HasOpSender() const
    {
        return Find(OP_SENDER) > 0; // Uses corrected Find method
    }
    // Implementations for P2PKH/P2PK checks are needed in script.cpp
    // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    // +++ END OF ADDED HELPER METHODS ++++++++++++++++++++++++++
    // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


    std::string ToString() const; // Implementation expected in script.cpp

    void clear()
    {
        // The default std::vector::clear() does not release memory.
        CScript().swap(*this); // Use swap trick for efficiency
    }
};


#endif // BITCOIN_SCRIPT_SCRIPT_H