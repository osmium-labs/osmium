// Copyright (c) 2021-2023 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/setup_common.h>

#include <chainparams.h>
#include <consensus/validation.h>
#include <deploymentstatus.h>
#include <evo/evodb.h>
#include <governance/governance.h>
#include <llmq/blockprocessor.h>
#include <llmq/chainlocks.h>
#include <llmq/context.h>
#include <llmq/instantsend.h>
#include <miner.h>
#include <script/interpreter.h>
#include <spork.h>
#include <validation.h>
#include <versionbits.h>

#include <boost/test/unit_test.hpp>

const auto deployment_id = Consensus::DEPLOYMENT_TESTDUMMY;
constexpr int window{100};

/**
 * BIP9 signalling cannot happen on Osmium, and this suite pins that rather than the dynamic
 * threshold machinery it inherited, which no chain of ours can ever drive.
 *
 * Osmium is always-auxpow, so a block's chain ID lives in the top 16 bits of nVersion
 * (CPureBlockHeader::GetChainId() is nVersion / 2^16) and validation rejects any block whose chain
 * ID is not nAuxpowChainId. VersionBitsConditionChecker::Condition() meanwhile requires the top
 * three bits of nVersion to be 001. Those two demands are mutually exclusive: every version that
 * signals carries chain ID 0x2000, and every version we accept has its top three bits clear. That
 * is what the FIXME in miner.cpp ("Active version bits after the always-auxpow fork!") is about --
 * ComputeBlockVersion() is not merely commented out, re-enabling it would emit blocks the network
 * rejects.
 *
 * Deployments therefore have to be driven deliberately (ALWAYS_ACTIVE) or through EHF, never by
 * miner signalling. If version bits are ever made to work, these tests fail and force this file to
 * be reconsidered instead of quietly passing.
 */
struct TestChainDATSetup : public TestChainSetup
{
    TestChainDATSetup() : TestChainSetup(window - 2, {"-vbparams=testdummy:0:999999999999:100:80:60:5:0"}) {}
};

/** Same deployment, but switched on deliberately rather than by signalling. */
struct TestChainAlwaysActiveSetup : public TestChainSetup
{
    TestChainAlwaysActiveSetup() : TestChainSetup(window - 2, {"-vbparams=testdummy:-1:999999999999:100:80:60:5:0"}) {}
};

BOOST_AUTO_TEST_SUITE(dynamic_activation_thresholds_tests)

BOOST_FIXTURE_TEST_CASE(miner_does_not_signal, TestChainDATSetup)
{
    const auto& consensus_params = Params().GetConsensus();
    CScript coinbasePubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

    LOCK(cs_main);
    const auto pblocktemplate = BlockAssembler(*m_node.sporkman, *m_node.govman, *m_node.llmq_ctx, *m_node.evodb, ::ChainstateActive(), *m_node.mempool, Params()).CreateNewBlock(coinbasePubKey);
    const int32_t nVersion = pblocktemplate->block.nVersion;

    // The assembler stamps the auxpow chain ID, which is what makes the block acceptable...
    BOOST_CHECK_EQUAL(pblocktemplate->block.GetChainId(), consensus_params.nAuxpowChainId);
    // ...and it is exactly what stops the block from ever satisfying BIP9's top-bits condition.
    BOOST_CHECK_NE(nVersion & VERSIONBITS_TOP_MASK, VERSIONBITS_TOP_BITS);

    const uint32_t bitmask = ((uint32_t)1) << consensus_params.vDeployments[deployment_id].bit;
    BOOST_CHECK_EQUAL(nVersion & bitmask, 0);
}

BOOST_FIXTURE_TEST_CASE(signalling_version_is_rejected, TestChainDATSetup)
{
    const auto& consensus_params = Params().GetConsensus();
    const int32_t signalling_version = VERSIONBITS_TOP_BITS | (((int32_t)1) << consensus_params.vDeployments[deployment_id].bit);

    // Control: the assembler's own version is accepted, so a failure below is about the version and
    // not about anything else in the block.
    const int height_before = WITH_LOCK(cs_main, return ::ChainActive().Height());
    CreateAndProcessBlock({}, coinbaseKey);
    BOOST_CHECK_EQUAL(WITH_LOCK(cs_main, return ::ChainActive().Height()), height_before + 1);

    // Positive control for the override itself: an explicit version that keeps our chain ID is
    // accepted, so the rejection below is about the version's value and not about -blockversion
    // being ignored or malformed.
    const int32_t valid_version = (consensus_params.nAuxpowChainId << 16) | 4;
    gArgs.ForceSetArg("-blockversion", ToString(valid_version));
    CreateAndProcessBlock({}, coinbaseKey);
    gArgs.ForceRemoveArg("blockversion");
    BOOST_CHECK_EQUAL(WITH_LOCK(cs_main, return ::ChainActive().Height()), height_before + 2);

    // A version that would signal carries chain ID 0x2000 instead of ours, and is refused.
    gArgs.ForceSetArg("-blockversion", ToString(signalling_version));
    CreateAndProcessBlock({}, coinbaseKey);
    gArgs.ForceRemoveArg("blockversion");
    BOOST_CHECK_EQUAL(WITH_LOCK(cs_main, return ::ChainActive().Height()), height_before + 2);

    // And the chain is still usable afterwards: the rejection was of that block, not of the chain.
    CreateAndProcessBlock({}, coinbaseKey);
    BOOST_CHECK_EQUAL(WITH_LOCK(cs_main, return ::ChainActive().Height()), height_before + 3);
}

BOOST_FIXTURE_TEST_CASE(deployment_never_locks_in_by_mining, TestChainDATSetup)
{
    const auto& consensus_params = Params().GetConsensus();

    // Mine several whole windows. Upstream this would lock in; here no block can ever signal, so
    // the deployment starts and then stays there however long we mine.
    for (int i = 0; i < window * 3; ++i) {
        CreateAndProcessBlock({}, coinbaseKey);
    }

    LOCK(cs_main);
    BOOST_CHECK_EQUAL(g_versionbitscache.State(::ChainActive().Tip(), consensus_params, deployment_id), ThresholdState::STARTED);
    BOOST_CHECK_EQUAL(g_versionbitscache.Statistics(::ChainActive().Tip(), consensus_params, deployment_id).count, 0);
}

BOOST_FIXTURE_TEST_CASE(always_active_deployment_activates, TestChainAlwaysActiveSetup)
{
    // The escape hatch: a deployment switched on deliberately is ACTIVE without any signalling,
    // which is how anything has to be activated on this chain.
    LOCK(cs_main);
    BOOST_CHECK_EQUAL(g_versionbitscache.State(::ChainActive().Tip(), Params().GetConsensus(), deployment_id), ThresholdState::ACTIVE);
}

BOOST_AUTO_TEST_SUITE_END()
