// Copyright (c) 2014-2023 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <validation.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(subsidy_tests, TestingSetup)

/*
 * Osmium's emission (GetBlockSubsidyHelper in validation.cpp), by nPrevHeight:
 *
 *   height 0        8000 OSMI premine
 *   1 .. 500        0.1 OSMI
 *   501 ..          1 OSMI
 *
 * reduced by 1/reductionRatio at every multiple of nSubsidyHalvingInterval (172,800), and
 * split 95/5 between miner and superblock once nPrevHeight > nSuperblockStartBlock (500).
 *
 * reductionRatio is `1210000 / 172800` -- INTEGER division, so exactly 7.0, i.e. a 14.2857%
 * decline per interval, matching the "~14.3%" in the source comment. The probes below are
 * computed from that rule independently of the implementation, so a change to either the
 * tiers or the ratio fails this test rather than being silently absorbed.
 */
BOOST_AUTO_TEST_CASE(block_subsidy_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    const auto& consensus = chainParams->GetConsensus();

    BOOST_CHECK_EQUAL(consensus.nSubsidyHalvingInterval, 172800);
    BOOST_CHECK_EQUAL(consensus.nSuperblockStartBlock, 500);

    struct Probe { int nPrevHeight; CAmount miner; CAmount superblock; };
    const Probe probes[] = {
        {        0, CAmount{800000000000}, CAmount{         0}},  // premine, no superblock cut
        {        1, CAmount{    10000000}, CAmount{         0}},  // 0.1 tier
        {      500, CAmount{    10000000}, CAmount{         0}},  // last block of the 0.1 tier
        {      501, CAmount{    95000000}, CAmount{   5000000}},  // 1 OSMI, superblock cut begins
        {   172799, CAmount{    95000000}, CAmount{   5000000}},  // last block before 1st reduction
        {   172800, CAmount{    81428571}, CAmount{   4285714}},  // 1st reduction (-1/7)
        {   172801, CAmount{    81428571}, CAmount{   4285714}},  // still the 1st reduction
        {   345600, CAmount{    69795918}, CAmount{   3673469}},  // 2nd reduction
        {   864000, CAmount{    43953114}, CAmount{   2313321}},  // 5th reduction
        {  1728000, CAmount{    20335538}, CAmount{   1070291}},  // 10th reduction
    };

    for (const auto& p : probes) {
        BOOST_CHECK_EQUAL(GetBlockSubsidyInner(0, p.nPrevHeight, consensus, /*fV20Active=*/false), p.miner);
        BOOST_CHECK_EQUAL(GetSuperblockSubsidyInner(0, p.nPrevHeight, consensus, /*fV20Active=*/false), p.superblock);
    }
}

/*
 * Unlike Dash, Osmium's subsidy depends only on height: GetBlockSubsidyHelper reads neither
 * nPrevBits nor fV20Active. Dash scaled the reward by the previous block's difficulty and
 * changed behaviour at v20; both were dropped here. Pin that, so reintroducing a
 * difficulty- or deployment-dependent reward cannot pass unnoticed.
 */
BOOST_AUTO_TEST_CASE(block_subsidy_ignores_bits_and_v20)
{
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    const auto& consensus = chainParams->GetConsensus();

    for (const int height : {0, 1, 500, 501, 172800, 345600}) {
        const CAmount baseline = GetBlockSubsidyInner(0, height, consensus, /*fV20Active=*/false);
        for (const uint32_t bits : {0x1c4a47c4u, 0x1b1441deu, 0x207fffffu, 0u}) {
            BOOST_CHECK_EQUAL(GetBlockSubsidyInner(bits, height, consensus, /*fV20Active=*/false), baseline);
            BOOST_CHECK_EQUAL(GetBlockSubsidyInner(bits, height, consensus, /*fV20Active=*/true), baseline);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
