#!/usr/bin/env python3
# Copyright (c) 2026 The Osmium Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Block query RPCs inherited from Dash that no other test reaches.

getblockheaders, getmerkleblocks and getspecialtxes.
"""

from test_framework.test_framework import BitcoinTestFramework, MASTERNODE_COLLATERAL
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
    p2p_port,
)

# A serialised CBloomFilter: eight bytes of vData with every bit set, one hash function, no
# tweak, no flags. Every query against it matches, so every block comes back.
MATCH_ALL_FILTER = "08" + "ff" * 8 + "01000000" + "00000000" + "00"

# The filter from the RPC's own help, which matches nothing on this chain.
HELP_FILTER = ("2303028005802040100040000008008400048141010000f84004208000800250"
               "04000004130000000000000001")

# Special transaction types (src/primitives/transaction.h).
TRANSACTION_NORMAL = 0
TRANSACTION_PROVIDER_REGISTER = 1
TRANSACTION_PROVIDER_UPDATE_SERVICE = 2
TRANSACTION_COINBASE = 5

# Regtest DIP0003Height: from here every coinbase is a CbTx.
DIP3_HEIGHT = 432


class BlockchainQueriesTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-txindex"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        node.generatetoaddress(DIP3_HEIGHT + 10, node.getnewaddress())

        self.test_getblockheaders()
        self.test_getmerkleblocks()
        self.test_getspecialtxes()

    def test_getblockheaders(self):
        node = self.nodes[0]
        start = node.getblockhash(1)

        self.log.info("getblockheaders walks forward from the hash it is given")
        headers = node.getblockheaders(start, 3)
        assert_equal(len(headers), 3)
        assert_equal(headers[0]["hash"], start)
        assert_equal(headers[0]["height"], 1)

        self.log.info("Each header names the one before it")
        for earlier, later in zip(headers, headers[1:]):
            assert_equal(later["previousblockhash"], earlier["hash"])
            assert_equal(later["height"], earlier["height"] + 1)

        self.log.info("It agrees with getblockheader, which also decodes the auxpow")
        single = node.getblockheader(start)
        assert "auxpow" in single
        del single["auxpow"]
        assert_equal(headers[0], single)

        self.log.info("Unverbose returns the serialised headers")
        raw = node.getblockheaders(start, 2, False)
        assert_equal(len(raw), 2)
        assert_equal(raw[0], node.getblockheader(start, False))
        assert_equal(raw[1], node.getblockheader(headers[1]["hash"], False))

        self.log.info("The count is capped by how far the chain reaches")
        assert_equal(len(node.getblockheaders(node.getbestblockhash(), 50)), 1)

        assert_raises_rpc_error(-5, "Block not found", node.getblockheaders, "00" * 32, 1)

    def test_getmerkleblocks(self):
        node = self.nodes[0]
        start = node.getblockhash(1)

        self.log.info("A filter that matches everything returns every block asked for")
        blocks = node.getmerkleblocks(MATCH_ALL_FILTER, start, 3)
        assert_equal(len(blocks), 3)

        self.log.info("Each one opens with the header of its block")
        for merkleblock, header in zip(blocks, node.getblockheaders(start, 3, False)):
            assert merkleblock.startswith(header[:160])

        self.log.info("A filter that matches nothing returns nothing")
        assert_equal(node.getmerkleblocks(HELP_FILTER, start, 3), [])

        assert_raises_rpc_error(-5, "Block not found", node.getmerkleblocks, MATCH_ALL_FILTER, "00" * 32, 1)

    def test_getspecialtxes(self):
        node = self.nodes[0]

        self.log.info("Before DIP3 a block holds no special transactions")
        assert_equal(node.getspecialtxes(node.getblockhash(DIP3_HEIGHT - 1)), [])

        self.log.info("A block's special transactions are all of its non-normal ones, in block order")
        funds = node.getnewaddress()
        node.sendtoaddress(funds, MASTERNODE_COLLATERAL + 1)
        owner = node.getnewaddress()  # also the voting key, which must equal it here
        protx = node.protx('register_fund', funds, '127.0.0.1:%d' % p2p_port(1), owner,
                           node.bls('generate')['public'], owner, 0, node.getnewaddress(), funds)
        block_hash = node.generate(1)[0]
        txs = node.getblock(block_hash, 2)["tx"]
        cbtx = txs[0]["txid"]
        # Besides the CbTx and the ProRegTx, the block carries quorum commitments (type 6) and
        # the wallet's ordinary send (type 0), which must not be listed.
        assert TRANSACTION_NORMAL in [tx["type"] for tx in txs]
        special = [tx["txid"] for tx in txs if tx["type"] != TRANSACTION_NORMAL]
        assert_equal(node.getspecialtxes(block_hash), special)

        self.log.info("Filtering by type picks out one kind, and an absent type finds nothing")
        assert_equal(node.getspecialtxes(block_hash, TRANSACTION_COINBASE), [cbtx])
        assert_equal(node.getspecialtxes(block_hash, TRANSACTION_PROVIDER_REGISTER), [protx])
        assert_equal(node.getspecialtxes(block_hash, TRANSACTION_PROVIDER_UPDATE_SERVICE), [])

        self.log.info("Verbosity climbs from txid to decoded transaction")
        decoded = node.getspecialtxes(block_hash, TRANSACTION_PROVIDER_REGISTER, 10, 0, 2)
        assert_equal(decoded[0]["txid"], protx)
        assert_equal(decoded[0]["type"], TRANSACTION_PROVIDER_REGISTER)
        assert_greater_than(decoded[0]["size"], 0)

        self.log.info("count and skip page through the list")
        assert_equal(node.getspecialtxes(block_hash, -1, 2), special[:2])
        assert_equal(node.getspecialtxes(block_hash, -1, 2, 1), special[1:3])
        assert_equal(node.getspecialtxes(block_hash, TRANSACTION_PROVIDER_REGISTER, 10, 1), [])

        assert_raises_rpc_error(-5, "Block not found", node.getspecialtxes, "00" * 32)


if __name__ == '__main__':
    BlockchainQueriesTest().main()
