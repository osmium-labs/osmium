#!/usr/bin/env python3
# Copyright (c) 2019-2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the generation of UTXO snapshots using `dumptxoutset`.
"""

from test_framework.blocktools import get_devfee, COINBASE_MATURITY
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

import hashlib
from pathlib import Path


class DumptxoutsetTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def run_test(self):
        """Test a trivial usage of the dumptxoutset RPC command."""
        node = self.nodes[0]
        mocktime = node.getblockheader(node.getblockhash(0))['time'] + 1
        node.setmocktime(mocktime)
        node.generate(COINBASE_MATURITY)

        FILENAME = 'txoutset.dat'
        out = node.dumptxoutset(FILENAME)
        expected_path = Path(node.datadir) / self.chain / FILENAME

        assert expected_path.is_file()

        # Osmium's coinbase carries a devfee output above nDevfeePayment.getStartBlock(), so the
        # UTXO set holds more entries than there are blocks -- Dash's had exactly one each.
        expected_coins = sum(2 if get_devfee(h) > 0 else 1 for h in range(1, 101))
        assert_equal(out['coins_written'], expected_coins)
        assert_equal(out['base_height'], 100)
        assert_equal(out['path'], str(expected_path))
        # Blockhash should be deterministic based on mocked time.
        assert_equal(
            out['base_hash'],
            # Regenerated for Osmium: the UTXO set differs from Dash's (different subsidy, plus a
            # devfee output per block above the start height), so the snapshot hash differs too.
            '7dedc3f7845b080f9389a6ebcb9b7c54f319f52ecdfc4c815340d8a593762de2')

        with open(str(expected_path), 'rb') as f:
            digest = hashlib.sha256(f.read()).hexdigest()
            # UTXO snapshot hash should be deterministic based on mocked time.
            assert_equal(
                # Regenerated for Osmium, same reason as the base_hash above.
                digest, '7af3ef866420082ae478bd22dcc8929d2c6f25b1c403d33e9435acc4439edec6')

        # Specifying a path to an existing file will fail.
        assert_raises_rpc_error(
            -8, '{} already exists'.format(FILENAME),  node.dumptxoutset, FILENAME)

if __name__ == '__main__':
    DumptxoutsetTest().main()
