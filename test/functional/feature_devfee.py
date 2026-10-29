#!/usr/bin/env python3
# Copyright (c) 2026 The Osmium Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the coinbase devfee rule.

Every coinbase must pay at least subsidy / 19 to the devfee address (CheckBlock in
src/validation.cpp, DevfeePayment::IsBlockPayeeValid). On regtest the devfee start block is 0
(mainnet: 1).

The miner and the consensus rule disagree by one block about where the devfee begins:
FillDevfeePayment() pays it from height start + 1, while CheckBlock() asks for the amount at
nHeight - 1 and so only requires it from height start + 2. The test pins both.
"""

from test_framework.blocktools import (
    create_block,
    create_coinbase,
    devfee_script,
    get_block_subsidy,
    get_devfee,
)
from test_framework.messages import CTxOut
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

DEVFEE_START_BLOCK = 0
DEVFEE_ENFORCED_FROM = DEVFEE_START_BLOCK + 2


class DevfeeTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def build_block(self, *, devfee=None, devfee_script_pubkey=None):
        """A block on the tip whose coinbase pays `devfee` (None: no devfee output at all)."""
        node = self.nodes[0]
        tip = node.getblock(node.getbestblockhash())
        height = tip["height"] + 1
        coinbase = create_coinbase(height)
        coinbase.vout = coinbase.vout[:1]
        if devfee is not None:
            coinbase.vout[0].nValue = get_block_subsidy(height) - devfee
            coinbase.vout.append(CTxOut(devfee, devfee_script_pubkey or devfee_script()))
        coinbase.rehash()
        block = create_block(int(tip["hash"], 16), coinbase, tip["time"] + 1)
        block.solve()
        return block

    def submit(self, block):
        return self.nodes[0].submitblock(block.serialize().hex())

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Below the enforced height a coinbase needs no devfee output")
        for height in range(1, DEVFEE_ENFORCED_FROM):
            assert_equal(self.submit(self.build_block()), None)
            assert_equal(node.getblockcount(), height)

        height = DEVFEE_ENFORCED_FROM
        required = get_devfee(height)
        assert required > 0

        self.log.info("A coinbase without the devfee output is rejected")
        assert_equal(self.submit(self.build_block()), "bad-cb-devfee-payment-not-found")

        self.log.info("Paying one duff too little is rejected")
        assert_equal(self.submit(self.build_block(devfee=required - 1)), "bad-cb-devfee-payment-not-found")

        self.log.info("The right amount to a different script is rejected")
        assert_equal(self.submit(self.build_block(devfee=required, devfee_script_pubkey=CScript([OP_TRUE]))),
                     "bad-cb-devfee-payment-not-found")

        self.log.info("Paying more than required is accepted")
        assert_equal(self.submit(self.build_block(devfee=required + 1)), None)
        assert_equal(node.getblockcount(), height)

        self.log.info("Paying exactly the required amount is accepted")
        assert_equal(self.submit(self.build_block(devfee=get_devfee(height + 1))), None)
        assert_equal(node.getblockcount(), height + 1)

        self.log.info("The node's own blocks pay exactly the devfee, from height start + 1")
        node.invalidateblock(node.getblockhash(1))
        assert_equal(node.getblockcount(), 0)
        node.generatetoaddress(10, node.get_deterministic_priv_key().address)
        devfee_hex = devfee_script().hex()
        for h in range(1, node.getblockcount() + 1):
            coinbase = node.getblock(node.getblockhash(h), 2)["tx"][0]
            paid = [out for out in coinbase["vout"] if out["scriptPubKey"]["hex"] == devfee_hex]
            if h <= DEVFEE_START_BLOCK:
                assert_equal(paid, [])
            else:
                assert_equal(len(paid), 1)
                assert_equal(int(paid[0]["valueSat"]), get_devfee(h))


if __name__ == '__main__':
    DevfeeTest().main()
