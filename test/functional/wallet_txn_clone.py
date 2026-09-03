#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the wallet accounts properly when there are cloned transactions with malleated scriptsigs."""

import io
from decimal import Decimal
from test_framework.blocktools import COIN, COINBASE_MATURITY, get_miner_reward
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
)
from test_framework.messages import CTransaction, COIN

class TxnMallTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.supports_cli = False

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def add_options(self, parser):
        parser.add_argument("--mineblock", dest="mine_block", default=False, action="store_true",
                            help="Test double-spend of 1-confirmed transaction")

    def setup_network(self):
        # Start with split network:
        super().setup_network()
        self.disconnect_nodes(1, 2)

    def run_test(self):
        # The cached chain does not hand the four nodes equal balances on Osmium the way it does
        # on Dash (12500 each): the height-1 premine dominates, so whichever node mined it holds
        # nearly everything. Work from node0's real balance instead of a fixed figure.
        starting_balance = self.nodes[0].getbalance()
        assert starting_balance > 100, "node0 holds %s, too little to run this test" % starting_balance
        # Keep the test's proportions -- it was written against a 12500 balance -- by scaling every
        # amount by what node0 actually has.
        scale = starting_balance / Decimal('12500')
        def amt(x):
            return (Decimal(x) * scale).quantize(Decimal('0.00000001'))
        send_self_1, send_self_2 = amt(12190), amt(290)
        send_1, send_2 = amt(400), amt(200)
        for i in range(4):
            self.nodes[i].getnewaddress()  # bug workaround, coins generated assigned to first getnewaddress!

        self.nodes[0].settxfee(.001)

        node0_address1 = self.nodes[0].getnewaddress()
        node0_txid1 = self.nodes[0].sendtoaddress(node0_address1, send_self_1)
        node0_tx1 = self.nodes[0].gettransaction(node0_txid1)

        node0_address2 = self.nodes[0].getnewaddress()
        node0_txid2 = self.nodes[0].sendtoaddress(node0_address2, send_self_2)
        node0_tx2 = self.nodes[0].gettransaction(node0_txid2)

        assert_equal(self.nodes[0].getbalance(),
                     starting_balance + node0_tx1["fee"] + node0_tx2["fee"])

        # Coins are sent to node1_address
        node1_address = self.nodes[1].getnewaddress()

        # Send tx1, and another transaction tx2 that won't be cloned
        txid1 = self.nodes[0].sendtoaddress(node1_address, send_1)
        txid2 = self.nodes[0].sendtoaddress(node1_address, send_2)

        # Construct a clone of tx1, to be malleated
        rawtx1 = self.nodes[0].getrawtransaction(txid1, 1)
        clone_inputs = [{"txid": rawtx1["vin"][0]["txid"], "vout": rawtx1["vin"][0]["vout"], "sequence": rawtx1["vin"][0]["sequence"]}]
        clone_outputs = {rawtx1["vout"][0]["scriptPubKey"]["addresses"][0]: rawtx1["vout"][0]["value"],
                         rawtx1["vout"][1]["scriptPubKey"]["addresses"][0]: rawtx1["vout"][1]["value"]}
        clone_locktime = rawtx1["locktime"]
        clone_raw = self.nodes[0].createrawtransaction(clone_inputs, clone_outputs, clone_locktime)

        # createrawtransaction randomizes the order of its outputs, so swap them if necessary.
        clone_tx = CTransaction()
        clone_tx.deserialize(io.BytesIO(bytes.fromhex(clone_raw)))
        if (rawtx1["vout"][0]["value"] == send_1 and clone_tx.vout[0].nValue != int(send_1*COIN) or rawtx1["vout"][0]["value"] != send_1 and clone_tx.vout[0].nValue == int(send_1*COIN)):
            (clone_tx.vout[0], clone_tx.vout[1]) = (clone_tx.vout[1], clone_tx.vout[0])

        # Use a different signature hash type to sign.  This creates an equivalent but malleated clone.
        # Don't send the clone anywhere yet
        tx1_clone = self.nodes[0].signrawtransactionwithwallet(clone_tx.serialize().hex(), None, "ALL|ANYONECANPAY")
        assert_equal(tx1_clone["complete"], True)

        # Have node0 mine a block, if requested:
        if (self.options.mine_block):
            self.nodes[0].generate(1)
            self.sync_blocks(self.nodes[0:2])

        tx1 = self.nodes[0].gettransaction(txid1)
        tx2 = self.nodes[0].gettransaction(txid2)

        # Node0's balance should be starting balance, plus 500 OSMI for another
        # matured block, minus tx1 and tx2 amounts, and minus transaction fees:
        expected = starting_balance + node0_tx1["fee"] + node0_tx2["fee"]
        if self.options.mine_block:
            # A block matured when node0 mined; its reward is the subsidy less the devfee, not
            # Dash's flat 500.
            expected += Decimal(get_miner_reward(self.nodes[0].getblockcount() - COINBASE_MATURITY)) / COIN
        expected += tx1["amount"] + tx1["fee"]
        expected += tx2["amount"] + tx2["fee"]
        assert_equal(self.nodes[0].getbalance(), expected)

        if self.options.mine_block:
            assert_equal(tx1["confirmations"], 1)
            assert_equal(tx2["confirmations"], 1)
        else:
            assert_equal(tx1["confirmations"], 0)
            assert_equal(tx2["confirmations"], 0)

        # Send clone and its parent to miner
        self.nodes[2].sendrawtransaction(node0_tx1["hex"])
        txid1_clone = self.nodes[2].sendrawtransaction(tx1_clone["hex"])
        # ... mine a block...
        self.nodes[2].generate(1)

        # Reconnect the split network, and sync chain:
        self.connect_nodes(1, 2)
        self.nodes[2].sendrawtransaction(node0_tx2["hex"])
        self.nodes[2].sendrawtransaction(tx2["hex"])
        self.nodes[2].generate(1)  # Mine another block to make sure we sync
        self.sync_blocks()

        # Re-fetch transaction info:
        tx1 = self.nodes[0].gettransaction(txid1)
        tx1_clone = self.nodes[0].gettransaction(txid1_clone)
        tx2 = self.nodes[0].gettransaction(txid2)

        # Verify expected confirmations
        assert_equal(tx1["confirmations"], -2)
        assert_equal(tx1_clone["confirmations"], 2)
        assert_equal(tx2["confirmations"], 1)

        # Check node0's total balance; same as before the clone plus whatever matured while node2
        # mined, less any orphaned matured subsidy. Dash could write 1000/500 because every block
        # paid 500; here the amount depends on the height and on which node's address the maturing
        # coinbase paid, so read it off the chain.
        def matured_to_node0(height):
            blk = self.nodes[0].getblock(self.nodes[0].getblockhash(height), 2)
            total = Decimal(0)
            for out in blk['tx'][0]['vout']:
                addr = out['scriptPubKey'].get('address') or (out['scriptPubKey'].get('addresses') or [None])[0]
                if addr and self.nodes[0].getaddressinfo(addr)['ismine']:
                    total += Decimal(str(out['value']))
            return total

        tip = self.nodes[0].getblockcount()
        expected += matured_to_node0(tip - COINBASE_MATURITY)
        expected += matured_to_node0(tip - COINBASE_MATURITY + 1)
        if (self.options.mine_block):
            expected -= matured_to_node0(tip - COINBASE_MATURITY + 2)
        assert_equal(self.nodes[0].getbalance(), expected)

if __name__ == '__main__':
    TxnMallTest().main()
