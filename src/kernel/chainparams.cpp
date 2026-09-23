// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kernel/chainparams.h>

#include <chainparamsseeds.h>
#include <consensus/amount.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <consensus/consensus.h>
#include <hash.h>
#include <chainparamsbase.h>
#include <logging.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <uint256.h>
#include <util/strencodings.h>
#include <util/convert.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

///////////////////////////////////////////// // digiwage
#include <libdevcore/SHA3.h>
#include <libdevcore/RLP.h>
#include "arith_uint256.h"
/////////////////////////////////////////////

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward, bool digiwage)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = digiwage
        ? CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp))
        : CScript() << 00 << 488804799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    genesis.hashStateRoot = uint256(h256Touint(dev::h256("e965ffd002cd6ad0e2dc402b8044de833e06b23127ea8c3d80aec91410771495"))); // digiwage
    genesis.hashUTXORoot = uint256(h256Touint(dev::sha3(dev::rlp("")))); // digiwage
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward, bool digiwage = false)
{
    const char* pszTimestamp = digiwage ? "Blockchain can change humanity for good" : "Sep 02, 2017 Bitcoin breaks $5,000 in latest price frenzy";
    const CScript genesisOutputScript = CScript() << ParseHex(digiwage
        ? "04682170b57e85aeae3ee34f858112040a933f6c48402620be4db4796e26f7d11d481f6ad9f05c471f8414c7ad7e1a90562906cff8b8c8b159666fbc4ff5af6904"
        : "040d61d8653448c98731ee5fffd303c15e71ec2057b77f11ab3601979728cdaff2d68afbba14e4fa0bc44f2072b0b23ef63717f8cdfbe58dcd33f32b6afe98741a") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward, digiwage);
}

/**
 * Main network on which people trade goods and services.
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        consensus.digiwage_history = true;
        consensus.digiwage_legacy_chain = true;
        consensus.digiwage_stake_modifier_v2_height = 1551945;
        consensus.digiwage_zerocoin_height = 1551955;
        consensus.digiwage_rhf_height = 1552000;
        // Scheduled native-EVM hard fork. Version-6 blocks commit the EVM
        // state/UTXO roots and the stake prevout in their header.
        consensus.digiwage_contract_height = 4000000;
        consensus.digiwage_stake_min_depth = 600;
        consensus.digiwage_pos_limit_v2 = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        strNetworkID = CBaseChainParams::MAIN;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 985500; // digiwage halving every 4 years
        // Digiwage does not inherit DigiWage's historical script exception.
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256S("0x000009f854e700ab62642c7d3e94be65a1d8c112384f5edfb4b2b3fa3fecaef6");
        consensus.BIP65Height = 1552000;
        consensus.BIP66Height = std::numeric_limits<int>::max();
        consensus.CSVHeight = std::numeric_limits<int>::max();
        consensus.SegwitHeight = std::numeric_limits<int>::max();
        consensus.MinBIP9WarningHeight = std::numeric_limits<int>::max();
        consensus.QIP5Height = consensus.digiwage_contract_height;
        consensus.QIP6Height = consensus.digiwage_contract_height;
        consensus.QIP7Height = consensus.digiwage_contract_height;
        consensus.QIP9Height = std::numeric_limits<int>::max();
        consensus.nOfflineStakeHeight = std::numeric_limits<int>::max();
        consensus.nReduceBlocktimeHeight = std::numeric_limits<int>::max();
        consensus.nMuirGlacierHeight = consensus.digiwage_contract_height;
        consensus.nLondonHeight = consensus.digiwage_contract_height;
        consensus.nShanghaiHeight = consensus.digiwage_contract_height;
        consensus.powLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.posLimit = uint256S("000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.QIP9PosLimit = uint256S("0000000000001fffffffffffffffffffffffffffffffffffffffffffffffffff"); // The new POS-limit activated after QIP9
        consensus.RBTPosLimit = uint256S("0000000000003fffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 40 * 60;
        consensus.nPowTargetTimespanV2 = 4000;
        consensus.nRBTPowTargetTimespan = 1000;
        consensus.nPowTargetSpacing = 60;
        consensus.nRBTPowTargetSpacing = 32;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.fPoSNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1815; // 90% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Deployment of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        // Min block number for activation, the number must be divisible by 2016
        // Replace 0xffffc0 with the activation block number
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 2080512;

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xc1;
        pchMessageStart[1] = 0xf4;
        pchMessageStart[2] = 0xa7;
        pchMessageStart[3] = 0xd6;
        nDefaultPort = 46003;
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 21;
        m_assumed_chain_state_size = 1;

        genesis = CreateGenesisBlock(1522130562, 3706113, 0x1e0ffff0, 1, 120 * COIN, true);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x000009f854e700ab62642c7d3e94be65a1d8c112384f5edfb4b2b3fa3fecaef6"));
        assert(genesis.hashMerkleRoot == uint256S("0xdda70dbacbeeb39750532e69dad0a0025c16e9bcc7ca412cf12a988d0020309d"));

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as an addrfetch if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        // Public Digiwage peers verified against protocol 80810.
        vSeeds.emplace_back("185.197.194.5");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,30);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,90);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,89);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x02, 0x2D, 0x25, 0x33};
        base58Prefixes[EXT_SECRET_KEY] = {0x02, 0x21, 0x31, 0x2B};

        bech32_hrp = "dw";

        vFixedSeeds.clear();

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        m_is_test_chain = false;
        m_is_mockable_chain = false;
        fHasHardwareWalletSupport = true;

        checkpointData = {
            {
                {0, uint256S("0x000009f854e700ab62642c7d3e94be65a1d8c112384f5edfb4b2b3fa3fecaef6")},
                {101, uint256S("0x000009f56b8dcc1904ef14a92fb1c7c66d89f2f300d27856e15ed4b78d9fef24")},
                {235000, uint256S("0x25961563bc9afae2b7ed3bc8dc61da40185bbe86096d687f746c632a885a1e01")},
                {435000, uint256S("0x6133435a7be2ca1c81172b19febd7ccd7825a3c6f5fdf41cdb673d77a54437c3")},
                {1474750, uint256S("0x2ef62243cc7ed03a111142ef0bb88cb34a92e9c4a42eec872def42461ad3be49")},
                {1722471, uint256S("0xffc8f35b3e7580c9ccd618c3028304751dcad0acb967d7e170a175b1f4415e51")},
                {1998808, uint256S("0x5f7cca9c8a121558d002c0a0e70a624ab8f1065e03f66e161d924c412ef17d73")},
                {2011680, uint256S("0x90f704de7f2cbe843d28644790612c5d6623966d14fd53069026a7faa7533442")},
            }
        };

        m_assumeutxo_data = MapAssumeutxo{
         // TODO to be specified in a future patch.
        };

        chainTxData = ChainTxData{
            // Data as of block 3dc42fcf2e731093ee9b3cbaa2df07d8b8638cdea77758bb28b1130f504a7f43 (height 3142000)
            .nTime    = 1693268288, // * UNIX timestamp of last known number of transactions
            .nTxCount = 10429839, // * total number of transactions between genesis and that timestamp
            .dTxRate  = 0.07664262206369668, // * estimated number of transactions per second after that timestamp
        };

        consensus.nBlocktimeDownscaleFactor = 4;
        consensus.nCoinbaseMaturity = 100;
        consensus.nRBTCoinbaseMaturity = consensus.nBlocktimeDownscaleFactor*500;
        consensus.nSubsidyHalvingIntervalV2 = consensus.nBlocktimeDownscaleFactor*985500; // digiwage halving every 4 years (nSubsidyHalvingInterval * nBlocktimeDownscaleFactor)

        consensus.nLastPOWBlock = 1000;
        consensus.nLastBigReward = 5000;
        consensus.nMPoSRewardRecipients = 10;
        consensus.nFirstMPoSBlock = consensus.nLastPOWBlock + 
                                    consensus.nMPoSRewardRecipients + 
                                    consensus.nCoinbaseMaturity;
        consensus.nLastMPoSBlock = 679999;


        consensus.nFixUTXOCacheHFHeight = 100000;
        consensus.nEnableHeaderSignatureHeight = std::numeric_limits<int>::max();
        consensus.nCheckpointSpan = consensus.nCoinbaseMaturity;
        consensus.nRBTCheckpointSpan = consensus.nRBTCoinbaseMaturity;
        consensus.delegationsAddress = uint160(ParseHex("0000000000000000000000000000000000000086")); // Delegations contract for offline staking
        consensus.nStakeTimestampMask = 15;
        consensus.nRBTStakeTimestampMask = 3;
    }
};

/**
 * Legacy DigiWage testnet: follows the chain produced by the legacy core
 * (/root/digiwage, "-testnet") so the v3 handoff can be rehearsed against
 * running legacy nodes. It shares the mainnet genesis block. Every value
 * below mirrors the legacy CTestNetParams.
 */
class CLegacyTestParams : public CMainParams {
public:
    CLegacyTestParams() {
        strNetworkID = CBaseChainParams::LEGACYTEST;
        consensus.digiwage_stake_modifier_v2_height = 320;
        consensus.digiwage_zerocoin_height = 250;
        consensus.digiwage_rhf_height = 350;
        // The contract fork is not scheduled yet; set a height here to rehearse it.
        consensus.digiwage_contract_height = std::numeric_limits<int>::max();
        consensus.digiwage_stake_min_depth = 50;
        consensus.digiwage_last_pow_height = 200;
        // Legacy GetNextWorkRequired uses the PoS retarget once the previous
        // block reaches height_last_PoW.
        consensus.digiwage_pos_retarget_prev_height = 200;
        consensus.digiwage_stake_modifier_new_selection_height = 300;
        consensus.digiwage_target_spacing = 30;
        consensus.digiwage_target_timespan = 30 * 60;
        consensus.digiwage_target_timespan_v2 = 15 * 60;
        consensus.digiwage_time_slot = 5;
        // A five minute stake age is shorter than the 2087 second modifier
        // selection interval, so legacy stakes do hit the zero modifier.
        consensus.digiwage_old_modifier_zero_fallback = true;
        consensus.digiwage_zerocoin_time_start = 1501776000; // legacy testnet ZC_TimeStart
        // SPORK_17_COLDSTAKING_ENFORCEMENT is off on the legacy testnet.
        consensus.digiwage_cold_staking_allowed = false;

        consensus.BIP65Height = 350; // height_start_BIP65 = height_RHF
        consensus.QIP5Height = consensus.digiwage_contract_height;
        consensus.QIP6Height = consensus.digiwage_contract_height;
        consensus.QIP7Height = consensus.digiwage_contract_height;
        consensus.nMuirGlacierHeight = consensus.digiwage_contract_height;
        consensus.nLondonHeight = consensus.digiwage_contract_height;
        consensus.nShanghaiHeight = consensus.digiwage_contract_height;
        // Dark Gravity Wave spacing (legacy nTargetSpacing).
        consensus.nPowTargetSpacing = 30;

        pchMessageStart[0] = 0x45;
        pchMessageStart[1] = 0x76;
        pchMessageStart[2] = 0x65;
        pchMessageStart[3] = 0xba;
        nDefaultPort = 46005;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 1;

        vSeeds.clear();
        vFixedSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,139);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,19);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x3a, 0x80, 0x61, 0xa0};
        base58Prefixes[EXT_SECRET_KEY] = {0x3a, 0x80, 0x58, 0x37};

        bech32_hrp = "dwt";

        // Legacy relays only standard transactions on testnet too.
        m_is_test_chain = true;

        checkpointData = {
            {
                {0, consensus.hashGenesisBlock},
            }
        };
        chainTxData = ChainTxData{0, 0, 0};

        consensus.nCoinbaseMaturity = 10;
        consensus.nLastPOWBlock = 200;
        consensus.nFirstMPoSBlock = consensus.nLastPOWBlock +
                                    consensus.nMPoSRewardRecipients +
                                    consensus.nCoinbaseMaturity;
        // Legacy stakers keep the whole reward: no MPoS range.
        consensus.nLastMPoSBlock = consensus.nFirstMPoSBlock;
        // Legacy DEFAULT_MAX_REORG_DEPTH.
        consensus.nCheckpointSpan = 100;
    }
};

/**
 * Testnet (v3): public test network which is reset from time to time.
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = CBaseChainParams::TESTNET;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 985500; // digiwage halving every 4 years
        consensus.script_flag_exceptions.emplace( // BIP16 exception
            uint256S("0x0000e803ee215c0684ca0d2f9220594d3f828617972aad66feb2ba51f5e14222"), SCRIPT_VERIFY_NONE);
        consensus.BIP34Height = 0;
        consensus.BIP34Hash = uint256S("0x0000e803ee215c0684ca0d2f9220594d3f828617972aad66feb2ba51f5e14222");
        consensus.BIP65Height = 0; // 00000000007f6655f22f98e72ed80d8b06dc761d5da09df0fa1dc4be4f861eb6
        consensus.BIP66Height = 0; // 000000002104c8c45e99a8853285a3b592602a3ccde2b832481da85e9e4ba182
        consensus.CSVHeight = 6048; // 00000000025e930139bac5c6c31a403776da130831ab85be56578f3fa75369bb
        consensus.SegwitHeight = 6048; // 00000000002b980fcd729daaa248fd9316a5200e9b367f4ff2c42453e84201ca
        consensus.MinBIP9WarningHeight = 8064; // segwit activation height + miner confirmation window
        consensus.QIP5Height = 446320;
        consensus.QIP6Height = 446320;
        consensus.QIP7Height = 446320;
        consensus.QIP9Height = 446320;
        consensus.nOfflineStakeHeight = 625000;
        consensus.nReduceBlocktimeHeight = 806600;
        consensus.nMuirGlacierHeight = 806600;
        consensus.nLondonHeight = 1967616;
        consensus.nShanghaiHeight = 3298892;
        consensus.powLimit = uint256S("0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.posLimit = uint256S("0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.QIP9PosLimit = uint256S("0000000000001fffffffffffffffffffffffffffffffffffffffffffffffffff"); // The new POS-limit activated after QIP9
        consensus.RBTPosLimit = uint256S("0000000000003fffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 16 * 60; // 16 minutes
        consensus.nPowTargetTimespanV2 = 4000;
        consensus.nRBTPowTargetTimespan = 1000;
        consensus.nPowTargetSpacing = 2 * 64;
        consensus.nRBTPowTargetSpacing = 32;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = true;
        consensus.fPoSNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Deployment of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        // Min block number for activation, the number must be divisible by 2016
        // Replace 0xffffc0 with the activation block number
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 1967616;

        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000213cff04c2108ab7d5a"); // 3180000
        consensus.defaultAssumeValid = uint256S("0xde6afcb300f7036b67b7446933b8aa7986850058d5927e07ce5df1e270069ef2"); // 3180000

        pchMessageStart[0] = 0x0d;
        pchMessageStart[1] = 0x22;
        pchMessageStart[2] = 0x15;
        pchMessageStart[3] = 0x06;
        nDefaultPort = 13888;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 10;
        m_assumed_chain_state_size = 1;

        genesis = CreateGenesisBlock(1504695029, 7349697, 0x1f00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        // Auxiliary DigiWage networks still require separate Digiwage migration.
        assert(consensus.hashGenesisBlock == uint256S("0x79bbf0c110656e33c4ed1e45005826d0298fdd3437267f2e968cbbcbe1042fad"));
        assert(genesis.hashMerkleRoot == uint256S("0xed34050eb5909ee535fcb07af292ea55f3d2f291187617b44d3282231405b96d"));

        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        vSeeds.emplace_back("digiwage4.dynu.net"); // DigiWage testnet

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,120);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,110);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tq";

        vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_test), std::end(chainparams_seed_test));

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        m_is_test_chain = true;
        m_is_mockable_chain = false;
        fHasHardwareWalletSupport = true;

        checkpointData = {
            {
                {0, uint256S("0x0000e803ee215c0684ca0d2f9220594d3f828617972aad66feb2ba51f5e14222")},
                {5000, uint256S("0x000000302bc22f2f65995506e757fff5c824545db5413e871d57d27a0997e8a0")}, //last PoW block
                {77000, uint256S("0xf41e2e8d09bca38827c23cad46ed6d434902da08415d2314d0c8ce285b1970cb")},
                {230000, uint256S("0xcd17baf80fa817dd543b83897ccb1e07350019e5b812f4956f69efe855d62601")},
                {343000, uint256S("0xac66f1de1a5fa473b5097b313c203e97d45669485e4c235a32a0f80df64f6948")},
                {441632, uint256S("0x2cb93f74cb3e47ec05b745a445f90a023b7136a68f94e9bff7fb49819155ccd8")},
                {491300, uint256S("0x75a7db2865423d3af5f0dfd70cfef6053b91f3c018c4b28a4e28c09a8c011e78")},
                {690000, uint256S("0x89b010b5333fa9d22c7fcf157c7eeaee1ccfe80c435390243b3d782a1fc1eff7")},
                {944000, uint256S("0x6bb6312088d81ca5484460b3466c66c01ff7d1cd4ef91e1dc9555a15b51d025d")},
                {1405000, uint256S("0xaff1f9c768e83f90d10a55306993e9042b5740251abc1afdde1429d09e95fa66")},
                {1930000, uint256S("0xf4836510a70e25d5c70554abbbcb346abd66af540f616d806fb1c20335c1e874")},
                {2686000, uint256S("0xc12594feff0dfae05f5a056cd9248ff5e6fc42d37c4bedf37b212eb17dccb486")},
                {3180000, uint256S("0xde6afcb300f7036b67b7446933b8aa7986850058d5927e07ce5df1e270069ef2")},
            }
        };

        m_assumeutxo_data = MapAssumeutxo{
            // TODO to be specified in a future patch.
        };

        chainTxData = ChainTxData{
            // Data as of block 0xde6afcb300f7036b67b7446933b8aa7986850058d5927e07ce5df1e270069ef2 (height 3180000)
            .nTime    = 1692923320,
            .nTxCount = 6552961,
            .dTxRate  = 0.06299127541669518,
        };

        consensus.nBlocktimeDownscaleFactor = 4;
        consensus.nCoinbaseMaturity = 500;
        consensus.nRBTCoinbaseMaturity = consensus.nBlocktimeDownscaleFactor*500;
        consensus.nSubsidyHalvingIntervalV2 = consensus.nBlocktimeDownscaleFactor*985500; // digiwage halving every 4 years (nSubsidyHalvingInterval * nBlocktimeDownscaleFactor)

        consensus.nLastPOWBlock = 5000;
        consensus.nLastBigReward = 5000;
        consensus.nMPoSRewardRecipients = 10;
        consensus.nFirstMPoSBlock = consensus.nLastPOWBlock + 
                                    consensus.nMPoSRewardRecipients + 
                                    consensus.nCoinbaseMaturity;
        consensus.nLastMPoSBlock = 624999;

        consensus.nFixUTXOCacheHFHeight = 84500;
        consensus.nEnableHeaderSignatureHeight = 391993;
        consensus.nCheckpointSpan = consensus.nCoinbaseMaturity;
        consensus.nRBTCheckpointSpan = consensus.nRBTCoinbaseMaturity;
        consensus.delegationsAddress = uint160(ParseHex("0000000000000000000000000000000000000086")); // Delegations contract for offline staking
        consensus.nStakeTimestampMask = 15;
        consensus.nRBTStakeTimestampMask = 3;
    }
};

/**
 * Signet: test network with an additional consensus parameter (see BIP325).
 */
class SigNetParams : public CChainParams {
public:
    explicit SigNetParams(const SigNetOptions& options)
    {
        std::vector<uint8_t> bin;
        vSeeds.clear();

        if (!options.challenge) {
            bin = ParseHex("51210276aa67f74d27c3dcd4be86ca8375a4d70b1e00f7787451d8445c647a3c099ee7210276aa67f74d27c3dcd4be86ca8375a4d70b1e00f7787451d8445c647a3c099ee752ae");

            consensus.nMinimumChainWork = uint256{};
            consensus.defaultAssumeValid = uint256{};
            m_assumed_blockchain_size = 1;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                // Data from RPC: getchaintxstats 4096 0000004429ef154f7e00b4f6b46bfbe2d2678ecd351d95bbfca437ab9a5b84ec
                .nTime    = 0,
                .nTxCount = 0,
                .dTxRate  = 0,
            };
        } else {
            bin = *options.challenge;
            consensus.nMinimumChainWork = uint256{};
            consensus.defaultAssumeValid = uint256{};
            m_assumed_blockchain_size = 0;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                0,
                0,
                0,
            };
            LogPrintf("Signet with challenge %s\n", HexStr(bin));
        }

        if (options.seeds) {
            vSeeds = *options.seeds;
        }

        strNetworkID = CBaseChainParams::SIGNET;
        consensus.signet_blocks = true;
        consensus.signet_challenge.assign(bin.begin(), bin.end());
        consensus.nSubsidyHalvingInterval = 985500;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 1;
        consensus.QIP5Height = 0;
        consensus.QIP6Height = 0;
        consensus.QIP7Height = 0;
        consensus.QIP9Height = 0;
        consensus.nOfflineStakeHeight = 1;
        consensus.nReduceBlocktimeHeight = 0;
        consensus.nMuirGlacierHeight = 0;
        consensus.nLondonHeight = 0;
        consensus.nShanghaiHeight = 0;
        consensus.powLimit = uint256S("0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.posLimit = uint256S("0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.QIP9PosLimit = uint256S("0000000000001fffffffffffffffffffffffffffffffffffffffffffffffffff"); // The new POS-limit activated after QIP9
        consensus.RBTPosLimit = uint256S("0000000000003fffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 16 * 60; // 16 minutes
        consensus.nPowTargetTimespanV2 = 4000;
        consensus.nRBTPowTargetTimespan = 1000;
        consensus.nPowTargetSpacing = 2 * 64;
        consensus.nRBTPowTargetSpacing = 32;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = true;
        consensus.fPoSNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1815; // 90% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.MinBIP9WarningHeight = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Activation of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        // message start is defined as the first 4 bytes of the sha256d of the block script
        HashWriter h{};
        h << consensus.signet_challenge;
        uint256 hash = h.GetHash();
        memcpy(pchMessageStart, hash.begin(), 4);

        nDefaultPort = 33888;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1623662135, 7377285, 0x1f00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x06a9b5d1753f199fece459fab4a5b4ecea7eff763c20cfd86ed73a2b891dc7b8"));
        assert(genesis.hashMerkleRoot == uint256S("0xed34050eb5909ee535fcb07af292ea55f3d2f291187617b44d3282231405b96d"));

        vFixedSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,120);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,110);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tq";

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        m_is_test_chain = true;
        m_is_mockable_chain = false;

        consensus.nBlocktimeDownscaleFactor = 4;
        consensus.nCoinbaseMaturity = 500;
        consensus.nRBTCoinbaseMaturity = consensus.nBlocktimeDownscaleFactor*500;
        consensus.nSubsidyHalvingIntervalV2 = consensus.nBlocktimeDownscaleFactor*985500; // digiwage halving every 4 years (nSubsidyHalvingInterval * nBlocktimeDownscaleFactor)

        consensus.nLastPOWBlock = 0x7fffffff;
        consensus.nLastBigReward = 5000;
        consensus.nMPoSRewardRecipients = 10;
        consensus.nFirstMPoSBlock = 5000;
        consensus.nLastMPoSBlock = 0;

        consensus.nFixUTXOCacheHFHeight = 0;
        consensus.nEnableHeaderSignatureHeight = 0;
        consensus.nCheckpointSpan = consensus.nCoinbaseMaturity;
        consensus.nRBTCheckpointSpan = consensus.nRBTCoinbaseMaturity;
        consensus.delegationsAddress = uint160(ParseHex("0000000000000000000000000000000000000086")); // Delegations contract for offline staking
        consensus.nStakeTimestampMask = 15;
        consensus.nRBTStakeTimestampMask = 3;
    }
};

/**
 * Regression test: intended for private networks only. Has minimal difficulty to ensure that
 * blocks can be found instantly.
 */
class CRegTestParams : public CChainParams
{
public:
    explicit CRegTestParams(const RegTestOptions& opts)
    {
        strNetworkID =  CBaseChainParams::REGTEST;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 985500;
        consensus.BIP34Height = 1; // Always active unless overridden
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1;  // Always active unless overridden
        consensus.BIP66Height = 1;  // Always active unless overridden
        consensus.CSVHeight = 1;    // Always active unless overridden
        consensus.SegwitHeight = 0; // Always active unless overridden
        consensus.MinBIP9WarningHeight = 0;
        consensus.QIP5Height = 0;
        consensus.QIP6Height = 0;
        consensus.QIP7Height = 0;
        consensus.QIP9Height = 0;
        consensus.nOfflineStakeHeight = 1;
        consensus.nReduceBlocktimeHeight = 0;
        consensus.nMuirGlacierHeight = 0;
        consensus.nLondonHeight = 0;
        consensus.nShanghaiHeight = 0;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.posLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.QIP9PosLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // The new POS-limit activated after QIP9
        consensus.RBTPosLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 16 * 60; // 16 minutes (960 = 832 + 128; multiplier is 832)
        consensus.nPowTargetTimespanV2 = 4000;
        consensus.nRBTPowTargetTimespan = 1000;
        consensus.nPowTargetSpacing = 2 * 64;
        consensus.nRBTPowTargetSpacing = 32;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.fPoSNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        pchMessageStart[0] = 0xfd;
        pchMessageStart[1] = 0xdd;
        pchMessageStart[2] = 0xc6;
        pchMessageStart[3] = 0xe1;
        nDefaultPort = 23888;
        nPruneAfterHeight = opts.fastprune ? 100 : 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        for (const auto& [dep, height] : opts.activation_heights) {
            switch (dep) {
            case Consensus::BuriedDeployment::DEPLOYMENT_SEGWIT:
                consensus.SegwitHeight = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_HEIGHTINCB:
                consensus.BIP34Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_DERSIG:
                consensus.BIP66Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_CLTV:
                consensus.BIP65Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_CSV:
                consensus.CSVHeight = int{height};
                break;
            }
        }

        for (const auto& [deployment_pos, version_bits_params] : opts.version_bits_parameters) {
            consensus.vDeployments[deployment_pos].nStartTime = version_bits_params.start_time;
            consensus.vDeployments[deployment_pos].nTimeout = version_bits_params.timeout;
            consensus.vDeployments[deployment_pos].min_activation_height = version_bits_params.min_activation_height;
        }

        genesis = CreateGenesisBlock(1504695029, 17, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0xd344bb7828b055e8361650e33906957c25aeef577f55059967f1e64a70d61aa9"));
        assert(genesis.hashMerkleRoot == uint256S("0xed34050eb5909ee535fcb07af292ea55f3d2f291187617b44d3282231405b96d"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();
        vSeeds.emplace_back("dummySeed.invalid.");

        fDefaultConsistencyChecks = true;
        fRequireStandard = true;
        fMineBlocksOnDemand = true;
        m_is_test_chain = true;
        m_is_mockable_chain = true;
        fHasHardwareWalletSupport = true;

        checkpointData = {
            {
                {0, uint256S("665ed5b402ac0b44efc37d8926332994363e8a7278b7ee9a58fb972efadae943")},
            }
        };

        m_assumeutxo_data = MapAssumeutxo{
         // TODO to be specified in a future patch.
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        consensus.nBlocktimeDownscaleFactor = 4;
        consensus.nCoinbaseMaturity = 500;
        consensus.nRBTCoinbaseMaturity = consensus.nBlocktimeDownscaleFactor*500;
        consensus.nSubsidyHalvingIntervalV2 = consensus.nBlocktimeDownscaleFactor*985500; // digiwage halving every 4 years (nSubsidyHalvingInterval * nBlocktimeDownscaleFactor)

        consensus.nLastPOWBlock = 0x7fffffff;
        consensus.nLastBigReward = 5000;
        consensus.nMPoSRewardRecipients = 10;
        consensus.nFirstMPoSBlock = 5000;
        consensus.nLastMPoSBlock = 0;

        consensus.nFixUTXOCacheHFHeight=0;
        consensus.nEnableHeaderSignatureHeight = 0;

        consensus.nCheckpointSpan = consensus.nCoinbaseMaturity;
        consensus.nRBTCheckpointSpan = consensus.nRBTCoinbaseMaturity;
        consensus.delegationsAddress = uint160(ParseHex("0000000000000000000000000000000000000086")); // Delegations contract for offline staking
        consensus.nStakeTimestampMask = 15;
        consensus.nRBTStakeTimestampMask = 3;

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,120);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,110);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "qcrt";
    }
};

/** Isolated two-node rehearsal network for the Digiwage version-6 fork. */
class CForkTestParams : public CRegTestParams
{
public:
    explicit CForkTestParams(const RegTestOptions& opts) : CRegTestParams(opts)
    {
        strNetworkID = CBaseChainParams::FORKTEST;
        consensus.digiwage_history = true;
        consensus.digiwage_contract_height = 30;
        consensus.digiwage_stake_modifier_v2_height = std::numeric_limits<int>::max();
        consensus.digiwage_zerocoin_height = std::numeric_limits<int>::max();
        consensus.digiwage_rhf_height = std::numeric_limits<int>::max();
        consensus.digiwage_stake_min_depth = 10;
        // digiwage_history is true here, so GetNextWorkRequired always goes
        // through DigiwageNextWork/DigiwageDarkGravityWave (pow.cpp), which
        // hardcode params.powLimit as the difficulty ceiling for every block
        // -- PoW or PoS alike -- regardless of fPoSNoRetargeting; posLimit is
        // never consulted on this path. CRegTestParams' inherited powLimit
        // (0x7fff..., ~2^255) overflows the un-fixed bnTarget *= bnWeight
        // kernel formula (nReduceBlocktimeHeight stays disabled below) for
        // any non-dust coin value, making organic staking unreachable no
        // matter how long you wait. Mainnet has the exact same overflow-prone
        // formula (it disables nReduceBlocktimeHeight too) and works around
        // it by keeping powLimit small enough (~2^236) that only fairly small
        // per-UTXO stake denominations (~0.01 DWG) stay overflow-safe -- real
        // wallets rely on coin splitting for this. Reuse that proven value
        // here instead of inventing a new one: staking UTXOs on forktest must
        // similarly be split down to small denominations before staking.
        consensus.powLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.posLimit = consensus.powLimit;
        consensus.digiwage_pos_limit_v2 = consensus.posLimit;

        consensus.CSVHeight = std::numeric_limits<int>::max();
        consensus.SegwitHeight = std::numeric_limits<int>::max();
        consensus.MinBIP9WarningHeight = std::numeric_limits<int>::max();
        consensus.QIP5Height = consensus.digiwage_contract_height;
        consensus.QIP6Height = consensus.digiwage_contract_height;
        consensus.QIP7Height = consensus.digiwage_contract_height;
        consensus.QIP9Height = std::numeric_limits<int>::max();
        consensus.nOfflineStakeHeight = std::numeric_limits<int>::max();
        consensus.nReduceBlocktimeHeight = std::numeric_limits<int>::max();
        consensus.nMuirGlacierHeight = consensus.digiwage_contract_height;
        consensus.nLondonHeight = consensus.digiwage_contract_height;
        consensus.nShanghaiHeight = consensus.digiwage_contract_height;
        consensus.nCoinbaseMaturity = 10;
        consensus.nLastPOWBlock = 50;
        consensus.nCheckpointSpan = consensus.nCoinbaseMaturity;

        pchMessageStart[0] = 0xd7;
        pchMessageStart[1] = 0x57;
        pchMessageStart[2] = 0xf6;
        pchMessageStart[3] = 0xa1;
        nDefaultPort = 34608;
        vFixedSeeds.clear();
        vSeeds.clear();

        const char* timestamp = "Digiwage isolated fork rehearsal 2026";
        const CScript output = CScript() << ParseHex("04682170b57e85aeae3ee34f858112040a933f6c48402620be4db4796e26f7d11d481f6ad9f05c471f8414c7ad7e1a90562906cff8b8c8b159666fbc4ff5af6904") << OP_CHECKSIG;
        genesis = CreateGenesisBlock(timestamp, output, 1789516800, 360499, 0x1e0fffff, 1, 50 * COIN, true);
        consensus.hashGenesisBlock = genesis.GetHash();
        checkpointData = {{{0, consensus.hashGenesisBlock}}};
    }
};

/**
 * Regression network parameters overwrites for unit testing
 */
class CUnitTestParams : public CRegTestParams
{
public:
    explicit CUnitTestParams(const RegTestOptions& opts)
    : CRegTestParams(opts)
    {
        // Activate the the BIPs for regtest as in Bitcoin
        consensus.BIP34Height = 100000000; // BIP34 has not activated on regtest (far in the future so block v1 are not rejected in tests)
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = consensus.nBlocktimeDownscaleFactor*500 + 851; // BIP65 activated on regtest (Used in rpc activation tests)
        consensus.BIP66Height = consensus.nBlocktimeDownscaleFactor*500 + 751; // BIP66 activated on regtest (Used in rpc activation tests)
        consensus.QIP6Height = consensus.nBlocktimeDownscaleFactor*500 + 500;
        consensus.QIP7Height = 0; // QIP7 activated on regtest

        // DIGIWAGE have 500 blocks of maturity, increased values for regtest in unit tests in order to correspond with it
        consensus.nSubsidyHalvingInterval = 750;
        consensus.nSubsidyHalvingIntervalV2 = consensus.nBlocktimeDownscaleFactor*750;
        consensus.nRuleChangeActivationThreshold = consensus.nBlocktimeDownscaleFactor*558; // 75% for testchains
        consensus.nMinerConfirmationWindow = consensus.nBlocktimeDownscaleFactor*744; // Faster than normal for regtest (744 instead of 2016)

        consensus.nBlocktimeDownscaleFactor = 4;
        consensus.nCoinbaseMaturity = 500;
        consensus.nRBTCoinbaseMaturity = consensus.nBlocktimeDownscaleFactor*500;

        consensus.nCheckpointSpan = consensus.nCoinbaseMaturity*2; // Increase the check point span for the reorganization tests from 500 to 1000
        consensus.nRBTCheckpointSpan = consensus.nRBTCoinbaseMaturity*2; // Increase the check point span for the reorganization tests from 500 to 1000

        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0;

        m_assumeutxo_data = MapAssumeutxo{
            {
                2010,
                {AssumeutxoHash{uint256S("0xf3ad83776715ee9b09a7a43421b6fe17701fb2247370a4ea9fcf0b073639cac9")}, 2010},
            },
            {
                2100,
                {AssumeutxoHash{uint256S("0x677f8902ca481677862d19fbe8c6214f596c8b475aabfe4273361485fc4e6fb4")}, 2100},
            },
        };
    }
};

std::unique_ptr<const CChainParams> CChainParams::SigNet(const SigNetOptions& options)
{
    return std::make_unique<const SigNetParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::RegTest(const RegTestOptions& options)
{
    return std::make_unique<const CRegTestParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::LegacyTest()
{
    return std::make_unique<const CLegacyTestParams>();
}

std::unique_ptr<const CChainParams> CChainParams::ForkTest(const RegTestOptions& options)
{
    return std::make_unique<const CForkTestParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::Main()
{
    return std::make_unique<const CMainParams>();
}

std::unique_ptr<const CChainParams> CChainParams::TestNet()
{
    return std::make_unique<const CTestNetParams>();
}

std::unique_ptr<const CChainParams> CChainParams::UnitTest(const RegTestOptions& options)
{
    return std::make_unique<const CUnitTestParams>(options);
}
