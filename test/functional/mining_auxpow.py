#!/usr/bin/env python3
# Copyright (c) 2026 The Osmium Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test merge mining end to end: createauxblock/submitauxblock and getauxblock.

Each round trip asks the node for a block, builds a parent chain's coinbase that commits to the
block hash behind the merged-mining header, mines the parent header to the block's target, and
hands the resulting auxpow back (src/auxpow.cpp CAuxPow::check).
"""

import struct

from test_framework.blocktools import AUXPOW_CHAIN_ID
from test_framework.messages import (
    CAuxPow,
    CBlockHeader,
    COutPoint,
    CTransaction,
    CTxIn,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

MERGED_MINING_HEADER = b"\xfa\xbe" + b"mm"


def target_from_bits(bits_hex):
    bits = int(bits_hex, 16)
    return (bits & 0x00ffffff) << (8 * ((bits >> 24) - 3))


def build_auxpow(aux_block, *, parent_chain_id=0):
    """An auxpow for `aux_block` (a createauxblock/getauxblock result) from a one-transaction
    parent block whose version carries `parent_chain_id`."""
    coinbase = CTransaction()
    coinbase.nVersion = 1
    # Merged-mining header, the block hash in display order, chain merkle tree size 1, nonce 0.
    script = MERGED_MINING_HEADER + bytes.fromhex(aux_block["hash"]) + struct.pack("<II", 1, 0)
    coinbase.vin.append(CTxIn(COutPoint(0, 0xffffffff), script, 0xffffffff))
    coinbase.calc_sha256()

    parent = CBlockHeader()
    parent.nVersion = (parent_chain_id << 16) | 1
    parent.hashPrevBlock = 0
    parent.hashMerkleRoot = coinbase.sha256
    parent.nTime = 0
    parent.nBits = int(aux_block["bits"], 16)
    parent.nNonce = 0
    target = target_from_bits(aux_block["bits"])
    parent.rehash()
    while parent.sha256 > target:
        parent.nNonce += 1
        parent.rehash()

    auxpow = CAuxPow()
    auxpow.coinbaseTx = coinbase
    auxpow.parentBlock = parent
    return auxpow.serialize().hex()


class AuxpowMiningTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        node.generate(1)
        address = node.getnewaddress()

        self.log.info("createauxblock describes a block on the current tip")
        aux = node.createauxblock(address)
        assert_equal(aux["chainid"], AUXPOW_CHAIN_ID)
        assert_equal(aux["previousblockhash"], node.getbestblockhash())
        assert_equal(aux["height"], node.getblockcount() + 1)

        self.log.info("submitauxblock with a valid auxpow extends the chain")
        assert_equal(node.submitauxblock(aux["hash"], build_auxpow(aux)), True)
        assert_equal(node.getbestblockhash(), aux["hash"])
        block = node.getblock(aux["hash"])
        assert_equal(block["height"], aux["height"])
        coinbase = node.getblock(aux["hash"], 2)["tx"][0]
        assert address in [out["scriptPubKey"].get("addresses", [None])[0] for out in coinbase["vout"]]

        self.log.info("getauxblock does the same with the wallet's own payout address")
        aux = node.getauxblock()
        assert_equal(aux["previousblockhash"], node.getbestblockhash())
        assert_equal(node.getauxblock(aux["hash"], build_auxpow(aux)), True)
        assert_equal(node.getbestblockhash(), aux["hash"])

        self.log.info("A block built on a tip that has since moved on is kept as a side branch")
        stale = node.createauxblock(address)
        tip = node.generate(1)[0]
        assert_equal(node.submitauxblock(stale["hash"], build_auxpow(stale)), True)
        assert_equal(node.getbestblockhash(), tip)
        assert {"height": stale["height"], "hash": stale["hash"], "branchlen": 1, "status": "valid-headers"} in \
            [{k: t[k] for k in ("height", "hash", "branchlen", "status")} for t in node.getchaintips()]

        # The rejection is right but its reason is not: CheckProofOfWork() collapses the chain-ID
        # failure into high-hash, which points at difficulty. Pinned so that a clearer message is
        # a deliberate change.
        self.log.info("A parent block carrying our own chain ID is rejected, reported as high-hash")
        aux = node.createauxblock(address)
        tip = node.getbestblockhash()
        with node.assert_debug_log(["Aux POW parent has our chain ID", "high-hash"]):
            assert_equal(node.submitauxblock(aux["hash"], build_auxpow(aux, parent_chain_id=AUXPOW_CHAIN_ID)), False)
        assert_equal(node.getbestblockhash(), tip)


if __name__ == '__main__':
    AuxpowMiningTest().main()
