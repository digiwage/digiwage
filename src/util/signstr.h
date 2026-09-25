/**
 * Sign verify message utilities.
 */
#ifndef UTIL_SIGNSTR_H
#define UTIL_SIGNSTR_H

#include <string>
#include <key.h>
#include <hash.h>


namespace SignStr
{
const std::string strMessageMagic = "DigiWage Signed Message:\n";
// Proofs of delegation (PoD) keep Qtum's prefix: the delegations contract
// deployed at nOfflineStakeHeight verifies them with "\x15Qtum Signed Message:\n".
const std::string strPoDMessageMagic = "Qtum Signed Message:\n";

inline bool SignMessage(const CKey& key, const std::string& strMessage, std::vector<unsigned char>& vchSig)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << strPoDMessageMagic;
    ss << strMessage;

    return key.SignCompact(ss.GetHash(), vchSig);
}

inline bool VerifyMessage(const CKeyID& keyID, const std::string& strMessage, const std::vector<unsigned char>& vchSig)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << strPoDMessageMagic;
    ss << strMessage;

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig))
        return false;

    return (pubkey.GetID() == keyID);
}

inline bool GetKeyIdMessage(const std::string& strMessage, const std::vector<unsigned char>& vchSig, CKeyID& keyID)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << strPoDMessageMagic;
    ss << strMessage;

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig))
        return false;

    keyID = pubkey.GetID();
    return true;
}
}

#endif // UTIL_SIGNSTR_H
