#include <primitives/block.h>
#include <chain.h>
#include <key.h>
#include <protocol.h>
#include <script/sign.h>
#include <script/standard.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>
#include <validation.h>
#include <version.h>

#include <boost/test/unit_test.hpp>

bool CheckBlockSignature(const CBlock& block);

BOOST_AUTO_TEST_SUITE(digiwage_header_tests)

BOOST_AUTO_TEST_CASE(early_block_hashes)
{
    // Headers from the local Digiwage blk00000.dat. Expected hashes are
    // independently encoded as prev-hashes in the following blocks.
    const std::pair<const char*, const char*> vectors[] = {
        {"0100000000000000000000000000000000000000000000000000000000000000000000009d3020008d982af12c41cac7bce9165c02a0d0da692e535097b3eecbba0da7dd82deb95af0ff0f1e018d3800", "000009f854e700ab62642c7d3e94be65a1d8c112384f5edfb4b2b3fa3fecaef6"},
        {"03000000f6aeec3ffab3b2b4df5e4f3812c1d8a165be943e7d2c6462ab00e754f8090000476b907dd5e48da771cdf80c3adf50f89895f20017dab1a4731a2be3b716f870225fba5affff0f1ee5830a00", "00000338b3b3a68bed1c0590ac709bd8ae0fb9aa986079ebdb6107f6488e8421"},
        {"0300000021848e48f60761dbeb796098aab90faed89b70ac90051ced8ba6b3b338030000a3fb07fe68f0f59fcfb3b708a24f3e402c80c4dcc1df0fdfd57e93670e069f66235fba5affff0f1efb5c0000", "000007050f9bce7b4f609cba3314db575bfab1ecdd3181d9912efefc3d4a4068"},
        {"030000005dc52fdacdbf191a1bc3b4bb90d38c690cc9c8a62f8aa98087034a432a0000008bb5ec7aab5315054a8f3bbc0e0036a25c89d55c2f675edb4fa14574142d1938827bbc5a5806011e29081a00", "0000005a2f56efc4caddb8c454bb2b72e18ed847d3d5fb3fe1edca31a12c86ad"},
        {"03000000ad862ca131caede13ffbd5d347d88ee1722bbb54c4b8ddcac4ef562f5a000000edc50fabd8e454466c2017eece6ce463945eab9dbd9da2f75079362339134e29b07bbc5acb0a011e00000000", "6bb464486837298524571368ad8005b8ca209c71041525f734cc47238dddb28e"},
        {"0300000035a94a6cd020ada9e8955bfafd935a32ae25478001e3288ea82d67898c52ca177afd8644fc556e52b9ddcc03d31aad806417c835d66b4806cf1b19a0a1c17f9e43e2225b9ec8001b00000000", "c7ed5a1254356e64068579ff91f5a05af9ac2aca8b44931efa10aecfb43c0431"},
    };
    for (const auto& [raw, expected] : vectors) {
        CDataStream stream(ParseHex(raw), SER_NETWORK, PROTOCOL_VERSION);
        CBlockHeader header;
        stream >> header;
        BOOST_CHECK_EQUAL(header.GetHash().ToString(), expected);
        BOOST_CHECK_EQUAL(GetSerializeSize(header, PROTOCOL_VERSION), 80U);
    }
}

BOOST_AUTO_TEST_CASE(first_proof_of_stake_block_roundtrip)
{
    // Full block at height 1001 from Digiwage blk00000.dat.
    const std::string raw =
        "03000000ad862ca131caede13ffbd5d347d88ee1722bbb54c4b8ddcac4ef562f5a000000edc50fabd8e454466c2017eece6c"
        "e463945eab9dbd9da2f75079362339134e29b07bbc5acb0a011e000000000301000000010000000000000000000000000000"
        "000000000000000000000000000000000000ffffffff0502e9030101ffffffff010000000000000000000000000001000000"
        "01ef09507cfda9d9d9440a3bc6a7f384e6335bae0c50d87cbe9a02ed3d4b01b2a6010000006a47304402202f6d51a0d9fd82"
        "f48ea723feb455b843bd681391ccc046305d1682dff4732f4f0220095e6d96d521decd7473bbc3ff03ee9657341437a6f058"
        "9e36c43d529daaa535012102802cd97aaa5076b73bf2bec7f2923f5242975280a28fb1f3b3e698ee21247992ffffffff0300"
        "0000000000000000f07341cb02000000232102802cd97aaa5076b73bf2bec7f2923f5242975280a28fb1f3b3e698ee212479"
        "92ac0050d6dc010000001976a9147eec23ddf82dd856eddcea011b502d5086b6ea9688ac0000000001000000019e1d373683"
        "a26fa3166fed5e4202a1526276790139cb0754c225230c19eb97f3060000006b483045022100de8d1d1b6aa5a803cbe7d11e"
        "43649a77eacdfa147713e56179130eab3b1d499202204551346f8b0fb6329bce32ac3e7d222129e7f7e7b9fb77ebb581f149"
        "9f85e6c901210384548b3a5cdc6eab90c8980662132b19408d38cff37fd16ea4e61855041d0f63ffffffff022c9d906d0600"
        "00001976a91444526904c3f7c3bf964ecec38d54c4f44820ae7288ac00e09265170100001976a91482c16e60e811bf2098dd"
        "a6f6a99d59c11f293b1f88ac00000000463044022004c5a66f0601470e347790c36324087af86e79431244ebb0f46ff5c75d"
        "f82d1f0220042fcc38c626867f40ec7f6a7a7d8bf13aa9ab3b83ad5490a3597525ff605259"
        ;
    CDataStream stream(ParseHex(raw), SER_NETWORK, PROTOCOL_VERSION);
    CBlock block;
    stream >> block;
    BOOST_CHECK(stream.empty());
    BOOST_CHECK(block.IsProofOfStake());
    BOOST_CHECK_EQUAL(block.vchBlockSig.size(), 70U);
    BOOST_CHECK_EQUAL(block.GetHash().ToString(), "6bb464486837298524571368ad8005b8ca209c71041525f734cc47238dddb28e");
    BOOST_CHECK(CheckBlockSignature(block));
    block.vchBlockSig.back() ^= 1;
    BOOST_CHECK(!CheckBlockSignature(block));
    BOOST_CHECK_EQUAL(GetSerializeSize(block, PROTOCOL_VERSION), 687U);
}

BOOST_AUTO_TEST_CASE(digiwage_reward_height_mapping)
{
    Consensus::Params params{};
    params.digiwage_history = true;
    BOOST_CHECK_EQUAL(GetBlockSubsidy(1, params), 27179800 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(86400, params), 120 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(86401, params), 60 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(259200, params), 60 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(259201, params), 30 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(604800, params), 15 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(604801, params), 75 * COIN / 10);
}

BOOST_AUTO_TEST_CASE(version_four_accumulator_survives_block_index)
{
    CBlockHeader header;
    header.nVersion = 4;
    header.nTime = 1522301872;
    header.nBits = 0x1e010acb;
    header.nAccumulatorCheckpoint = uint256S("123456789abcdef0");
    CBlockIndex index(header);
    BOOST_CHECK(index.GetBlockHeader().GetHash() == header.GetHash());

    CDiskBlockIndex disk(&index);
    CDataStream stream(SER_DISK, CLIENT_VERSION);
    stream << disk;
    CDiskBlockIndex restored;
    stream >> restored;
    BOOST_CHECK(restored.ConstructBlockHash() == header.GetHash());
}

BOOST_AUTO_TEST_CASE(version_six_commits_contract_state_and_stake)
{
    CBlockHeader header;
    header.nVersion = 6;
    header.hashPrevBlock = uint256S("01");
    header.hashMerkleRoot = uint256S("02");
    header.nTime = 1700000000;
    header.nBits = 0x1e00ffff;
    header.hashStateRoot = uint256S("03");
    header.hashUTXORoot = uint256S("04");
    header.prevoutStake = COutPoint{uint256S("05"), 7};

    CDataStream encoded(SER_NETWORK, PROTOCOL_VERSION);
    encoded << header;
    BOOST_CHECK_EQUAL(encoded.size(), 181U);
    const uint256 committed_hash = header.GetHash();

    CBlockHeader decoded;
    encoded >> decoded;
    BOOST_CHECK(decoded.hashStateRoot == header.hashStateRoot);
    BOOST_CHECK(decoded.hashUTXORoot == header.hashUTXORoot);
    BOOST_CHECK(decoded.prevoutStake == header.prevoutStake);
    BOOST_CHECK(decoded.GetHash() == committed_hash);

    decoded.hashStateRoot = uint256S("06");
    BOOST_CHECK(decoded.GetHash() != committed_hash);
}

BOOST_AUTO_TEST_CASE(version_six_signature_covers_contract_roots)
{
    BasicTestingSetup setup;
    CKey key;
    key.MakeNewKey(true);

    CMutableTransaction coinbase;
    CMutableTransaction coinstake;
    coinstake.vin.emplace_back(COutPoint{uint256S("11"), 1});
    coinstake.vout.emplace_back(0, CScript{});
    coinstake.vout.emplace_back(1, GetScriptForRawPubKey(key.GetPubKey()));

    CBlock block;
    block.nVersion = 6;
    block.nTime = 1700000000;
    block.nBits = 0x1e00ffff;
    block.hashStateRoot = uint256S("22");
    block.hashUTXORoot = uint256S("33");
    block.prevoutStake = coinstake.vin[0].prevout;
    block.vtx = {MakeTransactionRef(coinbase), MakeTransactionRef(coinstake)};

    BOOST_REQUIRE(SignBlockStake(block, key, false));
    BOOST_CHECK(block.vchBlockSig.empty());
    BOOST_CHECK(!block.vchBlockSigDlgt.empty());
    BOOST_CHECK(CheckBlockSignature(block));

    CDataStream encoded(SER_NETWORK, PROTOCOL_VERSION);
    encoded << block;
    CBlock decoded;
    encoded >> decoded;
    BOOST_CHECK(encoded.empty());
    BOOST_CHECK(CheckBlockSignature(decoded));
    decoded.hashStateRoot = uint256S("44");
    BOOST_CHECK(!CheckBlockSignature(decoded));
}

BOOST_AUTO_TEST_CASE(contract_execution_activates_atomically)
{
    Consensus::Params params{};
    params.digiwage_history = true;
    params.digiwage_contract_height = 4000000;
    params.QIP5Height = 4000000;

    BOOST_CHECK_EQUAL(GetContractScriptFlags(3999999, params), 0U);
    const unsigned int active_flags = GetContractScriptFlags(4000000, params);
    BOOST_CHECK(active_flags & SCRIPT_EXEC_BYTE_CODE);
    BOOST_CHECK(active_flags & SCRIPT_OUTPUT_SENDER);
}

BOOST_AUTO_TEST_CASE(cold_stake_script_is_recognized)
{
    const std::vector<unsigned char> staker(20, 0x11);
    const std::vector<unsigned char> owner(20, 0x22);
    const CScript script = CScript{} << OP_DUP << OP_HASH160 << OP_ROT << OP_IF
        << OP_CHECKCOLDSTAKEVERIFY << staker << OP_ELSE << owner << OP_ENDIF
        << OP_EQUALVERIFY << OP_CHECKSIG;
    std::vector<std::vector<unsigned char>> solutions;
    BOOST_CHECK(Solver(script, solutions) == TxoutType::COLDSTAKE);
    BOOST_REQUIRE_EQUAL(solutions.size(), 2U);
    BOOST_CHECK(solutions[0] == staker);
    BOOST_CHECK(solutions[1] == owner);
    CTxDestination destination;
    BOOST_REQUIRE(ExtractDestination(script, destination));
    BOOST_CHECK(destination == CTxDestination{PKHash{uint160{owner}}});
}

BOOST_AUTO_TEST_SUITE_END()
