// Copyright (c) 2014-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <net.h>
#include <uint256.h>
#include <validation.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(validation_tests, TestingSetup)

//! Test retrieval of valid assumeutxo values.
BOOST_AUTO_TEST_CASE(test_assumeutxo)
{
    const auto params = CreateChainParams(*m_node.args, CBaseChainParams::REGTEST);

    // These heights don't have assumeutxo configurations associated, per the contents
    // of chainparams.cpp.
    std::vector<int> bad_heights{0, 100, 111, 115, 209, 211};

    for (auto empty : bad_heights) {
        const auto out = ExpectedAssumeutxo(empty, *params);
        BOOST_CHECK(!out);
    }

    const auto out110 = *ExpectedAssumeutxo(110, *params);
    // Pins the regtest snapshot regenerated for Osmium in chainparams.cpp; the inherited Dash hash
    // was left behind here when that table was corrected.
    BOOST_CHECK_EQUAL(out110.hash_serialized.ToString(), "5e6c5467274e3d54ab08b0a474008edb452379142234703537a70d8351ae8c68");
    BOOST_CHECK_EQUAL(out110.nChainTx, (unsigned int)110);

    const auto out210 = *ExpectedAssumeutxo(210, *params);
    BOOST_CHECK_EQUAL(out210.hash_serialized.ToString(), "b8d417e2b5b2fb439f645a7b2d74f35989f4fb45fda7dde1e7723020f44af122");
    BOOST_CHECK_EQUAL(out210.nChainTx, (unsigned int)210);
}

BOOST_AUTO_TEST_SUITE_END()
