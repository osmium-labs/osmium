// Copyright (c) 2021-2023 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/setup_common.h>

#include <bls/bls.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <deploymentstatus.h>
#include <messagesigner.h>
#include <miner.h>
#include <netbase.h>
#include <script/interpreter.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <script/standard.h>
#include <spork.h>
#include <validation.h>

#include <evo/deterministicmns.h>
#include <evo/mnhftx.h>
#include <evo/providertx.h>
#include <evo/specialtx.h>
#include <governance/governance.h>
#include <llmq/blockprocessor.h>
#include <llmq/chainlocks.h>
#include <llmq/context.h>
#include <llmq/instantsend.h>
#include <masternode/payments.h>
#include <util/enumerate.h>
#include <util/irange.h>

#include <boost/test/unit_test.hpp>

#include <map>
#include <vector>

using SimpleUTXOMap = std::map<COutPoint, std::pair<int, CAmount>>;

struct TestChainBRRBeforeActivationSetup : public TestChainSetup
{
    // Force fast DIP3 activation
    TestChainBRRBeforeActivationSetup() : TestChainSetup(497, {"-dip3params=30:50", "-vbparams=mn_rr:0:999999999999:20:16:12:5:1"}) {}
};

static SimpleUTXOMap BuildSimpleUtxoMap(const std::vector<CTransactionRef>& txs)
{
    SimpleUTXOMap utxos;
    // Only vout[0] is paid to the coinbase key. Osmium's coinbase carries an additional
    // devfee output (miner.cpp: FillDevfeePayment), which coinbaseKey cannot sign for -- 
    // including it here makes SignSignature fail once such an output gets selected.
    for (auto [i, tx] : enumerate(txs)) {
        utxos.try_emplace(COutPoint(tx->GetHash(), 0), std::make_pair((int)i + 1, tx->vout[0].nValue));
    }
    return utxos;
}

static std::vector<COutPoint> SelectUTXOs(SimpleUTXOMap& utoxs, CAmount amount, CAmount& changeRet)
{
    changeRet = 0;

    std::vector<COutPoint> selectedUtxos;
    CAmount selectedAmount = 0;
    while (!utoxs.empty()) {
        bool found = false;
        for (auto it = utoxs.begin(); it != utoxs.end(); ++it) {
            if (::ChainActive().Height() - it->second.first < 101) {
                continue;
            }

            found = true;
            selectedAmount += it->second.second;
            selectedUtxos.emplace_back(it->first);
            utoxs.erase(it);
            break;
        }
        BOOST_ASSERT(found);
        if (selectedAmount >= amount) {
            changeRet = selectedAmount - amount;
            break;
        }
    }

    return selectedUtxos;
}

static void FundTransaction(CMutableTransaction& tx, SimpleUTXOMap& utoxs, const CScript& scriptPayout, CAmount amount)
{
    CAmount change;
    auto inputs = SelectUTXOs(utoxs, amount, change);
    for (const auto& input : inputs) {
        tx.vin.emplace_back(input);
    }
    tx.vout.emplace_back(amount, scriptPayout);
    if (change != 0) {
        tx.vout.emplace_back(change, scriptPayout);
    }
}

static void SignTransaction(const CTxMemPool& mempool, CMutableTransaction& tx, const CKey& coinbaseKey)
{
    FillableSigningProvider tempKeystore;
    tempKeystore.AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey());

    for (auto [i, input] : enumerate(tx.vin)) {
        uint256 hashBlock;
        CTransactionRef txFrom = GetTransaction(/* block_index */ nullptr, &mempool, input.prevout.hash, Params().GetConsensus(), hashBlock);
        BOOST_ASSERT(txFrom);
        BOOST_ASSERT(SignSignature(tempKeystore, *txFrom, tx, i, SIGHASH_ALL));
    }
}

static CMutableTransaction CreateProRegTx(const CTxMemPool& mempool, SimpleUTXOMap& utxos, int port, const CScript& scriptPayout, const CKey& coinbaseKey, CKey& ownerKeyRet, CBLSSecretKey& operatorKeyRet)
{
    ownerKeyRet.MakeNewKey(true);
    operatorKeyRet.MakeNewKey();

    CProRegTx proTx;
    proTx.nVersion = CProRegTx::GetVersion(!bls::bls_legacy_scheme);
    proTx.collateralOutpoint.n = 0;
    proTx.addr = LookupNumeric("1.1.1.1", port);
    proTx.keyIDOwner = ownerKeyRet.GetPubKey().GetID();
    proTx.pubKeyOperator.Set(operatorKeyRet.GetPublicKey(), bls::bls_legacy_scheme.load());
    proTx.keyIDVoting = ownerKeyRet.GetPubKey().GetID();
    proTx.scriptPayout = scriptPayout;

    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_PROVIDER_REGISTER;
    FundTransaction(tx, utxos, scriptPayout, dmn_types::Regular.collat_amount);
    proTx.inputsHash = CalcTxInputsHash(CTransaction(tx));
    SetTxPayload(tx, proTx);
    SignTransaction(mempool, tx, coinbaseKey);

    return tx;
}

static CScript GenerateRandomAddress()
{
    CKey key;
    key.MakeNewKey(false);
    return GetScriptForDestination(PKHash(key.GetPubKey()));
}

BOOST_AUTO_TEST_SUITE(block_reward_reallocation_tests)

/**
 * Osmium does not implement Dash's gradual MN_RR reward reallocation. GetMasternodePayment() is a
 * fixed, height-stepped split that ignores its fV20Active argument entirely, and the deployments
 * that gated reallocation upstream cannot activate here at all: BIP9 signalling is structurally
 * impossible on an always-auxpow chain, because the chain ID occupies the top 16 bits of nVersion
 * while VersionBitsConditionChecker::Condition() requires the top three to be 001, and
 * validation.cpp rejects any other chain ID outright.
 *
 * So this suite pins the schedule Osmium actually has, at the tier boundaries, rather than a
 * reallocation that no code performs. The expected values are computed from the documented rule
 * (9/19, then 72/95, then 72/85) rather than read back out of GetMasternodePayment(), so that a
 * change to the split fails here instead of being rubber-stamped.
 */
BOOST_FIXTURE_TEST_CASE(masternode_payment_schedule, TestChainBRRBeforeActivationSetup)
{
    const auto& consensus_params = Params().GetConsensus();
    const int increase_block = consensus_params.nMasternodePaymentsIncreaseBlock;
    const int increase_block2 = consensus_params.nMasternodePaymentsIncreaseBlock2;
    BOOST_CHECK_LT(increase_block, increase_block2);

    // Independently derived: 10 COIN split at each tier, truncating like integer division does.
    constexpr CAmount block_value{10 * COIN};
    BOOST_CHECK_EQUAL(GetMasternodePayment(increase_block - 1, block_value, false), 473684210);
    BOOST_CHECK_EQUAL(GetMasternodePayment(increase_block, block_value, false), 757894736);
    BOOST_CHECK_EQUAL(GetMasternodePayment(increase_block2 - 1, block_value, false), 757894736);
    BOOST_CHECK_EQUAL(GetMasternodePayment(increase_block2, block_value, false), 847058823);

    // The steps are one block wide, not gradual: nothing interpolates between the tiers.
    BOOST_CHECK_EQUAL(GetMasternodePayment(0, block_value, false),
                      GetMasternodePayment(increase_block - 1, block_value, false));
    BOOST_CHECK_EQUAL(GetMasternodePayment(increase_block, block_value, false),
                      GetMasternodePayment(increase_block2 - 1, block_value, false));

    // fV20Active is vestigial here. Asserting it changes nothing keeps the dead parameter honest:
    // if V20 is ever wired into the split, this fails rather than silently altering payouts.
    for (const int height : {0, increase_block - 1, increase_block, increase_block2}) {
        BOOST_CHECK_EQUAL(GetMasternodePayment(height, block_value, false),
                          GetMasternodePayment(height, block_value, true));
    }

    // The split is a share of whatever it is handed, so it must scale with the block value.
    BOOST_CHECK_EQUAL(GetMasternodePayment(increase_block2, COIN, false), 84705882);

    auto& dmnman = *Assert(m_node.dmnman);
    CScript coinbasePubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

    BOOST_ASSERT(DeploymentDIP0003Enforced(WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Height()), consensus_params));

    // Register one MN, so the assembler has somebody to pay.
    CKey ownerKey;
    CBLSSecretKey operatorKey;
    auto utxos = BuildSimpleUtxoMap(m_coinbase_txns);
    auto tx = CreateProRegTx(*m_node.mempool, utxos, 1, GenerateRandomAddress(), coinbaseKey, ownerKey, operatorKey);
    CreateAndProcessBlock({tx}, coinbaseKey);

    {
        LOCK(cs_main);
        const CBlockIndex* const tip{m_node.chainman->ActiveChain().Tip()};
        dmnman.UpdatedBlockTip(tip);
        BOOST_ASSERT(dmnman.GetListAtChainTip().HasMN(tx.GetHash()));
        BOOST_CHECK_EQUAL(tip->nHeight, 498);
    }

    // Check the assembler actually pays the scheduled amount, on both sides of the second step.
    auto check_template_pays_schedule = [&]() EXCLUSIVE_LOCKS_REQUIRED(cs_main) {
        const CBlockIndex* const tip{m_node.chainman->ActiveChain().Tip()};
        const bool isV20Active{DeploymentActiveAfter(tip, consensus_params, Consensus::DEPLOYMENT_V20)};
        const CAmount block_subsidy = GetBlockSubsidyInner(tip->nBits, tip->nHeight, consensus_params, isV20Active);
        const CAmount masternode_payment = GetMasternodePayment(tip->nHeight, block_subsidy, isV20Active);
        const auto pblocktemplate = BlockAssembler(*m_node.sporkman, *m_node.govman, *m_node.llmq_ctx, *m_node.evodb, m_node.chainman->ActiveChainstate(), *m_node.mempool, Params()).CreateNewBlock(coinbasePubKey);
        BOOST_CHECK_EQUAL(pblocktemplate->voutMasternodePayments[0].nValue, masternode_payment);
        return masternode_payment;
    };

    CAmount payment_before_step{0};
    {
        LOCK(cs_main);
        BOOST_CHECK_LT(m_node.chainman->ActiveChain().Height(), increase_block2);
        payment_before_step = check_template_pays_schedule();
    }

    while (WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Height()) < increase_block2) {
        CreateAndProcessBlock({}, coinbaseKey);
        LOCK(cs_main);
        dmnman.UpdatedBlockTip(m_node.chainman->ActiveChain().Tip());
    }

    {
        LOCK(cs_main);
        BOOST_CHECK_EQUAL(m_node.chainman->ActiveChain().Height(), increase_block2);
        BOOST_ASSERT(dmnman.GetListAtChainTip().HasMN(tx.GetHash()));
        check_template_pays_schedule();
    }

    // Crossing the step must raise the masternode's share of the block, which is the whole point of
    // the schedule. Comparing shares rather than amounts keeps this independent of the subsidy,
    // which decays on its own every nSubsidyHalvingInterval blocks.
    {
        LOCK(cs_main);
        const CBlockIndex* const tip{m_node.chainman->ActiveChain().Tip()};
        const CAmount subsidy_now = GetBlockSubsidyInner(tip->nBits, tip->nHeight, consensus_params, false);
        const CAmount payment_now = GetMasternodePayment(tip->nHeight, subsidy_now, false);
        const CAmount subsidy_before = GetBlockSubsidyInner(tip->nBits, 498, consensus_params, false);
        BOOST_CHECK_GT(payment_now * subsidy_before, payment_before_step * subsidy_now);
    }
}

BOOST_AUTO_TEST_SUITE_END()
