// Copyright (c) 2014-2023 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <validation.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(subsidy_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(block_subsidy_test)
{
    // Maximus emission schedule (see whitepaper):
    //   block 1 premine: 6000 MAXI
    //   regular reward: 0.5 MAXI, reduced by 0.8% every 36000 blocks (~monthly)
    //   15% superblock allocation for nPrevHeight > 18000
    //   supply cap trajectory: ~2.25M MAXI
    // Expected values computed by replicating GetBlockSubsidyHelper's integer math.
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    const auto& consensus = chainParams->GetConsensus();

    struct Probe { int nPrevHeight; CAmount miner; CAmount superblock; };
    const Probe probes[] = {
        {       0, CAmount{600000000000}, CAmount{0}},        // premine
        {       1, CAmount{50000000},     CAmount{0}},        // initial 0.5
        {   17999, CAmount{50000000},     CAmount{0}},        // before superblock start
        {   18000, CAmount{50000000},     CAmount{0}},        // boundary: strict >, still no cut
        {   18001, CAmount{42500000},     CAmount{7500000}},  // 15% superblock cut begins
        {   35999, CAmount{42500000},     CAmount{7500000}},  // before 1st reduction
        {   36000, CAmount{42160000},     CAmount{7440000}},  // 1st monthly 0.8% reduction
        {   72000, CAmount{41822720},     CAmount{7380480}},  // 2nd reduction
        {  360000, CAmount{39219824},     CAmount{6921144}},  // ~10 months in
        { 1800000, CAmount{28442797},     CAmount{5019315}},  // ~50 months in
    };

    for (const auto& p : probes) {
        BOOST_CHECK_EQUAL(GetBlockSubsidyInner(0, p.nPrevHeight, consensus, /*fV20Active=*/false), p.miner);
        BOOST_CHECK_EQUAL(GetSuperblockSubsidyInner(0, p.nPrevHeight, consensus, /*fV20Active=*/false), p.superblock);
    }
}

BOOST_AUTO_TEST_SUITE_END()
