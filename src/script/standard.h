// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2017-2019 The DIGIWAGE developers // Updated Copyright Year
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_STANDARD_H
#define BITCOIN_SCRIPT_STANDARD_H

#include "script/interpreter.h"
#include "uint256.h"
#include "script/script.h" // Include base class definition

#include <boost/variant.hpp>

#include <stdint.h>
#include <vector> // Include vector as it's used directly

class CKeyID;
// class CScript; // Forward declaration no longer needed if script.h included

/** A reference to a CScript: the Hash160 of its serialization (see script.h) */
class CScriptID : public uint160
{
public:
    CScriptID() : uint160() {}
    CScriptID(const CScript& in); // Definition likely in standard.cpp
    CScriptID(const uint160& in) : uint160(in) {}
};

// Define necessary constants that were previously causing errors
static const unsigned int STANDARD_SENDER_STACK_ITEMS = 2;
static const unsigned int MAX_STANDARD_SENDER_STACK_ITEM_SIZE = 520; // Size from Qtum

static const unsigned int MAX_OP_RETURN_RELAY = 83;      //!< bytes (+1 for OP_RETURN, +2 for the pushdata opcodes)

extern unsigned nMaxDatacarrierBytes; // Definition likely in policy.cpp or similar

/** Script verification flags - These should ideally live ONLY in interpreter.h **/
// static const unsigned int MANDATORY_SCRIPT_VERIFY_FLAGS = SCRIPT_VERIFY_P2SH; // Use from interpreter.h
// static const unsigned int STANDARD_SCRIPT_VERIFY_FLAGS = ...; // Use from interpreter.h
// static const unsigned int STANDARD_NOT_MANDATORY_VERIFY_FLAGS = ...; // Use from interpreter.h

enum txnouttype
{
    TX_NONSTANDARD,
    // 'standard' transaction types:
    TX_PUBKEY,
    TX_PUBKEYHASH,
    TX_SCRIPTHASH,
    TX_MULTISIG,
    TX_NULL_DATA,
    TX_ZEROCOINMINT, // Keep DigiWage specific types
    TX_COLDSTAKE     // Keep DigiWage specific types
};

class CNoDestination {
public:
    friend bool operator==(const CNoDestination &a, const CNoDestination &b) { return true; }
    friend bool operator<(const CNoDestination &a, const CNoDestination &b) { return true; }
};

/**
 * A txout script template with a specific destination. It is either:
 *  * CNoDestination: no destination set
 *  * CKeyID: TX_PUBKEYHASH destination
 *  * CScriptID: TX_SCRIPTHASH destination
 *  A CTxDestination is the internal data type encoded in a CBitcoinAddress
 */
typedef boost::variant<CNoDestination, CKeyID, CScriptID> CTxDestination;

const char* GetTxnOutputType(txnouttype t);

bool Solver(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<std::vector<unsigned char> >& vSolutionsRet);
int ScriptSigArgsExpected(txnouttype t, const std::vector<std::vector<unsigned char> >& vSolutions);
bool IsStandard(const CScript& scriptPubKey, txnouttype& whichType);
bool ExtractDestination(const CScript& scriptPubKey, CTxDestination& addressRet, bool fColdStake = false);
bool ExtractDestinations(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<CTxDestination>& addressRet, int& nRequiredRet);

CScript GetScriptForDestination(const CTxDestination& dest);
CScript GetLockedScriptForDestination(const CTxDestination& dest, int nLockTime);
CScript GetScriptForRawPubKey(const CPubKey& pubKey);
CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey>& keys);
CScript GetScriptForStakeDelegation(const CKeyID& stakingKey, const CKeyID& spendingKey); // Keep DigiWage specific

// --- Add missing declarations to CScript class ---
// It's assumed CScript is defined in script/script.h which should be included above
// If CScript is not defined yet, this won't work. Let's assume script.h is included.

// Add these declarations inside the CScript class definition in script/script.h (preferred)
// or redeclare the class here temporarily if needed (less ideal).
/* Example assuming inclusion of script.h where CScript is defined: */
class CPubKey; // Forward declare

// Add inside the public section of class CScript (defined in script/script.h)
/*
public:
    // ... existing public members ...
    bool IsPayToPublicKeyHash() const;
    bool IsPayToPublicKey() const;
    // ...
*/
// If you absolutely cannot modify script.h, you could potentially declare them
// as standalone functions taking a const CScript&, but modifying the class is better.
// Add globally in script/standard.h (NOT inside a class)


#endif // BITCOIN_SCRIPT_STANDARD_H