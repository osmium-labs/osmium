#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test coinstatsindex across nodes.

Test that the values returned by gettxoutsetinfo are consistent
between a node running the coinstatsindex and a node without
the index.
"""

import struct

from decimal import Decimal

from test_framework.blocktools import COIN, get_block_subsidy as framework_block_subsidy, get_miner_reward as framework_miner_reward, get_devfee as framework_devfee
from test_framework.blocktools import (
    create_block,
    create_coinbase,
)
from test_framework.messages import (
    COIN,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxOut,
    ToHex,
)
from test_framework.script import (
    CScript,
    OP_FALSE,
    OP_RETURN,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)

class CoinStatsIndexTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.supports_cli = False
        self.extra_args = [
            [],
            ["-coinstatsindex"]
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self._test_coin_stats_index()
        self._test_use_index_option()
        self._test_reorg_index()
        self._test_index_rejects_hash_serialized()

    def get_block_subsidy(self, prev_height):
        # Osmium's subsidy is height-based (src/validation.cpp GetBlockSubsidyHelper), not derived
        # from difficulty the way Dash's was, so use the framework helper for the block that
        # follows prev_height.
        return Decimal(framework_block_subsidy(prev_height + 1)) / COIN

    def block_sanity_check(self, block_info, prev_height):
        if prev_height != -1:
            block_subsidy = self.get_block_subsidy(prev_height)
        else:
            block_subsidy = 50 # see chainparams.cpp
        assert_equal(
            block_info['prevout_spent'] + block_subsidy,
            block_info['new_outputs_ex_coinbase'] + block_info['coinbase'] + block_info['unspendable']
        )

    def _test_coin_stats_index(self):
        node = self.nodes[0]
        index_node = self.nodes[1]
        # Both none and muhash options allow the usage of the index
        index_hash_options = ['none', 'muhash']

        # Generate a normal transaction and mine it
        node.generate(101)
        address = self.nodes[0].get_deterministic_priv_key().address
        node.sendtoaddress(address=address, amount=10, subtractfeefromamount=True)
        node.generate(1)

        self.sync_blocks(timeout=120)

        self.log.info("Test that gettxoutsetinfo() output is consistent with or without coinstatsindex option")
        res0 = node.gettxoutsetinfo('none')

        # The fields 'disk_size' and 'transactions' do not exist on the index
        del res0['disk_size'], res0['transactions']

        for hash_option in index_hash_options:
            res1 = index_node.gettxoutsetinfo(hash_option)
            # The fields 'block_info' and 'total_unspendable_amount' only exist on the index
            del res1['block_info'], res1['total_unspendable_amount']
            res1.pop('muhash', None)

            # Everything left should be the same
            assert_equal(res1, res0)

        self.log.info("Test that gettxoutsetinfo() can get fetch data on specific heights with index")

        # Generate a new tip
        node.generate(5)

        for hash_option in index_hash_options:
            # Fetch old stats by height
            res2 = index_node.gettxoutsetinfo(hash_option, 102)
            del res2['block_info'], res2['total_unspendable_amount']
            res2.pop('muhash', None)
            assert_equal(res0, res2)

            # Fetch old stats by hash
            res3 = index_node.gettxoutsetinfo(hash_option, res0['bestblock'])
            del res3['block_info'], res3['total_unspendable_amount']
            res3.pop('muhash', None)
            assert_equal(res0, res3)

            # It does not work without coinstatsindex
            assert_raises_rpc_error(-8, "Querying specific block heights requires coinstatsindex", node.gettxoutsetinfo, hash_option, 102)

        self.log.info("Test gettxoutsetinfo() with index and verbose flag")

        for hash_option in index_hash_options:
            # Genesis block is unspendable
            res4 = index_node.gettxoutsetinfo(hash_option, 0)
            # The unspendable genesis amount is Osmium's genesis coinbase, not Dash's 50.
            genesis_cb = sum(Decimal(str(o['value']))
                             for o in index_node.getblock(index_node.getblockhash(0), 2)['tx'][0]['vout'])
            assert_equal(res4['total_unspendable_amount'], genesis_cb)
            assert_equal(res4['block_info'], {
                'unspendable': genesis_cb,
                'prevout_spent': 0,
                'new_outputs_ex_coinbase': 0,
                'coinbase': 0,
                'unspendables': {
                    'genesis_block': genesis_cb,
                    'bip30': 0,
                    'scripts': 0,
                    'unclaimed_rewards': 0
                }
            })
            self.block_sanity_check(res4['block_info'], -1)

            # Test an older block height that included a normal tx
            res5 = index_node.gettxoutsetinfo(hash_option, 102)
            # The spent prevout is the coinbase the wallet drew on -- Osmium's height-1 premine,
            # not Dash's flat 500 -- and the block's own coinbase is its subsidy plus the fee.
            spent = Decimal(framework_miner_reward(1)) / COIN
            tx_fee = Decimal('0.00000225')
            subsidy_102 = Decimal(framework_block_subsidy(102)) / COIN
            assert_equal(res5['total_unspendable_amount'], genesis_cb)
            assert_equal(res5['block_info'], {
                'unspendable': 0,
                'prevout_spent': spent,
                'new_outputs_ex_coinbase': spent - tx_fee,
                'coinbase': subsidy_102 + tx_fee,
                'unspendables': {
                    'genesis_block': 0,
                    'bip30': 0,
                    'scripts': 0,
                    'unclaimed_rewards': 0
                }
            })
            self.block_sanity_check(res5['block_info'], 101)

        # Generate and send a normal tx with two outputs
        tx1_inputs = []
        tx1_outputs = {self.nodes[0].getnewaddress(): 21, self.nodes[0].getnewaddress(): 42}
        raw_tx1 = self.nodes[0].createrawtransaction(tx1_inputs, tx1_outputs)
        funded_tx1 = self.nodes[0].fundrawtransaction(raw_tx1)
        signed_tx1 = self.nodes[0].signrawtransactionwithwallet(funded_tx1['hex'])
        tx1_txid = self.nodes[0].sendrawtransaction(signed_tx1['hex'])

        # Find the right position of the 21 BTC output
        tx1_final = self.nodes[0].gettransaction(tx1_txid)
        for output in tx1_final['details']:
            if output['amount'] == Decimal('21.00000000') and output['category'] == 'receive':
                n = output['vout']

        # Generate and send another tx with an OP_RETURN output (which is unspendable)
        tx2 = CTransaction()
        tx2.vin.append(CTxIn(COutPoint(int(tx1_txid, 16), n), b''))
        tx2.vout.append(CTxOut(int(20.99 * COIN), CScript([OP_RETURN] + [OP_FALSE]*30)))
        tx2_hex = self.nodes[0].signrawtransactionwithwallet(ToHex(tx2))['hex']
        self.nodes[0].sendrawtransaction(tx2_hex)

        # Include both txs in a block
        self.nodes[0].generate(1)
        self.sync_all()

        for hash_option in index_hash_options:
            # Check all amounts were registered correctly
            res6 = index_node.gettxoutsetinfo(hash_option, 108)
            # As above: the spent prevouts are Osmium's premine plus the 11 moved earlier, and the
            # coinbase is this block's subsidy plus the fees, rather than Dash's flat 500.
            spent_108 = Decimal(framework_miner_reward(1)) / COIN + Decimal('11')
            fees_108 = Decimal('0.01000260')
            unspendable_108 = Decimal('20.98999999')
            subsidy_108 = Decimal(framework_block_subsidy(108)) / COIN
            assert_equal(res6['total_unspendable_amount'], genesis_cb + unspendable_108)
            assert_equal(res6['block_info'], {
                'unspendable': unspendable_108,
                'prevout_spent': spent_108,
                'new_outputs_ex_coinbase': spent_108 - unspendable_108 - fees_108,
                'coinbase': subsidy_108 + fees_108,
                'unspendables': {
                    'genesis_block': 0,
                    'bip30': 0,
                    'scripts': Decimal('20.98999999'),
                    'unclaimed_rewards': 0
                }
            })
            self.block_sanity_check(res6['block_info'], 107)

        # Create a coinbase that does not claim full subsidy and also
        # has two outputs
        # Dash's subsidy was 500 so claiming 35 + 5 left 460 unclaimed. Osmium's is a fraction of
        # a coin, so claim fractions of the real subsidy instead -- claiming 40 would simply make
        # the block invalid. create_coinbase() also adds the devfee output, which counts as claimed.
        subsidy_109 = Decimal(framework_block_subsidy(109)) / COIN
        devfee_109 = Decimal(framework_devfee(109)) / COIN
        claim_main = (subsidy_109 * Decimal('0.35')).quantize(Decimal('0.00000001'))
        claim_extra = (subsidy_109 * Decimal('0.05')).quantize(Decimal('0.00000001'))
        cb = create_coinbase(109, nValue=claim_main)
        cb.vout.append(CTxOut(int(claim_extra * COIN), CScript([OP_FALSE])))
        cb.rehash()

        # Generate a block that includes previous coinbase
        tip = self.nodes[0].getbestblockhash()
        block_time = self.nodes[0].getblock(tip)['time'] + 1
        block = create_block(int(tip, 16), cb, block_time)
        block.solve()
        self.nodes[0].submitblock(ToHex(block))
        self.sync_all()

        for hash_option in index_hash_options:
            res7 = index_node.gettxoutsetinfo(hash_option, 109)
            claimed_109 = claim_main + claim_extra + devfee_109
            unclaimed_109 = subsidy_109 - claimed_109
            assert_equal(res7['total_unspendable_amount'],
                         genesis_cb + Decimal('20.98999999') + unclaimed_109)
            assert_equal(res7['block_info'], {
                'unspendable': unclaimed_109,
                'prevout_spent': 0,
                'new_outputs_ex_coinbase': 0,
                'coinbase': claimed_109,
                'unspendables': {
                    'genesis_block': 0,
                    'bip30': 0,
                    'scripts': 0,
                    'unclaimed_rewards': unclaimed_109
                }
            })
            self.block_sanity_check(res7['block_info'], 108)

        self.log.info("Test that the index is robust across restarts")

        res8 = index_node.gettxoutsetinfo('muhash')
        self.restart_node(1, extra_args=self.extra_args[1])
        res9 = index_node.gettxoutsetinfo('muhash')
        assert_equal(res8, res9)

        index_node.generate(1)
        res10 = index_node.gettxoutsetinfo('muhash')
        assert(res8['txouts'] < res10['txouts'])

    def _test_use_index_option(self):
        self.log.info("Test use_index option for nodes running the index")

        self.connect_nodes(0, 1)
        self.nodes[0].waitforblockheight(110)
        res = self.nodes[0].gettxoutsetinfo('muhash')
        option_res = self.nodes[1].gettxoutsetinfo(hash_type='muhash', hash_or_height=None, use_index=False)
        del res['disk_size'], option_res['disk_size']
        assert_equal(res, option_res)

    def _test_reorg_index(self):
        self.log.info("Test that index can handle reorgs")

        # Generate two block, let the index catch up, then invalidate the blocks
        index_node = self.nodes[1]
        reorg_blocks = index_node.generatetoaddress(2, index_node.getnewaddress())
        reorg_block = reorg_blocks[1]
        res_invalid = index_node.gettxoutsetinfo('muhash')
        index_node.invalidateblock(reorg_blocks[0])
        assert_equal(index_node.gettxoutsetinfo('muhash')['height'], 110)

        # Add two new blocks
        block = index_node.generate(2)[1]
        res = index_node.gettxoutsetinfo(hash_type='muhash', hash_or_height=None, use_index=False)

        # Test that the result of the reorged block is not returned for its old block height
        res2 = index_node.gettxoutsetinfo(hash_type='muhash', hash_or_height=112)
        assert_equal(res["bestblock"], block)
        assert_equal(res["muhash"], res2["muhash"])
        assert(res["muhash"] != res_invalid["muhash"])

        # Test that requesting reorged out block by hash is still returning correct results
        res_invalid2 = index_node.gettxoutsetinfo(hash_type='muhash', hash_or_height=reorg_block)
        assert_equal(res_invalid2["muhash"], res_invalid["muhash"])
        assert(res["muhash"] != res_invalid2["muhash"])

        # Add another block, so we don't depend on reconsiderblock remembering which
        # blocks were touched by invalidateblock
        index_node.generate(1)
        self.sync_all()

        # Ensure that removing and re-adding blocks yields consistent results
        block = index_node.getblockhash(99)
        index_node.invalidateblock(block)
        index_node.reconsiderblock(block)
        res3 = index_node.gettxoutsetinfo(hash_type='muhash', hash_or_height=112)
        assert_equal(res2, res3)

    def _test_index_rejects_hash_serialized(self):
        self.log.info("Test that the rpc raises if the legacy hash is passed with the index")

        msg = "hash_serialized_2 hash type cannot be queried for a specific block"
        assert_raises_rpc_error(-8, msg, self.nodes[1].gettxoutsetinfo, hash_type='hash_serialized_2', hash_or_height=111)

        for use_index in {True, False, None}:
            assert_raises_rpc_error(-8, msg, self.nodes[1].gettxoutsetinfo, hash_type='hash_serialized_2', hash_or_height=111, use_index=use_index)


if __name__ == '__main__':
    CoinStatsIndexTest().main()
