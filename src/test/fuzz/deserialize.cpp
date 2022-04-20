// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "addrdb.h"
#include "addrman.h"
#include "primitives/block.h"
#include "addrman.h"
#include "chain.h"
#include "coins.h"
#include "compressor.h"
#include "consensus/merkle.h"
#include "policy/feerate.h"
#include "key.h"
#include "script/keyorigin.h"
#include "merkleblock.h"
#include "net.h"
#include "protocol.h"
#include "streams.h"
#include "undo.h"
#include "version.h"
#include "pubkey.h"

#include <exception>
#include <stdexcept>
#include <stdint.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#include "test/fuzz/fuzz.h"

namespace {

struct invalid_fuzzing_input_exception : public std::exception {
};

template <typename T>
CDataStream Serialize(const T& obj)
{
    CDataStream ds(SER_NETWORK, INIT_PROTO_VERSION);
    ds << obj;
    return ds;
}

template <typename T>
T Deserialize(CDataStream ds)
{
    T obj;
    ds >> obj;
    return obj;
}

template <typename T>
void DeserializeFromFuzzingInput(const std::vector<uint8_t>& buffer, T& obj)
{
    CDataStream ds(buffer, SER_NETWORK, INIT_PROTO_VERSION);
    try {
        int version;
        ds >> version;
        ds.SetVersion(version);
    } catch (const std::ios_base::failure&) {
        throw invalid_fuzzing_input_exception();
    }
    try {
        ds >> obj;
    } catch (const std::ios_base::failure&) {
        throw invalid_fuzzing_input_exception();
    }
    assert(buffer.empty() || !Serialize(obj).empty());
}

template <typename T>
void AssertEqualAfterSerializeDeserialize(const T& obj)
{
    assert(Deserialize<T>(Serialize(obj)) == obj);
}

} // namespace

void test_one_input(std::vector<uint8_t> buffer)
{
    try {
#if ADDR_INFO_DESERIALIZE
        CAddrInfo addr_info;
        DeserializeFromFuzzingInput(buffer, addr_info);
#elif BLOCK_FILE_INFO_DESERIALIZE
        CBlockFileInfo block_file_info;
        DeserializeFromFuzzingInput(buffer, block_file_info);
#elif FEE_RATE_DESERIALIZE
        CFeeRate fee_rate;
        DeserializeFromFuzzingInput(buffer, fee_rate);
        AssertEqualAfterSerializeDeserialize(fee_rate);
#elif MERKLE_BLOCK_DESERIALIZE
        CMerkleBlock merkle_block;
        DeserializeFromFuzzingInput(buffer, merkle_block);
#elif OUT_POINT_DESERIALIZE
        COutPoint out_point;
        DeserializeFromFuzzingInput(buffer, out_point);
        AssertEqualAfterSerializeDeserialize(out_point);
#elif PUB_KEY_DESERIALIZE
        CPubKey pub_key;
        DeserializeFromFuzzingInput(buffer, pub_key);
        // TODO: The following equivalence should hold for CPubKey? Fix.
        // AssertEqualAfterSerializeDeserialize(pub_key);
#elif SCRIPT_DESERIALIZE
        CScript script;
        DeserializeFromFuzzingInput(buffer, script);
#elif SUB_NET_DESERIALIZE
        CSubNet sub_net;
        DeserializeFromFuzzingInput(buffer, sub_net);
        AssertEqualAfterSerializeDeserialize(sub_net);
#elif TX_IN_DESERIALIZE
        CTxIn tx_in;
        DeserializeFromFuzzingInput(buffer, tx_in);
        AssertEqualAfterSerializeDeserialize(tx_in);
#elif KEY_ORIGIN_INFO_DESERIALIZE
        KeyOriginInfo key_origin_info;
        DeserializeFromFuzzingInput(buffer, key_origin_info);
        AssertEqualAfterSerializeDeserialize(key_origin_info);
#elif BLOCK_DESERIALIZE
        CBlock block;
        DeserializeFromFuzzingInput(buffer, block);
#elif BLOCKLOCATOR_DESERIALIZE
        CBlockLocator bl;
        DeserializeFromFuzzingInput(buffer, bl);
#elif BLOCKMERKLEROOT
        CBlock block;
        DeserializeFromFuzzingInput(buffer, block);
        bool mutated;
        BlockMerkleRoot(block, &mutated);
#elif ADDRMAN_DESERIALIZE
        CAddrMan am;
        DeserializeFromFuzzingInput(buffer, am);
#elif BLOCKHEADER_DESERIALIZE
        CBlockHeader bh;
        DeserializeFromFuzzingInput(buffer, bh);
#elif BANENTRY_DESERIALIZE
        CBanEntry be;
        DeserializeFromFuzzingInput(buffer, be);
#elif TXUNDO_DESERIALIZE
        CTxUndo tu;
        DeserializeFromFuzzingInput(buffer, tu);
#elif BLOCKUNDO_DESERIALIZE
        CBlockUndo bu;
        DeserializeFromFuzzingInput(buffer, bu);
#elif COINS_DESERIALIZE
        Coin coin;
        DeserializeFromFuzzingInput(buffer, coin);
#elif NETADDR_DESERIALIZE
        CNetAddr na;
        DeserializeFromFuzzingInput(buffer, na);
        AssertEqualAfterSerializeDeserialize(na);
#elif SERVICE_DESERIALIZE
        CService s;
        DeserializeFromFuzzingInput(buffer, s);
        AssertEqualAfterSerializeDeserialize(s);
#elif MESSAGEHEADER_DESERIALIZE
        const CMessageHeader::MessageStartChars pchMessageStart = {0x00, 0x00, 0x00, 0x00};
        CMessageHeader mh(pchMessageStart);
        DeserializeFromFuzzingInput(buffer, mh);
        (void)mh.IsValid(pchMessageStart);
#elif ADDRESS_DESERIALIZE
        CAddress a;
        DeserializeFromFuzzingInput(buffer, a);
#elif INV_DESERIALIZE
        CInv i;
        DeserializeFromFuzzingInput(buffer, i);
#elif BLOOMFILTER_DESERIALIZE
        CBloomFilter bf;
        DeserializeFromFuzzingInput(buffer, bf);
#elif DISKBLOCKINDEX_DESERIALIZE
        CDiskBlockIndex dbi;
        DeserializeFromFuzzingInput(buffer, dbi);
#elif TXOUTCOMPRESSOR_DESERIALIZE
        CTxOut to;
        auto toc = Using<TxOutCompression>(to);
        DeserializeFromFuzzingInput(buffer, toc);
#else
#error Need at least one fuzz target to compile
#endif
    } catch (const invalid_fuzzing_input_exception&) {
    }
}
