// Copyright (c) 2012-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dbwrapper.h"
#include "uint256.h"
#include "random.h"
#include "test/test_digiwage.h"

#include <boost/test/unit_test.hpp>

// Test if a string consists entirely of null characters
bool is_null_key(const std::vector<unsigned char>& key) {
    bool isnull = true;

    for (unsigned int i = 0; i < key.size(); i++)
        isnull &= (key[i] == '\x00');

    return isnull;
}

BOOST_FIXTURE_TEST_SUITE(dbwrapper_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(dbwrapper)
{
    {
        fs::path ph = SetDataDir(std::string("dbwrapper"));
        CDBWrapper dbw(ph, (1 << 20), true, false);
        char key = 'k';
        uint256 in = GetRandHash();
        uint256 res;

        BOOST_CHECK(dbw.Write(key, in));
        BOOST_CHECK(dbw.Read(key, res));
        BOOST_CHECK_EQUAL(res.ToString(), in.ToString());
    }
}

BOOST_AUTO_TEST_CASE(dbwrapper_basic_data)
{
    fs::path ph = SetDataDir(std::string("dbwrapper_basic_data"));
    CDBWrapper dbw(ph, (1 << 20), false, true);

    uint256 res;
    uint32_t res_uint_32;
    bool res_bool;

    //Simulate block raw data - "b + block hash"
    std::string key_block = "b" + InsecureRand256().ToString();

    uint256 in_block = InsecureRand256();
    BOOST_CHECK(dbw.Write(key_block, in_block));
    BOOST_CHECK(dbw.Read(key_block, res));
    BOOST_CHECK_EQUAL(res.ToString(), in_block.ToString());

    //Simulate file raw data - "f + file_number"
    std::string key_file = strprintf("f%04x", InsecureRand32());

    uint256 in_file_info = InsecureRand256();
    BOOST_CHECK(dbw.Write(key_file, in_file_info));
    BOOST_CHECK(dbw.Read(key_file, res));
    BOOST_CHECK_EQUAL(res.ToString(), in_file_info.ToString());

    //Simulate transaction raw data - "t + transaction hash"
    std::string key_transaction = "t" + InsecureRand256().ToString();

    uint256 in_transaction = InsecureRand256();
    BOOST_CHECK(dbw.Write(key_transaction, in_transaction));
    BOOST_CHECK(dbw.Read(key_transaction, res));
    BOOST_CHECK_EQUAL(res.ToString(), in_transaction.ToString());

    //Simulate UTXO raw data - "c + transaction hash"
    std::string key_utxo = "c" + InsecureRand256().ToString();

    uint256 in_utxo = InsecureRand256();
    BOOST_CHECK(dbw.Write(key_utxo, in_utxo));
    BOOST_CHECK(dbw.Read(key_utxo, res));
    BOOST_CHECK_EQUAL(res.ToString(), in_utxo.ToString());

    //Simulate last block file number - "l"
    char key_last_blockfile_number = 'l';
    uint32_t lastblockfilenumber = InsecureRand32();
    BOOST_CHECK(dbw.Write(key_last_blockfile_number, lastblockfilenumber));
    BOOST_CHECK(dbw.Read(key_last_blockfile_number, res_uint_32));
    BOOST_CHECK_EQUAL(lastblockfilenumber, res_uint_32);

    //Simulate Is Reindexing - "R"
    char key_IsReindexing = 'R';
    bool isInReindexing = InsecureRandBool();
    BOOST_CHECK(dbw.Write(key_IsReindexing, isInReindexing));
    BOOST_CHECK(dbw.Read(key_IsReindexing, res_bool));
    BOOST_CHECK_EQUAL(isInReindexing, res_bool);

    //Simulate last block hash up to which UXTO covers - 'B'
    char key_lastblockhash_uxto = 'B';
    uint256 lastblock_hash = InsecureRand256();
    BOOST_CHECK(dbw.Write(key_lastblockhash_uxto, lastblock_hash));
    BOOST_CHECK(dbw.Read(key_lastblockhash_uxto, res));
    BOOST_CHECK_EQUAL(lastblock_hash, res);

    //Simulate file raw data - "F + filename_number + filename"
    std::string file_option_tag = "F";
    uint8_t filename_length = InsecureRandBits(8);
    std::string filename = "randomfilename";
    std::string key_file_option = strprintf("%s%01x%s", file_option_tag,filename_length,filename);

    bool in_file_bool = InsecureRandBool();
    BOOST_CHECK(dbw.Write(key_file_option, in_file_bool));
    BOOST_CHECK(dbw.Read(key_file_option, res_bool));
    BOOST_CHECK_EQUAL(res_bool, in_file_bool);
}

// Test batch operations
BOOST_AUTO_TEST_CASE(dbwrapper_batch)
{
    {
        fs::path ph = SetDataDir(std::string("dbwrapper_batch"));
        CDBWrapper dbw(ph, (1 << 20), true, false);

        char key = 'i';
        uint256 in = GetRandHash();
        char key2 = 'j';
        uint256 in2 = GetRandHash();
        char key3 = 'k';
        uint256 in3 = GetRandHash();

        uint256 res;
        CDBBatch batch;

        batch.Write(key, in);
        batch.Write(key2, in2);
        batch.Write(key3, in3);

        // Remove key3 before it's even been written
        batch.Erase(key3);

        dbw.WriteBatch(batch);

        BOOST_CHECK(dbw.Read(key, res));
        BOOST_CHECK_EQUAL(res.ToString(), in.ToString());
        BOOST_CHECK(dbw.Read(key2, res));
        BOOST_CHECK_EQUAL(res.ToString(), in2.ToString());

        // key3 never should've been written
        BOOST_CHECK(dbw.Read(key3, res) == false);
    }
}

BOOST_AUTO_TEST_CASE(dbwrapper_iterator)
{
    {
        fs::path ph = SetDataDir(std::string("dbwrapper_iterator"));
        CDBWrapper dbw(ph, (1 << 20), true, false);

        // The two keys are intentionally chosen for ordering
        char key = 'j';
        uint256 in = GetRandHash();
        BOOST_CHECK(dbw.Write(key, in));
        char key2 = 'k';
        uint256 in2 = GetRandHash();
        BOOST_CHECK(dbw.Write(key2, in2));

        std::unique_ptr<CDBIterator> it(const_cast<CDBWrapper&>(dbw).NewIterator());

        // Be sure to seek past any earlier key (if it exists)
        it->Seek(key);

        char key_res;
        uint256 val_res;

        it->GetKey(key_res);
        it->GetValue(val_res);
        BOOST_CHECK_EQUAL(key_res, key);
        BOOST_CHECK_EQUAL(val_res.ToString(), in.ToString());

        it->Next();

        it->GetKey(key_res);
        it->GetValue(val_res);
        BOOST_CHECK_EQUAL(key_res, key2);
        BOOST_CHECK_EQUAL(val_res.ToString(), in2.ToString());

        it->Next();
        BOOST_CHECK_EQUAL(it->Valid(), false);
    }
}

BOOST_AUTO_TEST_CASE(iterator_ordering)
{
    fs::path ph = SetDataDir(std::string("iterator_ordering"));
    CDBWrapper dbw(ph, (1 << 20), true, false);
    for (int x=0x00; x<256; ++x) {
        uint8_t key = x;
        uint32_t value = x*x;
        if (!(x & 1)) BOOST_CHECK(dbw.Write(key, value));
    }

    // Check that creating an iterator creates a snapshot
    std::unique_ptr<CDBIterator> it(const_cast<CDBWrapper&>(dbw).NewIterator());

    for (int x=0x00; x<256; ++x) {
        uint8_t key = x;
        uint32_t value = x*x;
        if (x & 1) BOOST_CHECK(dbw.Write(key, value));
    }

    for (int seek_start : {0x00, 0x80}) {
        it->Seek((uint8_t)seek_start);
        for (int x=seek_start; x<255; ++x) {
            uint8_t key;
            uint32_t value;
            BOOST_CHECK(it->Valid());
            if (!it->Valid()) // Avoid spurious errors about invalid iterator's key and value in case of failure
                break;
            BOOST_CHECK(it->GetKey(key));
            if (x & 1) {
                BOOST_CHECK_EQUAL(key, x + 1);
                continue;
            }
            BOOST_CHECK(it->GetValue(value));
            BOOST_CHECK_EQUAL(key, x);
            BOOST_CHECK_EQUAL(value, x*x);
            it->Next();
        }
        BOOST_CHECK(!it->Valid());
    }
}

struct StringContentsSerializer {
    // Used to make two serialized objects the same while letting them have a different lengths
    // This is a terrible idea
    std::string str;
    StringContentsSerializer() {}
    StringContentsSerializer(const std::string& inp) : str(inp) {}

    StringContentsSerializer& operator+=(const std::string& s) {
        str += s;
        return *this;
    }
    StringContentsSerializer& operator+=(const StringContentsSerializer& s) { return *this += s.str; }

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        for (size_t i = 0; i < str.size(); i++) {
            s << str[i];
        }
    }

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        str.clear();
        char c = 0;
        while (true) {
            try {
                s >> c;
                str.push_back(c);
            } catch (const std::ios_base::failure&) {
                break;
            }
        }
    }
};

BOOST_AUTO_TEST_CASE(iterator_string_ordering)
{
    char buf[10];

    fs::path ph = SetDataDir(std::string("iterator_string_ordering"));
    CDBWrapper dbw(ph, (1 << 20), true, false);
    for (int x=0x00; x<10; ++x) {
        for (int y = 0; y < 10; y++) {
            snprintf(buf, sizeof(buf), "%d", x);
            StringContentsSerializer key(buf);
            for (int z = 0; z < y; z++)
                key += key;
            uint32_t value = x*x;
            BOOST_CHECK(dbw.Write(key, value));
        }
    }

    boost::scoped_ptr<CDBIterator> it(const_cast<CDBWrapper*>(&dbw)->NewIterator());
    for (int c=0; c<2; ++c) {
        int seek_start;
        if (c == 0)
            seek_start = 0;
        else
            seek_start = 5;
        snprintf(buf, sizeof(buf), "%d", seek_start);
        StringContentsSerializer seek_key(buf);
        it->Seek(seek_key);
        for (int x=seek_start; x<10; ++x) {
            for (int y = 0; y < 10; y++) {
                snprintf(buf, sizeof(buf), "%d", x);
                std::string exp_key(buf);
                for (int z = 0; z < y; z++)
                    exp_key += exp_key;
                StringContentsSerializer key;
                uint32_t value;
                BOOST_CHECK(it->Valid());
                if (!it->Valid()) // Avoid spurious errors about invalid iterator's key and value in case of failure
                    break;
                BOOST_CHECK(it->GetKey(key));
                BOOST_CHECK(it->GetValue(value));
                BOOST_CHECK_EQUAL(key.str, exp_key);
                BOOST_CHECK_EQUAL(value, x*x);
                it->Next();
            }
        }
        BOOST_CHECK(!it->Valid());
    }
}



BOOST_AUTO_TEST_SUITE_END()
