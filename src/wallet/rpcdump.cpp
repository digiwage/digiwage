// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bip38.h"
#include "init.h"
#include "main.h"
#include "rpc/server.h"
#include "script/script.h"
#include "script/standard.h"
#include "sync.h"
#include "util.h"
#include "utilstrencodings.h"
#include "utiltime.h"
#include "wallet/wallet.h"

#include <fstream>
#include <secp256k1.h>
#include <stdint.h>

#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <univalue.h>


std::string static EncodeDumpTime(int64_t nTime)
{
    return DateTimeStrFormat("%Y-%m-%dT%H:%M:%SZ", nTime);
}

int64_t static DecodeDumpTime(const std::string& str)
{
    static const boost::posix_time::ptime epoch = boost::posix_time::from_time_t(0);
    static const std::locale loc(std::locale::classic(),
        new boost::posix_time::time_input_facet("%Y-%m-%dT%H:%M:%SZ"));
    std::istringstream iss(str);
    iss.imbue(loc);
    boost::posix_time::ptime ptime(boost::date_time::not_a_date_time);
    iss >> ptime;
    if (ptime.is_not_a_date_time())
        return 0;
    return (ptime - epoch).total_seconds();
}

std::string static EncodeDumpString(const std::string& str)
{
    std::stringstream ret;
    for (unsigned char c : str) {
        if (c <= 32 || c >= 128 || c == '%') {
            ret << '%' << HexStr(&c, &c + 1);
        } else {
            ret << c;
        }
    }
    return ret.str();
}

std::string DecodeDumpString(const std::string& str)
{
    std::stringstream ret;
    for (unsigned int pos = 0; pos < str.length(); pos++) {
        unsigned char c = str[pos];
        if (c == '%' && pos + 2 < str.length()) {
            c = (((str[pos + 1] >> 6) * 9 + ((str[pos + 1] - '0') & 15)) << 4) |
                ((str[pos + 2] >> 6) * 9 + ((str[pos + 2] - '0') & 15));
            pos += 2;
        }
        ret << c;
    }
    return ret.str();
}

UniValue importprivkey(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 4)
        throw std::runtime_error(
            "importprivkey \"digiwageprivkey\" ( \"label\" rescan fStakingAddress )\n"
            "\nAdds a private key (as returned by dumpprivkey) to your wallet.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. \"digiwageprivkey\"      (string, required) The private key (see dumpprivkey)\n"
            "2. \"label\"            (string, optional, default=\"\") An optional label\n"
            "3. rescan               (boolean, optional, default=true) Rescan the wallet for transactions\n"
            "4. fStakingAddress      (boolean, optional, default=false) Whether this key refers to a (cold) staking address\n"

            "\nNote: This call can take minutes to complete if rescan is true.\n"

            "\nExamples:\n"
            "\nDump a private key\n" +
            HelpExampleCli("dumpprivkey", "\"myaddress\"") +
            "\nImport the private key with rescan\n" +
            HelpExampleCli("importprivkey", "\"mykey\"") +
            "\nImport using a label and without rescan\n" +
            HelpExampleCli("importprivkey", "\"mykey\" \"testing\" false") +
            "\nAs a JSON-RPC call\n" +
            HelpExampleRpc("importprivkey", "\"mykey\", \"testing\", false"));

    const std::string strSecret = params[0].get_str();
    const std::string strLabel = (params.size() > 1 ? params[1].get_str() : "");
    const bool fRescan = (params.size() > 2 ? params[2].get_bool() : true);
    const bool fStakingAddress = (params.size() > 3 ? params[3].get_bool() : false);

    CBitcoinSecret vchSecret;
    if (!vchSecret.SetString(strSecret))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key encoding");

    CKey key = vchSecret.GetKey();
    if (!key.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Private key outside allowed range");

    CPubKey pubkey = key.GetPubKey();
    assert(key.VerifyPubKey(pubkey));
    CKeyID vchAddress = pubkey.GetID();
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        EnsureWalletIsUnlocked();

        pwalletMain->MarkDirty();
        pwalletMain->SetAddressBook(vchAddress, strLabel, (
                fStakingAddress ?
                        AddressBook::AddressBookPurpose::COLD_STAKING :
                        AddressBook::AddressBookPurpose::RECEIVE));

        // Don't throw error in case a key is already there
        if (pwalletMain->HaveKey(vchAddress))
            return NullUniValue;

        pwalletMain->mapKeyMetadata[vchAddress].nCreateTime = 1;

        if (!pwalletMain->AddKeyPubKey(key, pubkey))
            throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");

        // whenever a key is imported, we need to scan the whole chain
        pwalletMain->nTimeFirstKey = 1; // 0 would be considered 'no value'

        if (fRescan) {
            CBlockIndex *pindex = chainActive.Genesis();
            if (fStakingAddress && !Params().IsRegTestNet()) {
                // cold staking was activated after nBlockTimeProtocolV2. No need to scan the whole chain
                pindex = chainActive[Params().GetConsensus().height_start_TimeProtoV2];
            }
            pwalletMain->ScanForWalletTransactions(pindex, true);
        }
    }

    return NullUniValue;
}

UniValue dumphdinfo(const UniValue& params, bool fHelp)
{
    if (pwalletMain == NULL)
        return false;

    if (fHelp || params.size() != 0)
        throw std::runtime_error(
                "dumphdinfo\n"
                "Returns an object containing sensitive private info about this HD wallet.\n"
                "\nResult:\n"
                "{\n"
                "  \"hdseed\": \"seed\",                    (string) The HD seed (bip32, in hex)\n"
                "  \"mnemonic\": \"words\",                 (string) The mnemonic for this HD wallet (bip39, english words) \n"
                "  \"mnemonicpassphrase\": \"passphrase\",  (string) The mnemonic passphrase for this HD wallet (bip39)\n"
                "}\n"
                "\nExamples:\n"
                + HelpExampleCli("dumphdinfo", "")
                + HelpExampleRpc("dumphdinfo", "")
        );

    LOCK(pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    CHDChain hdChainCurrent;
    if (!pwalletMain->GetHDChain(hdChainCurrent))
        throw JSONRPCError(RPC_WALLET_ERROR, "This wallet is not a HD wallet.");

    if (!pwalletMain->GetDecryptedHDChain(hdChainCurrent))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Cannot decrypt HD seed");

    SecureString ssMnemonic;
    SecureString ssMnemonicPassphrase;
    hdChainCurrent.GetMnemonic(ssMnemonic, ssMnemonicPassphrase);

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("warning", _("This seed contains all of your private keys. DO NOT send this file to anyone!")));
    obj.push_back(Pair("hdseed", HexStr(hdChainCurrent.GetSeed())));
    obj.push_back(Pair("mnemonic", ssMnemonic.c_str()));
    obj.push_back(Pair("mnemonicpassphrase", ssMnemonicPassphrase.c_str()));

    return obj;
}


UniValue importaddress(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
        throw std::runtime_error(
            "importaddress \"address\" ( \"label\" rescan )\n"
            "\nAdds an address or script (in hex) that can be watched as if it were in your wallet but cannot be used to spend.\n"

            "\nArguments:\n"
            "1. \"address\"          (string, required) The address\n"
            "2. \"label\"            (string, optional, default=\"\") An optional label\n"
            "3. rescan               (boolean, optional, default=true) Rescan the wallet for transactions\n"

            "\nNote: This call can take minutes to complete if rescan is true.\n"

            "\nExamples:\n"
            "\nImport an address with rescan\n" +
            HelpExampleCli("importaddress", "\"myaddress\"") +
            "\nImport using a label without rescan\n" +
            HelpExampleCli("importaddress", "\"myaddress\" \"testing\" false") +
            "\nAs a JSON-RPC call\n" +
            HelpExampleRpc("importaddress", "\"myaddress\", \"testing\", false"));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CScript script;

    CBitcoinAddress address(params[0].get_str());
    if (address.IsValid()) {
        script = GetScriptForDestination(address.Get());
    } else if (IsHex(params[0].get_str())) {
        std::vector<unsigned char> data(ParseHex(params[0].get_str()));
        script = CScript(data.begin(), data.end());
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DIGIWAGE address or script");
    }

    std::string strLabel = "";
    if (params.size() > 1)
        strLabel = params[1].get_str();

    // Whether to perform rescan after import
    bool fRescan = true;
    if (params.size() > 2)
        fRescan = params[2].get_bool();

    {
        if (::IsMine(*pwalletMain, script) & ISMINE_SPENDABLE_ALL)
            throw JSONRPCError(RPC_WALLET_ERROR, "The wallet already contains the private key for this address or script");

        // add to address book or update label
        if (address.IsValid()) {
            pwalletMain->SetAddressBook(address.Get(), strLabel,
                    (address.IsStakingAddress() ?
                            AddressBook::AddressBookPurpose::COLD_STAKING :
                            AddressBook::AddressBookPurpose::RECEIVE));
        }

        // Don't throw error in case an address is already there
        if (pwalletMain->HaveWatchOnly(script))
            return NullUniValue;

        pwalletMain->MarkDirty();

        if (!pwalletMain->AddWatchOnly(script))
            throw JSONRPCError(RPC_WALLET_ERROR, "Error adding address to wallet");

        if (fRescan) {
            pwalletMain->ScanForWalletTransactions(chainActive.Genesis(), true);
            pwalletMain->ReacceptWalletTransactions();
        }
    }

    return NullUniValue;
}

// TODO: Needs further review over the HD flow, staking addresses and multisig import.
UniValue importwallet(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "importwallet \"filename\"\n"
            "\nImports keys from a wallet dump file (see dumpwallet).\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. \"filename\"    (string, required) The wallet file\n"

            "\nExamples:\n"
            "\nDump the wallet\n" +
            HelpExampleCli("dumpwallet", "\"test\"") +
            "\nImport the wallet\n" +
            HelpExampleCli("importwallet", "\"test\"") +
            "\nImport using the json rpc call\n" +
            HelpExampleRpc("importwallet", "\"test\""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    std::ifstream file;
    file.open(params[0].get_str().c_str(), std::ios::in | std::ios::ate);
    if (!file.is_open())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");

    int64_t nTimeBegin = chainActive.Tip()->GetBlockTime();

    bool fGood = true;

    int64_t nFilesize = std::max((int64_t)1, (int64_t)file.tellg());
    file.seekg(0, file.beg);

    pwalletMain->ShowProgress(_("Importing..."), 0); // show progress dialog in GUI
    while (file.good()) {
        pwalletMain->ShowProgress("", std::max(1, std::min(99, (int)(((double)file.tellg() / (double)nFilesize) * 100))));
        std::string line;
        std::getline(file, line);
        if (line.empty() || line[0] == '#')
            continue;

        std::vector<std::string> vstr;
        boost::split(vstr, line, boost::is_any_of(" "));
        if (vstr.size() < 2)
            continue;
        CBitcoinSecret vchSecret;
        if (!vchSecret.SetString(vstr[0]))
            continue;
        CKey key = vchSecret.GetKey();
        CPubKey pubkey = key.GetPubKey();
        assert(key.VerifyPubKey(pubkey));
        CKeyID keyid = pubkey.GetID();
        if (pwalletMain->HaveKey(keyid)) {
            LogPrintf("Skipping import of %s (key already present)\n", CBitcoinAddress(keyid).ToString());
            continue;
        }
        int64_t nTime = DecodeDumpTime(vstr[1]);
        std::string strLabel;
        bool fLabel = true;
        for (unsigned int nStr = 2; nStr < vstr.size(); nStr++) {
            const std::string& type = vstr[nStr];
            if (boost::algorithm::starts_with(type, "#"))
                break;
            if (type == "change=1")
                fLabel = false;
            else if (type == "reserve=1")
                fLabel = false;
            else if (type == "hdseed")
                fLabel = false;
            if (boost::algorithm::starts_with(type, "label=")) {
                strLabel = DecodeDumpString(vstr[nStr].substr(6));
                fLabel = true;
            }
        }
        LogPrintf("Importing %s...\n", CBitcoinAddress(keyid).ToString());
        if (!pwalletMain->AddKeyPubKey(key, pubkey)) {
            fGood = false;
            continue;
        }
        pwalletMain->mapKeyMetadata[keyid].nCreateTime = nTime;
        if (fLabel) // TODO: This is not entirely true.. needs to be reviewed properly.
            pwalletMain->SetAddressBook(keyid, strLabel, AddressBook::AddressBookPurpose::RECEIVE);
        nTimeBegin = std::min(nTimeBegin, nTime);
    }
    file.close();
    pwalletMain->ShowProgress("", 100); // hide progress dialog in GUI

    CBlockIndex* pindex = chainActive.Tip();
    while (pindex && pindex->pprev && pindex->GetBlockTime() > nTimeBegin - 7200)
        pindex = pindex->pprev;

    if (!pwalletMain->nTimeFirstKey || nTimeBegin < pwalletMain->nTimeFirstKey)
        pwalletMain->nTimeFirstKey = nTimeBegin;

    LogPrintf("Rescanning last %i blocks\n", chainActive.Height() - pindex->nHeight + 1);
    pwalletMain->ScanForWalletTransactions(pindex);
    pwalletMain->MarkDirty();

    if (!fGood)
        throw JSONRPCError(RPC_WALLET_ERROR, "Error adding some keys to wallet");

    return NullUniValue;
}

UniValue dumpprivkey(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "dumpprivkey \"digiwageaddress\"\n"
            "\nReveals the private key corresponding to 'digiwageaddress'.\n"
            "Then the importprivkey can be used with this output\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. \"digiwageaddress\"   (string, required) The DIGIWAGE address for the private key\n"

            "\nResult:\n"
            "\"key\"                (string) The private key\n"

            "\nExamples:\n" +
            HelpExampleCli("dumpprivkey", "\"myaddress\"") + HelpExampleCli("importprivkey", "\"mykey\"") + HelpExampleRpc("dumpprivkey", "\"myaddress\""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    std::string strAddress = params[0].get_str();
    CBitcoinAddress address;
    if (!address.SetString(strAddress))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DIGIWAGE address");
    CKeyID keyID;
    if (!address.GetKeyID(keyID))
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
    CKey vchSecret;
    if (!pwalletMain->GetKey(keyID, vchSecret))
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + strAddress + " is not known");
    return CBitcoinSecret(vchSecret).ToString();
}

UniValue dumpwallet(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "dumpwallet \"filename\"\n"
            "\nDumps all wallet keys in a human-readable format to a server-side file. This does not allow overwriting existing files.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. \"filename\"    (string, required) The filename\n"

            "\nExamples:\n" +
            HelpExampleCli("dumpwallet", "\"test\"") + HelpExampleRpc("dumpwallet", "\"test\""));

    if (params[0].get_str().find("bug") != std::string::npos ||
        params[0].get_str().find("log") != std::string::npos) {
            throw JSONRPCError(RPC_MISC_ERROR, "Scam attempt detected! (DO NOT give dumpwallet or dumpprivkey information to ANYONE!)");
    }

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    boost::filesystem::path filepath = params[0].get_str().c_str();
    filepath = boost::filesystem::absolute(filepath);

    std::ofstream file;
    file.open(params[0].get_str().c_str());
    if (!file.is_open())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");

    std::map<CKeyID, int64_t> mapKeyBirth;
    std::set<CKeyID> setKeyPool;
    pwalletMain->GetKeyBirthTimes(mapKeyBirth);
    pwalletMain->GetAllReserveKeys(setKeyPool);

    // sort time/key pairs
    std::vector<std::pair<int64_t, CKeyID> > vKeyBirth;
    for (std::map<CKeyID, int64_t>::const_iterator it = mapKeyBirth.begin(); it != mapKeyBirth.end(); it++) {
        vKeyBirth.push_back(std::make_pair(it->second, it->first));
    }
    mapKeyBirth.clear();
    std::sort(vKeyBirth.begin(), vKeyBirth.end());

    CBlockIndex* tip = chainActive.Tip();
    // produce output
    file << strprintf("# Wallet dump created by DIGIWAGE %s (%s)\n", CLIENT_BUILD, CLIENT_DATE);
    file << strprintf("# * Created on %s\n", EncodeDumpTime(GetTime()));
    if (tip) {
        file << strprintf("# * Best block at time of backup was %i (%s),\n", tip->nHeight,
                          tip->GetBlockHash().ToString());
        file << strprintf("#   mined on %s\n", EncodeDumpTime(tip->GetBlockTime()));
    } else {
        file << "# Missing tip information\n";
    }
    file << "\n";
    // add the base58check encoded extended master if the wallet uses HD
    CHDChain hdChainCurrent;
    if (pwalletMain->GetHDChain(hdChainCurrent))
    {
        if (!pwalletMain->GetDecryptedHDChain(hdChainCurrent))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Cannot decrypt HD chain");

        SecureString ssMnemonic;
        SecureString ssMnemonicPassphrase;
        hdChainCurrent.GetMnemonic(ssMnemonic, ssMnemonicPassphrase);
        file << "# mnemonic: " << ssMnemonic << "\n";
        file << "# mnemonic passphrase: " << ssMnemonicPassphrase << "\n\n";
        SecureVector vchSeed = hdChainCurrent.GetSeed();
        file << "# HD seed: " << HexStr(vchSeed) << "\n\n";
        CExtKey masterKey;
        masterKey.SetMaster(&vchSeed[0], vchSeed.size());
        CBitcoinExtKey b58extkey;
        b58extkey.SetKey(masterKey);
        file << "# extended private masterkey: " << b58extkey.ToString() << "\n";
        CExtPubKey masterPubkey;
        masterPubkey = masterKey.Neuter();
        CBitcoinExtPubKey b58extpubkey;
        b58extpubkey.SetKey(masterPubkey);
        file << "# extended public masterkey: " << b58extpubkey.ToString() << "\n\n";
        for (size_t i = 0; i < hdChainCurrent.CountAccounts(); ++i)
        {
            CHDAccount acc;
            if(hdChainCurrent.GetAccount(i, acc)) {
                file << "# external chain counter: " << acc.nExternalChainCounter << "\n";
                file << "# internal chain counter: " << acc.nInternalChainCounter << "\n\n";
            } else {
                file << "# WARNING: ACCOUNT " << i << " IS MISSING!" << "\n\n";
            }
        }
    }
    for (std::vector<std::pair<int64_t, CKeyID> >::const_iterator it = vKeyBirth.begin(); it != vKeyBirth.end(); it++) {
        const CKeyID& keyid = it->second;
        std::string strTime = EncodeDumpTime(it->first);
        std::string strAddr = CBitcoinAddress(keyid).ToString();
        CKey key;
        if (pwalletMain->GetKey(keyid, key)) {
            file << strprintf("%s %s ", CBitcoinSecret(key).ToString(), strTime);
            if (pwalletMain->mapAddressBook.count(keyid)) {
                file << strprintf("label=%s", EncodeDumpString(pwalletMain->mapAddressBook[keyid].name));
            } else if (setKeyPool.count(keyid)) {
                file << "reserve=1";
            } else {
                file << "change=1";
            }
            file << strprintf(" # addr=%s%s\n", strAddr, (pwalletMain->mapHdPubKeys.count(keyid) ? " hdkeypath="+pwalletMain->mapHdPubKeys[keyid].GetKeyPath() : ""));
        }
    }
    file << "\n";
    file << "# End of dump\n";
    file.close();

    UniValue reply(UniValue::VOBJ);
    reply.push_back(Pair("warning", _("This file contains all of your private keys in plain text. DO NOT send this file to anyone!")));
    reply.push_back(Pair("filename", filepath.string()));

    return reply;
}

UniValue bip38encrypt(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw std::runtime_error(
            "bip38encrypt \"digiwageaddress\" \"passphrase\"\n"
            "\nEncrypts a private key corresponding to 'digiwageaddress'.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. \"digiwageaddress\"   (string, required) The DIGIWAGE address for the private key (you must hold the key already)\n"
            "2. \"passphrase\"   (string, required) The passphrase you want the private key to be encrypted with - Valid special chars: !#$%&'()*+,-./:;<=>?`{|}~ \n"

            "\nResult:\n"
            "\"key\"                (string) The encrypted private key\n"

            "\nExamples:\n" +
            HelpExampleCli("bip38encrypt", "\"DMJRSsuU9zfyrvxVaAEFQqK4MxZg6vgeS6\" \"mypasphrase\"") +
            HelpExampleRpc("bip38encrypt", "\"DMJRSsuU9zfyrvxVaAEFQqK4MxZg6vgeS6\" \"mypasphrase\""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    std::string strAddress = params[0].get_str();
    std::string strPassphrase = params[1].get_str();

    CBitcoinAddress address;
    if (!address.SetString(strAddress))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DIGIWAGE address");
    CKeyID keyID;
    if (!address.GetKeyID(keyID))
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
    CKey vchSecret;
    if (!pwalletMain->GetKey(keyID, vchSecret))
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + strAddress + " is not known");

    uint256 privKey = vchSecret.GetPrivKey_256();
    std::string encryptedOut = BIP38_Encrypt(strAddress, strPassphrase, privKey, vchSecret.IsCompressed());

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("Addess", strAddress));
    result.push_back(Pair("Encrypted Key", encryptedOut));

    return result;
}

UniValue bip38decrypt(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw std::runtime_error(
            "bip38decrypt \"digiwageaddress\" \"passphrase\"\n"
            "\nDecrypts and then imports password protected private key.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. \"encryptedkey\"   (string, required) The encrypted private key\n"
            "2. \"passphrase\"   (string, required) The passphrase you want the private key to be encrypted with\n"

            "\nResult:\n"
            "\"key\"                (string) The decrypted private key\n"

            "\nExamples:\n" +
            HelpExampleCli("bip38decrypt", "\"encryptedkey\" \"mypassphrase\"") +
            HelpExampleRpc("bip38decrypt", "\"encryptedkey\" \"mypassphrase\""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    /** Collect private key and passphrase **/
    std::string strKey = params[0].get_str();
    std::string strPassphrase = params[1].get_str();

    uint256 privKey;
    bool fCompressed;
    if (!BIP38_Decrypt(strPassphrase, strKey, privKey, fCompressed))
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed To Decrypt");

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("privatekey", HexStr(privKey)));

    CKey key;
    key.Set(privKey.begin(), privKey.end(), fCompressed);

    if (!key.IsValid())
        throw JSONRPCError(RPC_WALLET_ERROR, "Private Key Not Valid");

    CPubKey pubkey = key.GetPubKey();
    pubkey.IsCompressed();
    assert(key.VerifyPubKey(pubkey));
    result.push_back(Pair("Address", CBitcoinAddress(pubkey.GetID()).ToString()));
    CKeyID vchAddress = pubkey.GetID();
    {
        pwalletMain->MarkDirty();
        pwalletMain->SetAddressBook(vchAddress, "", AddressBook::AddressBookPurpose::RECEIVE);

        // Don't throw error in case a key is already there
        if (pwalletMain->HaveKey(vchAddress))
            throw JSONRPCError(RPC_WALLET_ERROR, "Key already held by wallet");

        pwalletMain->mapKeyMetadata[vchAddress].nCreateTime = 1;

        if (!pwalletMain->AddKeyPubKey(key, pubkey))
            throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");

        // whenever a key is imported, we need to scan the whole chain
        pwalletMain->nTimeFirstKey = 1; // 0 would be considered 'no value'
        pwalletMain->ScanForWalletTransactions(chainActive.Genesis(), true);
    }

    return result;
}
