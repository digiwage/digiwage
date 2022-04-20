// Copyright (c) 2020 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "test/librust/sapling_test_fixture.h"
#include "sapling/sapling_util.h"

SaplingTestingSetup::SaplingTestingSetup(const std::string& chainName) : TestingSetup(chainName)
{
    initZKSNARKS(); // init zk-snarks lib
}

SaplingTestingSetup::~SaplingTestingSetup()
{
}

SaplingRegTestingSetup::SaplingRegTestingSetup() : SaplingTestingSetup(CBaseChainParams::REGTEST)
{
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V3_0, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
}
