// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2017-2019 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "script/standard.h"

#include "pubkey.h"
#include "script/script.h"
#include "util.h"
#include "utilstrencodings.h"

typedef std::vector<unsigned char> valtype;

unsigned nMaxDatacarrierBytes = MAX_OP_RETURN_RELAY;

CScriptID::CScriptID(const CScript& in) : uint160(Hash160(in.begin(), in.end())) {}

const char* GetTxnOutputType(txnouttype t)
{
    switch (t)
    {
    case TX_NONSTANDARD: return "nonstandard";
    case TX_PUBKEY: return "pubkey";
    case TX_PUBKEYHASH: return "pubkeyhash";
    case TX_SCRIPTHASH: return "scripthash";
    case TX_MULTISIG: return "multisig";
    case TX_COLDSTAKE: return "coldstake";
    case TX_NULL_DATA: return "nulldata";
    case TX_ZEROCOINMINT: return "zerocoinmint";
    }
    return NULL;
}


// Add these function definitions to script/standard.cpp
bool CScript::IsPayToPublicKeyHash() const
{
    // Returns true if the script is a conventional pay-to-pubkey-hash script
    return size() == 25 &&
           (*this)[0] == OP_DUP &&
           (*this)[1] == OP_HASH160 &&
           (*this)[2] == 0x14 &&        // Push 20 bytes
           (*this)[23] == OP_EQUALVERIFY &&
           (*this)[24] == OP_CHECKSIG;
}

bool CScript::IsPayToPublicKey() const
{
    // Returns true if the script is a conventional pay-to-pubkey script
    // Format: <pubkey> OP_CHECKSIG
    // Sizes: 33 bytes (compressed pubkey) + 1 byte opcode + 1 byte OP_CHECKSIG = 35 bytes
    //     OR 65 bytes (uncompressed pubkey) + 1 byte opcode + 1 byte OP_CHECKSIG = 67 bytes

    // 1. Check total size and final opcode
    if ((this->size() != 35 && this->size() != 67) || (*this)[this->size() - 1] != OP_CHECKSIG) {
        return false;
    }

    // 2. Check that the first operation is a data push of the correct size
    CScript::const_iterator pc = this->begin();
    opcodetype opcode;
    std::vector<unsigned char> vch;
    if (!this->GetOp(pc, opcode, vch)) {
        // Failed to parse first operation
        return false;
    }

    // 3. Check if the opcode was a push operation and the size is correct for a pubkey
    // (Opcodes 1-75 are direct pushes, PUSHDATA1/2/4 are pushes, > OP_16 covers these)
    if (opcode < OP_1 || opcode > OP_PUSHDATA4 || (vch.size() != 33 && vch.size() != 65) ) {
         // Not a push opcode, or pushed data size is wrong for a pubkey
         return false;
    }


    // 4. Check that after reading the push op, we are exactly at the OP_CHECKSIG
    if (pc != this->end() - 1) {
        // There was more data/opcodes after the pubkey push but before OP_CHECKSIG
        return false;
    }

    // Optional stricter check: Validate the pushed public key itself
    // CPubKey pubkey(vch);
    // if (!pubkey.IsValid()) return false;


    return true; // Structure matches P2PK
}


/**
 * Return public keys or hashes from scriptPubKey, for 'standard' transaction types.
 */
bool Solver(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<std::vector<unsigned char> >& vSolutionsRet)
{
    // Templates
    static std::multimap<txnouttype, CScript> mTemplates;
    if (mTemplates.empty())
    {
        // Standard tx, sender provides pubkey, receiver adds signature
        mTemplates.insert(std::make_pair(TX_PUBKEY, CScript() << OP_PUBKEY << OP_CHECKSIG));

        // Bitcoin address tx, sender provides hash of pubkey, receiver provides signature and pubkey
        mTemplates.insert(std::make_pair(TX_PUBKEYHASH, CScript() << OP_DUP << OP_HASH160 << OP_PUBKEYHASH << OP_EQUALVERIFY << OP_CHECKSIG));

        // Sender provides N pubkeys, receivers provides M signatures
        mTemplates.insert(std::make_pair(TX_MULTISIG, CScript() << OP_SMALLINTEGER << OP_PUBKEYS << OP_SMALLINTEGER << OP_CHECKMULTISIG));

        // Cold Staking: sender provides P2CS scripts, receiver provides signature, staking-flag and pubkey
        mTemplates.insert(std::make_pair(TX_COLDSTAKE, CScript() << OP_DUP << OP_HASH160 << OP_ROT << OP_IF << OP_CHECKCOLDSTAKEVERIFY <<
                OP_PUBKEYHASH << OP_ELSE << OP_PUBKEYHASH << OP_ENDIF << OP_EQUALVERIFY << OP_CHECKSIG));
    }

    // Shortcut for pay-to-script-hash, which are more constrained than the other types:
    // it is always OP_HASH160 20 [20 byte hash] OP_EQUAL
    if (scriptPubKey.IsPayToScriptHash())
    {
        typeRet = TX_SCRIPTHASH;
        std::vector<unsigned char> hashBytes(scriptPubKey.begin()+2, scriptPubKey.begin()+22);
        vSolutionsRet.push_back(hashBytes);
        return true;
    }

    // Zerocoin
    if (scriptPubKey.IsZerocoinMint()) {
        typeRet = TX_ZEROCOINMINT;
        if(scriptPubKey.size() > 150) return false;
        std::vector<unsigned char> hashBytes(scriptPubKey.begin()+2, scriptPubKey.end());
        vSolutionsRet.push_back(hashBytes);
        return true;
    }

    // Provably prunable, data-carrying output
    //
    // So long as script passes the IsUnspendable() test and all but the first
    // byte passes the IsPushOnly() test we don't care what exactly is in the
    // script.
    if (scriptPubKey.size() >= 1 && scriptPubKey[0] == OP_RETURN && scriptPubKey.IsPushOnly(scriptPubKey.begin()+1)) {
        typeRet = TX_NULL_DATA;
        return true;
    }

    // Scan templates
    const CScript& script1 = scriptPubKey;
    for (const PAIRTYPE(txnouttype, CScript)& tplate : mTemplates)
    {
        const CScript& script2 = tplate.second;
        vSolutionsRet.clear();

        opcodetype opcode1, opcode2;
        std::vector<unsigned char> vch1, vch2;

        // Compare
        CScript::const_iterator pc1 = script1.begin();
        CScript::const_iterator pc2 = script2.begin();
        while (true)
        {
            if (pc1 == script1.end() && pc2 == script2.end())
            {
                // Found a match
                typeRet = tplate.first;
                if (typeRet == TX_MULTISIG)
                {
                    // Additional checks for TX_MULTISIG:
                    unsigned char m = vSolutionsRet.front()[0];
                    unsigned char n = vSolutionsRet.back()[0];
                    if (m < 1 || n < 1 || m > n || vSolutionsRet.size()-2 != n)
                        return false;
                }
                return true;
            }
            if (!script1.GetOp(pc1, opcode1, vch1))
                break;
            if (!script2.GetOp(pc2, opcode2, vch2))
                break;

            // Template matching opcodes:
            if (opcode2 == OP_PUBKEYS)
            {
                while (vch1.size() >= 33 && vch1.size() <= 65)
                {
                    vSolutionsRet.push_back(vch1);
                    if (!script1.GetOp(pc1, opcode1, vch1))
                        break;
                }
                if (!script2.GetOp(pc2, opcode2, vch2))
                    break;
                // Normal situation is to fall through
                // to other if/else statements
            }

            if (opcode2 == OP_PUBKEY)
            {
                if (vch1.size() < 33 || vch1.size() > 65)
                    break;
                vSolutionsRet.push_back(vch1);
            }
            else if (opcode2 == OP_PUBKEYHASH)
            {
                if (vch1.size() != sizeof(uint160))
                    break;
                vSolutionsRet.push_back(vch1);
            }
            else if (opcode2 == OP_SMALLINTEGER)
            {   // Single-byte small integer pushed onto vSolutions
                if (opcode1 == OP_0 ||
                    (opcode1 >= OP_1 && opcode1 <= OP_16))
                {
                    char n = (char)CScript::DecodeOP_N(opcode1);
                    vSolutionsRet.push_back(valtype(1, n));
                }
                else
                    break;
            }
            else if (opcode1 != opcode2 || vch1 != vch2)
            {
                // Others must match exactly
                break;
            }
        }
    }

    vSolutionsRet.clear();
    typeRet = TX_NONSTANDARD;
    return false;
}

int ScriptSigArgsExpected(txnouttype t, const std::vector<std::vector<unsigned char> >& vSolutions)
{
    switch (t)
    {
    case TX_NONSTANDARD:
    case TX_NULL_DATA:
    case TX_ZEROCOINMINT:
        return -1;
    case TX_PUBKEY:
        return 1;
    case TX_PUBKEYHASH:
        return 2;
    case TX_COLDSTAKE:
        return 3;
    case TX_MULTISIG:
        if (vSolutions.size() < 1 || vSolutions[0].size() < 1)
            return -1;
        return vSolutions[0][0] + 1;
    case TX_SCRIPTHASH:
        return 1; // doesn't include args needed by the script
    }
    return -1;
}

bool IsStandard(const CScript& scriptPubKey, txnouttype& whichType)
{
    std::vector<valtype> vSolutions;
    if (!Solver(scriptPubKey, whichType, vSolutions))
        return false;

    if (whichType == TX_MULTISIG)
    {
        unsigned char m = vSolutions.front()[0];
        unsigned char n = vSolutions.back()[0];
        // Support up to x-of-3 multisig txns as standard
        if (n < 1 || n > 3)
            return false;
        if (m < 1 || m > n)
            return false;
    } else if (whichType == TX_NULL_DATA &&
                (!GetBoolArg("-datacarrier", true) || scriptPubKey.size() > nMaxDatacarrierBytes))
        return false;

    return whichType != TX_NONSTANDARD;
}

bool ExtractDestination(const CScript& scriptPubKey, CTxDestination& addressRet, bool fColdStake)
{
    std::vector<valtype> vSolutions;
    txnouttype whichType;
    if (!Solver(scriptPubKey, whichType, vSolutions))
        return false;

    if (whichType == TX_PUBKEY) {
        CPubKey pubKey(vSolutions[0]);
        if (!pubKey.IsValid())
            return false;

        addressRet = pubKey.GetID();
        return true;

    } else if (whichType == TX_PUBKEYHASH) {
        addressRet = CKeyID(uint160(vSolutions[0]));
        return true;

    } else if (whichType == TX_SCRIPTHASH) {
        addressRet = CScriptID(uint160(vSolutions[0]));
        return true;
    } else if (whichType == TX_COLDSTAKE) {
        addressRet = CKeyID(uint160(vSolutions[!fColdStake]));
        return true;
    }
    // Multisig txns have more than one address...
    return false;
}


bool ExtractSenderData(const CScript& outputPubKey, CScript* senderPubKey, CScript* senderSig)
{
    // Ensure the script actually claims to have OP_SENDER first
    if (!outputPubKey.HasOpSender()) {
        return false;
    }

    CScript::const_iterator pc = outputPubKey.begin();
    CScript::const_iterator pend = outputPubKey.end();
    std::vector<valtype> stack; // Use to store PUSHED data items
    opcodetype opcode = OP_INVALIDOPCODE;
    opcodetype contractOpcode = OP_INVALIDOPCODE; // Track if we've seen OP_CALL or OP_CREATE

    // Simulate script execution to find OP_SENDER and preceding data
    while (pc < pend) {
        valtype data;
        if (!outputPubKey.GetOp(pc, opcode, data)) {
            // Script parsing failed
            LogPrint("script", "ExtractSenderData Error: Failed to parse script.\n");
            return false;
        }

        // If the opcode pushes data, add it to our simulated stack
        if (data.size() > 0) {
             stack.push_back(data);
        } else if (opcode >= OP_1 && opcode <= OP_16) {
             // Handle OP_N opcodes (push small integers)
             stack.push_back(valtype(1, CScript::DecodeOP_N(opcode)));
        } else if (opcode == OP_0) {
             stack.push_back(valtype()); // OP_0 pushes empty vector
        }
        // Ignore non-push opcodes for this purpose, except the ones we care about

        // Track if we hit the main contract opcode
        if (opcode == OP_CALL || opcode == OP_CREATE) {
             if (contractOpcode != OP_INVALIDOPCODE) {
                 LogPrint("script", "ExtractSenderData Error: Multiple OP_CALL/OP_CREATE found.\n");
                 return false; // Should only be one
             }
             contractOpcode = opcode;
        }

        // Check for OP_SENDER
        if (opcode == OP_SENDER) {
            // OP_SENDER format expects stack (bottom to top) before OP_SENDER:
            // ..., <senderSig>, <senderPubKey>, <programData>

            // 1. Check if we previously found the main contract op
            if (contractOpcode == OP_INVALIDOPCODE) {
                 LogPrint("script", "ExtractSenderData Error: OP_SENDER found before OP_CALL/OP_CREATE.\n");
                 return false; // OP_SENDER must come after the program opcode logically
            }

            // 2. Check if there are enough items on the simulated stack
            // We need at least 3 items pushed before OP_SENDER was hit: senderSig, senderPubKey, programData
            if (stack.size() < 3) {
                LogPrint("script", "ExtractSenderData Error: OP_SENDER stack too small (%d).\n", stack.size());
                return false; // Not enough items for OP_SENDER
            }

            // Extract the items assuming they are the last 3 PUSHED items
            // Note: The actual EVM execution stack might be different, but we are parsing the script *template*
            const valtype& programDataStack = stack[stack.size() - 1]; // Last pushed item should be program
            const valtype& pubkeyDataStack = stack[stack.size() - 2]; // Item before program should be senderPubKey script data
            const valtype& sigDataStack = stack[stack.size() - 3];    // Item before that should be senderSig script data

            // Basic validation (e.g., non-empty) - add more checks as needed
            if (pubkeyDataStack.empty() || sigDataStack.empty()) {
                 LogPrint("script", "ExtractSenderData Error: OP_SENDER pubkey or sig components are empty on stack.\n");
                 return false;
            }


            // Assign to output parameters if they are not null
            // We assume the data pushed was the raw CScript bytes
            if (senderPubKey) {
                try {
                    // Deserialize the pubkey script part from the stack data
                    *senderPubKey = CScript(pubkeyDataStack.begin(), pubkeyDataStack.end());
                } catch (const std::exception& e) {
                     LogPrint("script", "ExtractSenderData Error: Failed to deserialize senderPubKey: %s\n", e.what());
                     return false;
                }
            }
            if (senderSig) {
                 try {
                    // Deserialize the signature script part from the stack data
                    *senderSig = CScript(sigDataStack.begin(), sigDataStack.end());
                 } catch (const std::exception& e) {
                     LogPrint("script", "ExtractSenderData Error: Failed to deserialize senderSig: %s\n", e.what());
                     return false;
                 }
            }

            return true; // Found OP_SENDER and extracted data
        }

        // If it's not OP_SENDER, continue parsing
    }

    // If we reach the end without finding OP_SENDER
    // LogPrint("script", "ExtractSenderData: Reached end of script without finding OP_SENDER.\n");
    return false;
}


bool ExtractDestinations(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<CTxDestination>& addressRet, int& nRequiredRet)
{
    addressRet.clear();
    typeRet = TX_NONSTANDARD;
    std::vector<valtype> vSolutions;
    if (!Solver(scriptPubKey, typeRet, vSolutions))
        return false;
    if (typeRet == TX_NULL_DATA){
        // This is data, not addresses
        return false;
    }

    if (typeRet == TX_MULTISIG)
    {
        nRequiredRet = vSolutions.front()[0];
        for (unsigned int i = 1; i < vSolutions.size()-1; i++)
        {
            CPubKey pubKey(vSolutions[i]);
            if (!pubKey.IsValid())
                continue;

            CTxDestination address = pubKey.GetID();
            addressRet.push_back(address);
        }

        if (addressRet.empty())
            return false;

    } else if (typeRet == TX_COLDSTAKE)
    {
        if (vSolutions.size() < 2)
            return false;
        nRequiredRet = 2;
        addressRet.push_back(CKeyID(uint160(vSolutions[0])));
        addressRet.push_back(CKeyID(uint160(vSolutions[1])));
        return true;

    } else
    {
        nRequiredRet = 1;
        CTxDestination address;
        if (!ExtractDestination(scriptPubKey, address))
           return false;
        addressRet.push_back(address);
    }

    return true;
}

namespace
{
class CScriptVisitor : public boost::static_visitor<bool>
{
private:
    CScript *script;
public:
    CScriptVisitor(CScript *scriptin) { script = scriptin; }

    bool operator()(const CNoDestination &dest) const {
        script->clear();
        return false;
    }

    bool operator()(const CKeyID &keyID) const {
        script->clear();
        *script << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG;
        return true;
    }

    bool operator()(const CScriptID &scriptID) const {
        script->clear();
        *script << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
        return true;
    }
};
}

namespace
{
class CLockedScriptVisitor : public boost::static_visitor<bool>
{
private:
    CScript *script;
    int nLockTime;
public:
    CLockedScriptVisitor(CScript *scriptin, int nLockTime) : script(scriptin), nLockTime(nLockTime) { }

    bool operator()(const CNoDestination &dest) const {
        script->clear();
        return false;
    }

    bool operator()(const CKeyID &keyID) const {
        script->clear();
        *script << nLockTime << OP_CHECKLOCKTIMEVERIFY << OP_DROP << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG;
        return true;
    }

    bool operator()(const CScriptID &scriptID) const {
        script->clear();
        *script << nLockTime << OP_CHECKLOCKTIMEVERIFY << OP_DROP << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
        return true;
    }
};
}

CScript GetScriptForDestination(const CTxDestination& dest)
{
    CScript script;

    boost::apply_visitor(CScriptVisitor(&script), dest);
    return script;
}



CScript GetScriptForStakeDelegation(const CKeyID& stakingKey, const CKeyID& spendingKey)
{
    CScript script;
    script << OP_DUP << OP_HASH160 << OP_ROT <<
            OP_IF << OP_CHECKCOLDSTAKEVERIFY << ToByteVector(stakingKey) <<
            OP_ELSE << ToByteVector(spendingKey) << OP_ENDIF <<
            OP_EQUALVERIFY << OP_CHECKSIG;
    return script;
}

CScript GetLockedScriptForDestination(const CTxDestination& dest, int nLockTime)
{
    CScript script;

    boost::apply_visitor(CLockedScriptVisitor(&script, nLockTime), dest);
    return script;
}

CScript GetScriptForRawPubKey(const CPubKey& pubKey)
{
    return CScript() << std::vector<unsigned char>(pubKey.begin(), pubKey.end()) << OP_CHECKSIG;
}


CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey>& keys)
{
    CScript script;

    script << CScript::EncodeOP_N(nRequired);
    for (const CPubKey& key : keys)
        script << ToByteVector(key);
    script << CScript::EncodeOP_N(keys.size()) << OP_CHECKMULTISIG;
    return script;
}
