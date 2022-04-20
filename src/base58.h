// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2017-2020 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Why base-58 instead of standard base-64 encoding?
 * - Don't want 0OIl characters that look the same in some fonts and
 *      could be used to create visually identical looking account numbers.
 * - A string with non-alphanumeric characters is not as easily accepted as an account number.
 * - E-mail usually won't line-break if there's no punctuation to break at.
 * - Double-clicking selects the whole number as one word if it's all alphanumeric.
 */
#ifndef BITCOIN_BASE58_H
#define BITCOIN_BASE58_H

#include "attributes.h"
#include "chainparams.h"
#include "key.h"
#include "pubkey.h"
#include "script/standard.h"

#include <string>
#include <vector>

/**
 * Encode a byte sequence as a base58-encoded string.
 * pbegin and pend cannot be NULL, unless both are.
 */
std::string EncodeBase58(const unsigned char* pbegin, const unsigned char* pend);

/**
 * Encode a byte vector as a base58-encoded string
 */
std::string EncodeBase58(const std::vector<unsigned char>& vch);

/**
 * Decode a base58-encoded string (psz) into a byte vector (vchRet).
 * return true if decoding is successful.
 * psz cannot be NULL.
 */
NODISCARD bool DecodeBase58(const char* psz, std::vector<unsigned char>& vchRet, int max_ret_len);

/**
 * Decode a base58-encoded string (str) into a byte vector (vchRet).
 * return true if decoding is successful.
 */
NODISCARD bool DecodeBase58(const std::string& str, std::vector<unsigned char>& vchRet, int max_ret_len);

/**
 * Encode a byte vector into a base58-encoded string, including checksum
 */
std::string EncodeBase58Check(const std::vector<unsigned char>& vchIn);

/**
 * Decode a base58-encoded string (psz) that includes a checksum into a byte
 * vector (vchRet), return true if decoding is successful
 */
NODISCARD bool DecodeBase58Check(const char* psz, std::vector<unsigned char>& vchRet, int max_ret_len);

/**
 * Decode a base58-encoded string (str) that includes a checksum into a byte
 * vector (vchRet), return true if decoding is successful
 */
NODISCARD bool DecodeBase58Check(const std::string& str, std::vector<unsigned char>& vchRet, int max_ret_len);

#endif // BITCOIN_BASE58_H
