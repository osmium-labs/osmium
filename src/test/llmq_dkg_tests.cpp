// Copyright (c) 2022-2023 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/debug.h>
#include <llmq/dkgsession.h>
#include <llmq/params.h>
#include <uint256.h>
#include <util/irange.h>
#include <util/underlying.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(llmq_dkg_tests)

BOOST_AUTO_TEST_CASE(llmq_dkgerror)
{
    using namespace llmq;
    for (auto i : irange::range(ToUnderlying(llmq::DKGError::type::_COUNT))) {
        BOOST_ASSERT(GetSimulatedErrorRate(llmq::DKGError::type(i)) == 0.0);
        SetSimulatedDKGErrorRate(llmq::DKGError::type(i), 1.0);
        BOOST_ASSERT(GetSimulatedErrorRate(llmq::DKGError::type(i)) == 1.0);
    }
    BOOST_ASSERT(GetSimulatedErrorRate(llmq::DKGError::type::_COUNT) == 0.0);
    SetSimulatedDKGErrorRate(llmq::DKGError::type::_COUNT, 1.0);
    BOOST_ASSERT(GetSimulatedErrorRate(llmq::DKGError::type::_COUNT) == 0.0);
}

//! An out-of-range member index must be skipped, not thrown on.
//!
//! The debug session's member vector is sized from llmqParams.size, while memberIdx
//! indexes CDKGSession::members, sized from the masternode list GetAllQuorumMembers
//! returned. UpdateLLMQParams() rewrites consensus.llmqs in place as the masternode
//! count crosses tier thresholds, so the two can disagree. The previous .at() threw
//! std::out_of_range out of the DKG phase thread; util::TraceThread rethrows, which
//! terminates the node -- over bookkeeping only the dkgstatus RPC ever reads.
BOOST_AUTO_TEST_CASE(llmq_dkgdebug_member_index_out_of_range)
{
    using namespace llmq;

    CDKGDebugManager dkgDebugManager;
    const auto& params = Consensus::llmq_test; // .size == 3
    dkgDebugManager.InitLocalSessionStatus(params, /*quorumIndex=*/0, uint256::ONE, /*quorumHeight=*/1);

    bool in_range_called = false;
    dkgDebugManager.UpdateLocalMemberStatus(params.type, 0, 1, [&](CDKGDebugMemberStatus&) {
        in_range_called = true;
        return true;
    });
    BOOST_CHECK(in_range_called);

    bool out_of_range_called = false;
    BOOST_CHECK_NO_THROW(dkgDebugManager.UpdateLocalMemberStatus(
        params.type, 0, static_cast<size_t>(params.size) + 2, [&](CDKGDebugMemberStatus&) {
            out_of_range_called = true;
            return true;
        }));
    BOOST_CHECK(!out_of_range_called);
}

BOOST_AUTO_TEST_SUITE_END()
