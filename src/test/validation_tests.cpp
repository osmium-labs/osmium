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
    BOOST_CHECK_EQUAL(out110.hash_serialized.ToString(), "927d43dbac32e62d607da7e2f1c12274cd30c341eae38a7e6019083c655c7145"); // regenerated for Maximus (devfee UTXOs)
    BOOST_CHECK_EQUAL(out110.nChainTx, (unsigned int)170);

    const auto out210 = *ExpectedAssumeutxo(210, *params);
    BOOST_CHECK_EQUAL(out210.hash_serialized.ToString(), "d4c97d32882583b057efc3dce673e44204851435e6ffcef20346e69cddc7c91e");
    BOOST_CHECK_EQUAL(out210.nChainTx, (unsigned int)210);
}

BOOST_AUTO_TEST_SUITE_END()
