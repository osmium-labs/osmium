// Copyright (c) 2018-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <devfee_payment.h>
#include <evo/evodb.h>
#include <governance/governance.h>
#include <llmq/blockprocessor.h>
#include <llmq/chainlocks.h>
#include <llmq/context.h>
#include <llmq/instantsend.h>
#include <miner.h>
#include <pow.h>
#include <random.h>
#include <script/standard.h>
#include <spork.h>
#include <test/util/setup_common.h>
#include <util/time.h>
#include <validation.h>
#include <validationinterface.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace validation_block_tests {
struct MinerTestingSetup : public RegTestingSetup {
    // Height of every block this fixture has minted, so a re-parented block can be paid what its
    // real height allows. Seeded with the genesis block on first use.
    std::map<uint256, int> m_block_heights;

    std::shared_ptr<CBlock> Block(const uint256& prev_hash);
    std::shared_ptr<const CBlock> GoodBlock(const uint256& prev_hash);
    std::shared_ptr<const CBlock> BadBlock(const uint256& prev_hash);
    std::shared_ptr<CBlock> FinalizeBlock(std::shared_ptr<CBlock> pblock);
    void BuildChain(const uint256& root, int height, const unsigned int invalid_rate, const unsigned int branch_rate, const unsigned int max_size, std::vector<std::shared_ptr<const CBlock>>& blocks);
};
} // namespace validation_block_tests

static const std::vector<unsigned char> V_OP_TRUE{OP_TRUE};

BOOST_FIXTURE_TEST_SUITE(validation_block_tests, MinerTestingSetup)

struct TestSubscriber final : public CValidationInterface {
    uint256 m_expected_tip;

    explicit TestSubscriber(uint256 tip) : m_expected_tip(tip) {}

    void UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload) override
    {
        BOOST_CHECK_EQUAL(m_expected_tip, pindexNew->GetBlockHash());
    }

    void BlockConnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex) override
    {
        BOOST_CHECK_EQUAL(m_expected_tip, block->hashPrevBlock);
        BOOST_CHECK_EQUAL(m_expected_tip, pindex->pprev->GetBlockHash());

        m_expected_tip = block->GetHash();
    }

    void BlockDisconnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex) override
    {
        BOOST_CHECK_EQUAL(m_expected_tip, block->GetHash());
        BOOST_CHECK_EQUAL(m_expected_tip, pindex->GetBlockHash());

        m_expected_tip = block->hashPrevBlock;
    }
};

std::shared_ptr<CBlock> MinerTestingSetup::Block(const uint256& prev_hash)
{
    static int i = 0;
    static uint64_t time = Params().GenesisBlock().nTime;

    CScript pubKey;
    pubKey << i++ << OP_TRUE;

    if (m_block_heights.empty()) m_block_heights[Params().GenesisBlock().GetHash()] = 0;
    const int height = m_block_heights.at(prev_hash) + 1;

    auto ptemplate = BlockAssembler(*m_node.sporkman, *m_node.govman, *m_node.llmq_ctx, *m_node.evodb, ::ChainstateActive(), *m_node.mempool, Params()).CreateNewBlock(pubKey);
    auto pblock = std::make_shared<CBlock>(ptemplate->block);
    pblock->hashPrevBlock = prev_hash;
    pblock->nTime = ++time;

    pubKey.clear();
    {
        pubKey << OP_HASH160 << ToByteVector(CScriptID(CScript() << OP_TRUE))
               << OP_EQUAL;
    }
    // CreateNewBlock() sized the reward against the current tip, but the block was just
    // re-parented, so it has to pay what its real height allows. Osmium mints an 8000-coin premine
    // at height 1 and 0.1 per block after that, so a template minted at genesis pays 80000x the
    // limit as soon as it sits deeper in the chain (bad-cb-amount). Dash's flat ~500 per regtest
    // block made re-parenting harmless here.
    const CAmount reward = GetBlockSubsidyInner(pblock->nBits, height - 1, Params().GetConsensus(), false);

    // Make the coinbase transaction with two outputs:
    // One zero-value one that has a unique pubkey to make sure that blocks at
    // the same height can have a different hash. Another one that has the
    // coinbase reward in a P2SH with OP_TRUE as scriptPubKey to make it easy to
    // spend
    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    txCoinbase.vout.resize(2);
    txCoinbase.vout[1].scriptPubKey = pubKey;
    txCoinbase.vout[1].nValue = reward;
    txCoinbase.vout[0].nValue = 0;

    // Truncating to two outputs above dropped the assembler's devfee payment, which is consensus
    // enforced past nDevfeePayment.getStartBlock() (bad-cb-devfee-payment-not-found).
    //
    // The amount cannot simply be this block's own, because ProcessNewBlock() checks the coinbase
    // against the height of the CURRENT TIP rather than the block's (validation.cpp:4270), while
    // ConnectBlock and AcceptBlock use the block's real height. A block below the start height
    // therefore still has to carry the payment once the tip has climbed past it, and the amount is
    // sized from whichever height validation happens to use. Pay the larger of the two so the block
    // is acceptable under either, and keep it out of the spendable output so the coinbase still
    // pays no more than the reward. A scratch transaction absorbs FillDevfeePayment's deduction,
    // which it always applies to vout[0], while ours has to stay at index 1 for the reorg test.
    {
        DevfeePayment devfee_payment = Params().GetConsensus().nDevfeePayment;
        const int start_block = devfee_payment.getStartBlock();
        const int tip_height = ::ChainActive().Height() + 1;

        // The check only bites once the height validation uses is past the start block, so the
        // largest amount it can demand is the largest subsidy over that enforced range. The range
        // ends at the tip, and the subsidy is piecewise: it decays through each halving and steps
        // up between tiers, so the maximum sits at one end or the other. Sampling both endpoints
        // keeps this correct without walking every height, and deliberately never samples the
        // premine at height 1, which is not an enforceable height.
        const Consensus::Params& consensus = Params().GetConsensus();
        const CAmount subsidy_at_start = GetBlockSubsidyInner(pblock->nBits, start_block + 1, consensus, false);
        const CAmount subsidy_at_tip = GetBlockSubsidyInner(pblock->nBits, std::max(tip_height - 1, start_block + 1), consensus, false);
        const CAmount worst_case = std::max({reward, subsidy_at_start, subsidy_at_tip});

        CMutableTransaction scratch;
        scratch.vout.emplace_back(reward, CScript());
        CTxOut txout_devfee;
        devfee_payment.FillDevfeePayment(scratch, std::max(height, start_block + 1), worst_case, txout_devfee);
        if (!txout_devfee.IsNull()) {
            // Paying the devfee out of a reward that cannot cover it would trip bad-cb-amount
            // instead, which would be a far more confusing failure than this assertion.
            BOOST_REQUIRE(txout_devfee.nValue < txCoinbase.vout[1].nValue);
            txCoinbase.vout[1].nValue -= txout_devfee.nValue;
            txCoinbase.vout.emplace_back(txout_devfee);
        }
    }

    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));

    return pblock;
}

std::shared_ptr<CBlock> MinerTestingSetup::FinalizeBlock(std::shared_ptr<CBlock> pblock)
{
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);

    while (!CheckProofOfWork(pblock->GetHash(), pblock->nBits, Params().GetConsensus())) {
        ++(pblock->nNonce);
    }
    m_block_heights[pblock->GetHash()] = m_block_heights.at(pblock->hashPrevBlock) + 1;

    return pblock;
}

// construct a valid block
std::shared_ptr<const CBlock> MinerTestingSetup::GoodBlock(const uint256& prev_hash)
{
    return FinalizeBlock(Block(prev_hash));
}

// construct an invalid block (but with a valid header)
std::shared_ptr<const CBlock> MinerTestingSetup::BadBlock(const uint256& prev_hash)
{
    auto pblock = Block(prev_hash);

    CMutableTransaction coinbase_spend;
    coinbase_spend.vin.push_back(CTxIn(COutPoint(pblock->vtx[0]->GetHash(), 0), CScript(), 0));
    coinbase_spend.vout.push_back(pblock->vtx[0]->vout[0]);

    CTransactionRef tx = MakeTransactionRef(coinbase_spend);
    pblock->vtx.push_back(tx);

    auto ret = FinalizeBlock(pblock);
    return ret;
}

void MinerTestingSetup::BuildChain(const uint256& root, int height, const unsigned int invalid_rate, const unsigned int branch_rate, const unsigned int max_size, std::vector<std::shared_ptr<const CBlock>>& blocks)
{
    if (height <= 0 || blocks.size() >= max_size) return;

    bool gen_invalid = InsecureRandRange(100) < invalid_rate;
    bool gen_fork = InsecureRandRange(100) < branch_rate;

    const std::shared_ptr<const CBlock> pblock = gen_invalid ? BadBlock(root) : GoodBlock(root);
    blocks.push_back(pblock);
    if (!gen_invalid) {
        BuildChain(pblock->GetHash(), height - 1, invalid_rate, branch_rate, max_size, blocks);
    }

    if (gen_fork) {
        blocks.push_back(GoodBlock(root));
        BuildChain(blocks.back()->GetHash(), height - 1, invalid_rate, branch_rate, max_size, blocks);
    }
}

BOOST_AUTO_TEST_CASE(processnewblock_signals_ordering)
{
    // build a large-ish chain that's likely to have some forks
    std::vector<std::shared_ptr<const CBlock>> blocks;
    while (blocks.size() < 50) {
        blocks.clear();
        BuildChain(Params().GenesisBlock().GetHash(), 100, 15, 10, 500, blocks);
    }

    bool ignored;
    BlockValidationState state;
    std::vector<CBlockHeader> headers;
    std::transform(blocks.begin(), blocks.end(), std::back_inserter(headers), [](std::shared_ptr<const CBlock> b) { return b->GetBlockHeader(); });

    // Process all the headers so we understand the toplogy of the chain
    BOOST_CHECK(Assert(m_node.chainman)->ProcessNewBlockHeaders(headers, state, Params()));

    // Connect the genesis block and drain any outstanding events
    BOOST_CHECK(Assert(m_node.chainman)->ProcessNewBlock(Params(), std::make_shared<CBlock>(Params().GenesisBlock()), true, &ignored));
    SyncWithValidationInterfaceQueue();

    // subscribe to events (this subscriber will validate event ordering)
    const CBlockIndex* initial_tip = nullptr;
    {
        LOCK(cs_main);
        initial_tip = ::ChainActive().Tip();
    }
    auto sub = std::make_shared<TestSubscriber>(initial_tip->GetBlockHash());
    RegisterSharedValidationInterface(sub);

    // create a bunch of threads that repeatedly process a block generated above at random
    // this will create parallelism and randomness inside validation - the ValidationInterface
    // will subscribe to events generated during block validation and assert on ordering invariance
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([&]() {
            bool ignored;
            FastRandomContext insecure;
            for (int i = 0; i < 1000; i++) {
                auto block = blocks[insecure.randrange(blocks.size() - 1)];
                Assert(m_node.chainman)->ProcessNewBlock(Params(), block, true, &ignored);
            }

            // to make sure that eventually we process the full chain - do it here
            for (auto block : blocks) {
                if (block->vtx.size() == 1) {
                    bool processed = Assert(m_node.chainman)->ProcessNewBlock(Params(), block, true, &ignored);
                    assert(processed);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
    SyncWithValidationInterfaceQueue();

    UnregisterSharedValidationInterface(sub);

    LOCK(cs_main);
    BOOST_CHECK_EQUAL(sub->m_expected_tip, ::ChainActive().Tip()->GetBlockHash());
}

/**
 * Test that mempool updates happen atomically with reorgs.
 *
 * This prevents RPC clients, among others, from retrieving immediately-out-of-date mempool data
 * during large reorgs.
 *
 * The test verifies this by creating a chain of `num_txs` blocks, matures their coinbases, and then
 * submits txns spending from their coinbase to the mempool. A fork chain is then processed,
 * invalidating the txns and evicting them from the mempool.
 *
 * We verify that the mempool updates atomically by polling it continuously
 * from another thread during the reorg and checking that its size only changes
 * once. The size changing exactly once indicates that the polling thread's
 * view of the mempool is either consistent with the chain state before reorg,
 * or consistent with the chain state after the reorg, and not just consistent
 * with some intermediate state during the reorg.
 */
BOOST_AUTO_TEST_CASE(mempool_locks_reorg)
{
    bool ignored;
    auto ProcessBlock = [&](std::shared_ptr<const CBlock> block) -> bool {
        return Assert(m_node.chainman)->ProcessNewBlock(Params(), block, /* fForceProcessing */ true, /* fNewBlock */ &ignored);
    };

    // Process all mined blocks
    BOOST_REQUIRE(ProcessBlock(std::make_shared<CBlock>(Params().GenesisBlock())));
    auto last_mined = GoodBlock(Params().GenesisBlock().GetHash());
    BOOST_REQUIRE(ProcessBlock(last_mined));

    // Run the test multiple times
    for (int test_runs = 3; test_runs > 0; --test_runs) {
        BOOST_CHECK_EQUAL(last_mined->GetHash(), ::ChainActive().Tip()->GetBlockHash());

        // Later on split from here
        const uint256 split_hash{last_mined->hashPrevBlock};

        // Create a bunch of transactions to spend the miner rewards of the
        // most recent blocks
        std::vector<CTransactionRef> txs;
        for (int num_txs = 22; num_txs > 0; --num_txs) {
            CMutableTransaction mtx;
            mtx.vin.push_back(
                CTxIn(COutPoint(last_mined->vtx[0]->GetHash(), 1),
                      CScript() << ToByteVector(CScript() << OP_TRUE)));
            // Two outputs to make sure the transaction is larger than 100 bytes
            for (int i = 1; i < 3; ++i) {
                mtx.vout.emplace_back(
                    CTxOut(50000,
                           CScript() << OP_DUP << OP_HASH160
                                     << ToByteVector(CScriptID(CScript() << i))
                                     << OP_EQUALVERIFY << OP_CHECKSIG));
            }
            txs.push_back(MakeTransactionRef(mtx));

            last_mined = GoodBlock(last_mined->GetHash());
            BOOST_REQUIRE(ProcessBlock(last_mined));
        }

        // Mature the inputs of the txs
        for (int j = COINBASE_MATURITY; j > 0; --j) {
            last_mined = GoodBlock(last_mined->GetHash());
            BOOST_REQUIRE(ProcessBlock(last_mined));
        }

        // Mine a reorg (and hold it back) before adding the txs to the mempool
        const uint256 tip_init{last_mined->GetHash()};

        std::vector<std::shared_ptr<const CBlock>> reorg;
        last_mined = GoodBlock(split_hash);
        reorg.push_back(last_mined);
        for (size_t j = COINBASE_MATURITY + txs.size() + 1; j > 0; --j) {
            last_mined = GoodBlock(last_mined->GetHash());
            reorg.push_back(last_mined);
        }

        // Add the txs to the tx pool
        {
            LOCK(cs_main);
            for (const auto& tx : txs) {
                const MempoolAcceptResult result = AcceptToMemoryPool(::ChainstateActive(), *m_node.mempool, tx, false /* bypass_limits */);
                BOOST_REQUIRE(result.m_result_type == MempoolAcceptResult::ResultType::VALID);
            }
        }

        // Check that all txs are in the pool
        {
            LOCK(m_node.mempool->cs);
            BOOST_CHECK_EQUAL(m_node.mempool->mapTx.size(), txs.size());
        }

        // Run a thread that simulates an RPC caller that is polling while
        // validation is doing a reorg
        std::thread rpc_thread{[&]() {
            // This thread is checking that the mempool either contains all of
            // the transactions invalidated by the reorg, or none of them, and
            // not some intermediate amount.
            // Bounded so that a reorg which never happens fails the checks below instead of
            // spinning here forever and taking the whole test binary with it.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            while (true) {
                LOCK(m_node.mempool->cs);
                if (m_node.mempool->mapTx.size() == 0) {
                    // We are done with the reorg
                    break;
                }
                if (std::chrono::steady_clock::now() > deadline) return;
                // Internally, we might be in the middle of the reorg, but
                // externally the reorg to the most-proof-of-work chain should
                // be atomic. So the caller assumes that the returned mempool
                // is consistent. That is, it has all txs that were there
                // before the reorg.
                assert(m_node.mempool->mapTx.size() == txs.size());
                continue;
            }
            LOCK(cs_main);
            // We are done with the reorg, so the tip must have changed
            assert(tip_init != ::ChainActive().Tip()->GetBlockHash());
        }};

        // Submit the reorg in this thread to invalidate and remove the txs from the tx pool
        for (const auto& b : reorg) {
            ProcessBlock(b);
        }
        // Check that the reorg was eventually successful
        BOOST_CHECK_EQUAL(last_mined->GetHash(), ::ChainActive().Tip()->GetBlockHash());

        // We can join the other thread, which returns when the reorg was successful
        rpc_thread.join();
    }
}
BOOST_AUTO_TEST_SUITE_END()
