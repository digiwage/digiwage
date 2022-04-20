#!/usr/bin/env python3
# Copyright (c) 2018 The Zcash developers
# Copyright (c) 2020 The DIGIWAGE developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

from decimal import Decimal

from test_framework.test_framework import PivxTestFramework
from test_framework.util import (
    assert_equal,
)


class SaplingWalletSend(PivxTestFramework):

    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        saplingUpgrade = ['-nuparams=v5_shield:201']
        self.extra_args = [saplingUpgrade, saplingUpgrade, saplingUpgrade]

    def run_test(self):
        self.log.info("Mining...")
        self.nodes[0].generate(2)
        self.sync_all()
        self.nodes[2].generate(200)
        self.sync_all()
        assert_equal(self.nodes[1].getblockcount(), 202)
        taddr1 = self.nodes[1].getnewaddress()
        saplingAddr1 = self.nodes[1].getnewshieldaddress()

        # Verify addresses
        assert(saplingAddr1 in self.nodes[1].listshieldaddresses())
        assert_equal(self.nodes[1].getshieldbalance(saplingAddr1), Decimal('0'))
        assert_equal(self.nodes[1].getreceivedbyaddress(taddr1), Decimal('0'))

        # Test subtract fee from recipient
        self.log.info("Checking sendto[shield]address with subtract-fee-from-amt")
        node_0_bal = self.nodes[0].getbalance()
        node_1_bal = self.nodes[1].getbalance()
        txid = self.nodes[0].sendtoaddress(saplingAddr1, 10, "", "", True)
        node_0_bal -= Decimal('10')
        assert_equal(self.nodes[0].getbalance(), node_0_bal)
        self.sync_mempools()
        self.nodes[2].generate(1)
        self.sync_all()
        feeTx = self.nodes[0].gettransaction(txid)["fee"]     # fee < 0
        saplingAddr1_bal = (Decimal('10') + feeTx)
        node_1_bal += saplingAddr1_bal
        assert_equal(self.nodes[1].getbalance(), node_1_bal)

        self.log.info("Checking shieldsendmany with subtract-fee-from-amt")
        node_2_bal = self.nodes[2].getbalance()
        recipients1 = [{"address": saplingAddr1, "amount": Decimal('10')},
                       {"address": self.nodes[0].getnewshieldaddress(), "amount": Decimal('5')}]
        subtractfeefrom = [saplingAddr1]
        txid = self.nodes[2].shieldsendmany("from_transparent", recipients1, 1, 0, subtractfeefrom)
        node_2_bal -= Decimal('15')
        assert_equal(self.nodes[2].getbalance(), node_2_bal)
        self.nodes[2].generate(1)
        self.sync_all()
        feeTx = self.nodes[2].gettransaction(txid)["fee"]     # fee < 0
        node_1_bal += (Decimal('10') + feeTx)
        saplingAddr1_bal += (Decimal('10') + feeTx)
        assert_equal(self.nodes[1].getbalance(), node_1_bal)
        node_0_bal += Decimal('5')
        assert_equal(self.nodes[0].getbalance(), node_0_bal)

        self.log.info("Checking sendmany to shield with subtract-fee-from-amt")
        node_2_bal = self.nodes[2].getbalance()
        txid = self.nodes[2].sendmany('', {saplingAddr1: 10, taddr1: 10},
                                      1, "", False, [saplingAddr1, taddr1])
        node_2_bal -= Decimal('20')
        assert_equal(self.nodes[2].getbalance(), node_2_bal)
        self.nodes[2].generate(1)
        self.sync_all()
        feeTx = self.nodes[2].gettransaction(txid)["fee"]     # fee < 0
        node_1_bal += (Decimal('20') + feeTx)
        assert_equal(self.nodes[1].getbalance(), node_1_bal)
        taddr1_bal = Decimal('10') + feeTx/2
        saplingAddr1_bal += Decimal('10') + feeTx / 2
        assert_equal(self.nodes[1].getreceivedbyaddress(taddr1), taddr1_bal)
        assert_equal(self.nodes[1].getshieldbalance(saplingAddr1), saplingAddr1_bal)


if __name__ == '__main__':
    SaplingWalletSend().main()
