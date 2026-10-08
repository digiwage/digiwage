#ifndef DIGIWAGEUTILS_H
#define DIGIWAGEUTILS_H

#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>

/**
 * digiwageutils Provides utility functions to EVM for functionalities that already exist in digiwage
 */
namespace digiwageutils
{
/**
 * @brief btc_ecrecover Wrapper to CPubKey::RecoverCompact
 */
bool btc_ecrecover(dev::h256 const& hash, dev::u256 const& v, dev::h256 const& r, dev::h256 const& s, dev::h256 & key);


/**
 * @brief The ChainIdType enum Chain Id values for the networks
 */
enum ChainIdType
{
    MAIN = 46003,
    TESTNET = 46103,
    REGTEST = 46203,
};

/**
 * @brief eth_getChainId Get eth chain id
 * @param blockHeight Block height
 * @param shanghaiHeight Shanghai fork height
 * @param chain Network ID
 * @return chain id
 */
int eth_getChainId(int blockHeight, int shanghaiHeight, const std::string& chain);

/**
 * @brief eth_getChainId Get eth chain id and cache it
 * @param blockHeight Block height
 * @return chain id
 */
int eth_getChainId(int blockHeight);

}

#endif
