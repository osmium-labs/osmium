#!/usr/bin/env python3
# Copyright (c) 2026 The Osmium Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

'''
feature_llmq_tiers.py

Mainnet and testnet resize their LLMQs in place as the registered masternode count crosses tier
thresholds (CChainParams::UpdateLLMQParams). -llmqtesttiers=5:4:3 turns the same mechanism on for
LLMQ_TEST: 3 members (threshold 2) below 5 masternodes, 4 members (threshold 3) from 5.

Checks that quorums form at each tier, that a quorum keeps signing with its own threshold after the
tier changes, and that no honest masternode is punished across the changes.
'''

from test_framework.test_framework import OsmiumTestFramework
from test_framework.util import assert_equal

LLMQ_TEST = 100
TIER_ARG = "-llmqtesttiers=5:4:3"


class LLMQTiersTest(OsmiumTestFramework):
    def set_test_params(self):
        self.set_osmium_test_params(5, 4, fast_dip3_enforcement=True, extra_args=[[TIER_ARG]] * 5)

    def run_test(self):
        self.enable_dkg_only()

        self.log.info("4 masternodes: LLMQ_TEST has 3 members")
        q_small = self.mine_quorum()
        assert_equal(len(self.quorum_members(q_small)), 3)

        self.log.info("A fifth masternode moves LLMQ_TEST to 4 members, threshold 3")
        self.dynamically_add_masternode(wait_for_islock=False)
        q_large = self.mine_quorum(expected_members=4, expected_contributions=4, expected_commitments=4)
        assert_equal(len(self.quorum_members(q_large)), 4)

        self.log.info("Each quorum signs with its own threshold after the tier change")
        self.sign_with(q_small, "01")
        self.sign_with(q_large, "02")

        self.log.info("Removing a masternode between DKGs moves LLMQ_TEST back to 3 members")
        self.remove_masternode(len(self.mninfo) - 1)
        q_after = self.mine_quorum()
        assert_equal(len(self.quorum_members(q_after)), 3)
        self.assert_no_pose_penalty()

    def enable_dkg_only(self):
        # InstantSend and ChainLocks are not under test, and without them the miner takes
        # transactions without waiting for a lock.
        self.nodes[0].sporkupdate("SPORK_2_INSTANTSEND_ENABLED", 4070908800)
        self.nodes[0].sporkupdate("SPORK_19_CHAINLOCKS_ENABLED", 4070908800)
        self.nodes[0].sporkupdate("SPORK_17_QUORUM_DKG_ENABLED", 0)
        self.wait_for_sporks_same()

    def quorum_members(self, quorum_hash):
        return self.nodes[0].quorum("info", LLMQ_TEST, quorum_hash)["members"]

    def sign_with(self, quorum_hash, n):
        request_id = n.rjust(64, "0")
        msg_hash = ("f" + n).rjust(64, "0")
        for mn in self.get_quorum_masternodes(quorum_hash, LLMQ_TEST):
            assert mn.node.quorum("sign", LLMQ_TEST, request_id, msg_hash, quorum_hash)
        self.wait_until(lambda: all(mn.node.quorum("hasrecsig", LLMQ_TEST, request_id, msg_hash) for mn in self.mninfo))
        rec_sig = self.mninfo[0].node.quorum("getrecsig", LLMQ_TEST, request_id, msg_hash)
        assert_equal(rec_sig["quorumHash"], quorum_hash)

    def assert_no_pose_penalty(self):
        for mn in self.mninfo:
            state = self.nodes[0].protx("info", mn.proTxHash)["state"]
            assert_equal((state["PoSePenalty"], state["PoSeBanHeight"]), (0, -1))


if __name__ == '__main__':
    LLMQTiersTest().main()
