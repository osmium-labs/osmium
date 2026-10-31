#!/usr/bin/env python3
# Copyright (c) 2026 The Osmium Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Wallet RPCs no other test reaches: listaddressbalances, abortrescan and the Electrum import.

listaddressbalances needs -addressindex. The Electrum import is exercised only for its refusals:
a real import needs an Electrum export to feed it, and the refusals are where a user lands.
"""

import os
from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)


class WalletAddressQueriesTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-addressindex"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        node.generatetoaddress(110, node.getnewaddress())

        self.test_listaddressbalances()
        self.test_abortrescan()
        self.test_importelectrumwallet()

    def test_listaddressbalances(self):
        node = self.nodes[0]

        self.log.info("Sending to two addresses puts both in the listing")
        small, large = node.getnewaddress(), node.getnewaddress()
        node.sendtoaddress(small, 1)
        node.sendtoaddress(large, 20)
        node.generate(1)

        balances = node.listaddressbalances()
        assert_equal(balances[small], Decimal("1"))
        assert_equal(balances[large], Decimal("20"))

        self.log.info("The minimum filters out anything smaller")
        filtered = node.listaddressbalances(5)
        assert_equal(filtered[large], Decimal("20"))
        assert small not in filtered

        self.log.info("A minimum above every balance leaves nothing")
        assert_equal(node.listaddressbalances(max(balances.values()) + 1), {})

    def test_abortrescan(self):
        node = self.nodes[0]

        self.log.info("With no rescan under way there is nothing to abort")
        assert_equal(node.abortrescan(), False)

        self.log.info("It stays false after a rescan has finished")
        node.rescanblockchain(0, 1)
        assert_equal(node.abortrescan(), False)

    def test_importelectrumwallet(self):
        node = self.nodes[0]
        tmpdir = self.options.tmpdir

        self.log.info("A file that is not there is reported")
        assert_raises_rpc_error(-8, "Cannot open Electrum wallet export file",
                                node.importelectrumwallet, os.path.join(tmpdir, "no-such-export.csv"))

        self.log.info("The extension is checked before the file is opened")
        assert_raises_rpc_error(-8, "File has no extension",
                                node.importelectrumwallet, os.path.join(tmpdir, "export"))
        assert_raises_rpc_error(-8, "File has wrong extension",
                                node.importelectrumwallet, os.path.join(tmpdir, "export.txt"))

        self.log.info("A dot in a directory name does not supply an extension")
        dotted = os.path.join(tmpdir, "v1.2")
        os.makedirs(dotted, exist_ok=True)
        assert_raises_rpc_error(-8, "File has no extension",
                                node.importelectrumwallet, os.path.join(dotted, "export"))
        assert_raises_rpc_error(-8, "Cannot open Electrum wallet export file",
                                node.importelectrumwallet, os.path.join(dotted, "export.json"))

        # Past those checks the parser skips whatever it cannot read, so a well-named file of
        # nonsense imports nothing and reports nothing.
        self.log.info("A well-named file of nonsense is accepted and imports nothing")
        junk = os.path.join(tmpdir, "not-an-export.csv")
        with open(junk, "w", encoding="utf-8") as f:
            f.write("this is not an electrum export\n")
        before = node.getwalletinfo()["keypoolsize"]
        node.importelectrumwallet(junk)
        assert_equal(node.getwalletinfo()["keypoolsize"], before)


if __name__ == '__main__':
    WalletAddressQueriesTest().main()
