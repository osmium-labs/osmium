#!/usr/bin/env bash
# Copyright (c) 2018-2023 The Dash Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
# use testnet settings,  if you need mainnet,  use ~/.osmiumcore/osmiumd.pid file instead
export LC_ALL=C

osmium_pid=$(<~/.osmiumcore/testnet3/osmiumd.pid)
sudo gdb -batch -ex "source debug.gdb" osmiumd ${osmium_pid}
