// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <evo/evodb.h>
#include <governance/governance.h>
#include <llmq/blockprocessor.h>
#include <llmq/chainlocks.h>
#include <llmq/context.h>
#include <llmq/instantsend.h>
#include <miner.h>
#include <policy/policy.h>
#include <pow.h>
#include <script/standard.h>
#include <spork.h>
#include <uint256.h>
#include <algorithm>
#include <thread>
#include <vector>
#include <util/strencodings.h>
#include <util/system.h>
#include <util/time.h>
#include <validation.h>

#include <test/util/setup_common.h>

#include <memory>

#include <boost/test/unit_test.hpp>

namespace miner_tests {

//! Find the lowest nonce >= nStart satisfying PoW, searching in parallel.
//!
//! Only runs when the nonce stored in blockinfo[] no longer matches the chain (e.g. after a
//! consensus-parameter change), in which case every block has to be re-mined and a single core
//! takes ~25s per block. Threads scan disjoint contiguous ranges of one window and the window's
//! minimum is taken, so the result is the same regardless of how the threads are scheduled --
//! the harvested table stays reproducible.
static uint32_t FindNonceParallel(const CBlock& block, const Consensus::Params& consensus, uint32_t nStart)
{
    const unsigned nThreads = std::max(1u, std::min(20u, std::thread::hardware_concurrency()));
    constexpr uint32_t CHUNK = 1u << 18;

    for (uint64_t base = nStart;; base += static_cast<uint64_t>(CHUNK) * nThreads) {
        std::vector<uint32_t> hit(nThreads, 0);
        std::vector<char> found(nThreads, 0);
        std::vector<std::thread> workers;
        workers.reserve(nThreads);

        for (unsigned t = 0; t < nThreads; ++t) {
            workers.emplace_back([&, t] {
                CPureBlockHeader hdr = block; // slice: the hash depends only on the header
                const uint64_t from = base + static_cast<uint64_t>(t) * CHUNK;
                for (uint32_t k = 0; k < CHUNK; ++k) {
                    hdr.nNonce = static_cast<uint32_t>(from + k);
                    if (CheckProofOfWork(hdr.GetHash(), hdr.nBits, consensus)) {
                        hit[t] = hdr.nNonce;
                        found[t] = 1;
                        return;
                    }
                }
            });
        }
        for (auto& w : workers) w.join();

        bool any = false;
        uint32_t best = 0;
        for (unsigned t = 0; t < nThreads; ++t) {
            if (found[t] && (!any || hit[t] < best)) { best = hit[t]; any = true; }
        }
        if (any) return best;
    }
}

struct MinerTestingSetup : public TestingSetup {
    void TestPackageSelection(const CChainParams& chainparams, const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst) EXCLUSIVE_LOCKS_REQUIRED(::cs_main, m_node.mempool->cs);
    bool TestSequenceLocks(const CTransaction& tx, int flags) EXCLUSIVE_LOCKS_REQUIRED(::cs_main, m_node.mempool->cs)
    {
        CCoinsViewMemPool view_mempool(&m_node.chainman->ActiveChainstate().CoinsTip(), *m_node.mempool);
        return CheckSequenceLocks(m_node.chainman->ActiveChain().Tip(), view_mempool, tx, flags);
    }
    BlockAssembler AssemblerForTest(const CChainParams& params);
};
} // namespace miner_tests

BOOST_FIXTURE_TEST_SUITE(miner_tests, MinerTestingSetup)

static CFeeRate blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);

BlockAssembler MinerTestingSetup::AssemblerForTest(const CChainParams& params)
{
    BlockAssembler::Options options;

    options.nBlockMaxSize = DEFAULT_BLOCK_MAX_SIZE;
    options.blockMinFeeRate = blockMinFeeRate;
    return BlockAssembler(*m_node.sporkman, *m_node.govman, *m_node.llmq_ctx, *m_node.evodb, ::ChainstateActive(), *m_node.mempool, params, options);
}

constexpr static struct {
    unsigned char extranonce;
    unsigned int nonce;
} blockinfo[] = {
    {0, 0x004b259a}, {0, 0x001d53b9}, {0, 0x005e14c5}, {0, 0x003afb69},
    {0, 0x200e239a}, {0, 0x600cdc94}, {0, 0xe0345752}, {0, 0x2063ae68},
    {0, 0x400ecff4}, {0, 0x8011a4a6}, {0, 0x6047ffcf}, {0, 0x604e229c},
    {0, 0x4035d1b8}, {0, 0xc0b55489}, {0, 0xc0276326}, {0, 0x400e1258},
    {0, 0xc00e3919}, {0, 0xa06f3811}, {0, 0x201c8f39}, {0, 0x8025b7fe},
    {0, 0xc0232bc7}, {0, 0x802a361a}, {0, 0xc00db5f1}, {0, 0x60104e2f},
    {0, 0xc008070b}, {0, 0xa04a6b81}, {0, 0x2021bf4a}, {0, 0xc01dee80},
    {0, 0x003ba554}, {0, 0xa026eb16}, {0, 0x400e33d4}, {0, 0x40221689},
    {0, 0x0016b8d2}, {0, 0x801381d3}, {0, 0xc00b424b}, {0, 0xe051951a},
    {0, 0xe03fc134}, {0, 0x00171659}, {0, 0x6019c92a}, {0, 0x0037e274},
    {0, 0x604acc31}, {0, 0x602ddd23}, {0, 0xc0190613}, {0, 0xa027f44e},
    {0, 0x4009e7b7}, {0, 0xe012f5fc}, {0, 0xe01d724c}, {0, 0xa01e57ea},
    {0, 0x80277d29}, {0, 0xc027ecf4}, {0, 0x0015be23}, {0, 0x40184a04},
    {0, 0x603ff794}, {0, 0x801c100e}, {0, 0xe02c0f14}, {0, 0xe01e0ffe},
    {0, 0x80480722}, {0, 0x60281e89}, {0, 0x402729ee}, {0, 0x602bf161},
    {0, 0xa00e895f}, {0, 0x60205d36}, {0, 0x8009799d}, {0, 0x6038af88},
    {0, 0xe018cad0}, {0, 0x204cd78b}, {0, 0x00254b5e}, {0, 0x200d56dd},
    {0, 0x2016e83b}, {0, 0x804a3a83}, {0, 0x60157603}, {0, 0x406f78c8},
    {0, 0xc0237e06}, {0, 0x20118920}, {0, 0x002c3c92}, {0, 0x000ec33e},
    {0, 0x20481463}, {0, 0x205f0d56}, {0, 0x40287f3b}, {0, 0x806a27e8},
    {0, 0xe02f3e6c}, {0, 0xc02d55a5}, {0, 0x8040d08b}, {0, 0xc006ed07},
    {0, 0x0025d37d}, {0, 0xa01a05c8}, {0, 0x60107cac}, {0, 0xe00aabe8},
    {0, 0xa02bc5b4}, {0, 0xa02bcac0}, {0, 0xe0099c1e}, {0, 0x4036abb1},
    {0, 0xc0286534}, {0, 0xe01ba826}, {0, 0xa0518df7}, {0, 0xc030cec9},
    {0, 0x20137f24}, {0, 0xc0555cd0}, {0, 0xe00f26a0}, {0, 0x600788a4},
    {0, 0x00127c86}, {0, 0x0019dcd5}, {0, 0x406bd452}, {0, 0x20192505},
    {0, 0x400707be}, {0, 0x6037aaad}, {0, 0x401f528a}, {0, 0xc01f757d},
    {0, 0x602a624f}, {0, 0xc04d6943}, {0, 0x40363a65}, {0, 0x80677f02},
    {0, 0x401acfeb}, {0, 0x603e5edb}, {0, 0xe018af1d}, {0, 0x4032161c},
    {0, 0xa049b16c}, {0, 0x6043751b}, {0, 0x60475abc},
};
constexpr static size_t blockinfo_size = sizeof(blockinfo) / sizeof(blockinfo[0]);

static CBlockIndex CreateBlockIndex(int nHeight) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    CBlockIndex index;
    index.nHeight = nHeight;
    index.pprev = ::ChainActive().Tip();
    return index;
}

// Test suite for ancestor feerate transaction selection.
// Implemented as an additional function, rather than a separate test case,
// to allow reusing the blockchain created in CreateNewBlock_validity.
void MinerTestingSetup::TestPackageSelection(const CChainParams& chainparams, const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst)
{
    // Disable free transactions, otherwise TX selection is non-deterministic
    gArgs.SoftSetArg("-blockprioritysize", "0");

    // Test the ancestor feerate transaction selection.
    TestMemPoolEntryHelper entry;

    // Test that a medium fee transaction will be selected after a higher fee
    // rate package with a low fee rate parent.
    const CAmount BASEVALUE = std::min({txFirst[0]->vout[0].nValue, txFirst[1]->vout[0].nValue, txFirst[2]->vout[0].nValue, txFirst[3]->vout[0].nValue});
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = BASEVALUE - 1000;
    // This tx has a low fee: 1000 satoshis
    uint256 hashParentTx = tx.GetHash(); // save this txid for later use
    m_node.mempool->addUnchecked(entry.Fee(1000).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));

    // This tx has a medium fee: 10000 satoshis
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vout[0].nValue = BASEVALUE - 10000;
    uint256 hashMediumFeeTx = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(10000).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));

    // This tx has a high fee, but depends on the first transaction
    tx.vin[0].prevout.hash = hashParentTx;
    tx.vout[0].nValue = BASEVALUE - 1000 - 50000; // 50k satoshi fee
    uint256 hashHighFeeTx = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(50000).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));

    std::unique_ptr<CBlockTemplate> pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
    BOOST_REQUIRE_EQUAL(pblocktemplate->block.vtx.size(), 4U);
    BOOST_CHECK(pblocktemplate->block.vtx[1]->GetHash() == hashParentTx);
    BOOST_CHECK(pblocktemplate->block.vtx[2]->GetHash() == hashHighFeeTx);
    BOOST_CHECK(pblocktemplate->block.vtx[3]->GetHash() == hashMediumFeeTx);

    // Test that a package below the block min tx fee doesn't get included
    tx.vin[0].prevout.hash = hashHighFeeTx;
    tx.vout[0].nValue = BASEVALUE - 1000 - 50000; // 0 fee
    uint256 hashFreeTx = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(0).FromTx(tx));
    size_t freeTxSize = GetVirtualTransactionSize(CTransaction(tx));

    // Calculate a fee on child transaction that will put the package just
    // below the block min tx fee (assuming 1 child tx of the same size).
    CAmount feeToUse = blockMinFeeRate.GetFee(2*freeTxSize) - 1;

    tx.vin[0].prevout.hash = hashFreeTx;
    tx.vout[0].nValue = BASEVALUE - 1000 - 50000 - feeToUse;
    uint256 hashLowFeeTx = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(feeToUse).FromTx(tx));
    pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
    // Verify that the free tx and the low fee tx didn't get selected
    for (size_t i=0; i<pblocktemplate->block.vtx.size(); ++i) {
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashFreeTx);
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashLowFeeTx);
    }

    // Test that packages above the min relay fee do get included, even if one
    // of the transactions is below the min relay fee
    // Remove the low fee transaction and replace with a higher fee transaction
    m_node.mempool->removeRecursive(CTransaction(tx), MemPoolRemovalReason::MANUAL);
    tx.vout[0].nValue -= 2; // Now we should be just over the min relay fee
    hashLowFeeTx = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(feeToUse+2).FromTx(tx));
    pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
    BOOST_REQUIRE_EQUAL(pblocktemplate->block.vtx.size(), 6U);
    BOOST_CHECK(pblocktemplate->block.vtx[4]->GetHash() == hashFreeTx);
    BOOST_CHECK(pblocktemplate->block.vtx[5]->GetHash() == hashLowFeeTx);

    // Test that transaction selection properly updates ancestor fee
    // calculations as ancestor transactions get included in a block.
    // Add a 0-fee transaction that has 2 outputs.
    tx.vin[0].prevout.hash = txFirst[2]->GetHash();
    tx.vout.resize(2);
    tx.vout[0].nValue = BASEVALUE - BASEVALUE/10;
    tx.vout[1].nValue = BASEVALUE/10; // side output
    uint256 hashFreeTx2 = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(0).SpendsCoinbase(true).FromTx(tx));

    // This tx can't be mined by itself
    tx.vin[0].prevout.hash = hashFreeTx2;
    tx.vout.resize(1);
    feeToUse = blockMinFeeRate.GetFee(freeTxSize);
    tx.vout[0].nValue = BASEVALUE - BASEVALUE/10 - feeToUse;
    uint256 hashLowFeeTx2 = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(feeToUse).SpendsCoinbase(false).FromTx(tx));
    pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);

    // Verify that this tx isn't selected.
    for (size_t i=0; i<pblocktemplate->block.vtx.size(); ++i) {
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashFreeTx2);
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashLowFeeTx2);
    }

    // This tx will be mineable, and should cause hashLowFeeTx2 to be selected
    // as well.
    tx.vin[0].prevout.n = 1;
    tx.vout[0].nValue = BASEVALUE/10 - 10000; // 10k satoshi fee
    m_node.mempool->addUnchecked(entry.Fee(10000).FromTx(tx));
    pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
    BOOST_REQUIRE_EQUAL(pblocktemplate->block.vtx.size(), 9U);
    BOOST_CHECK(pblocktemplate->block.vtx[8]->GetHash() == hashLowFeeTx2);
}

// NOTE: These tests rely on CreateNewBlock doing its own self-validation!
BOOST_AUTO_TEST_CASE(CreateNewBlock_validity)
{
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    const CChainParams& chainparams = *chainParams;
    CScript scriptPubKey = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    std::unique_ptr<CBlockTemplate> pblocktemplate, pemptyblocktemplate;
    CMutableTransaction tx;
    CScript script;
    uint256 hash;
    uint256 hash_abs_time; // captured for the post-advance check below
    TestMemPoolEntryHelper entry;
    entry.nFee = 11;
    entry.nHeight = 11;

    fCheckpointsEnabled = false;

    // Simple block creation, nothing special yet:
    BOOST_CHECK(pemptyblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));

    // We can't make transactions until we have inputs
    // Therefore, load 100 blocks :)
    int baseheight = 0;
    std::vector<CTransactionRef> txFirst;

        auto createAndProcessEmptyBlock = [&]() {
        int i = ::ChainActive().Height() % blockinfo_size;
        pemptyblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey); // fresh template: coinbase must match current height (CbTx, devfee)
        CBlock *pblock = &pemptyblocktemplate->block; // pointer for convenience
        {
            LOCK(cs_main);
            pblock->SetBaseVersion(2, chainparams.GetConsensus().nAuxpowChainId);
            pblock->nTime = std::max<int64_t>(::ChainActive().Tip()->GetMedianTimePast()+1, ::ChainActive().Tip()->GetBlockTime() + chainparams.GetConsensus().nPowTargetSpacing);
            CMutableTransaction txCoinbase(*pblock->vtx[0]);
            // Do NOT reset nVersion/nType/vExtraPayload here (upstream set nVersion = 1).
            // Two reasons: past DIP0003Height the coinbase is a CbTx and clobbering these
            // fields would invalidate it; and right now any change to the coinbase changes
            // the merkle root, so every nonce in blockinfo[] below stops matching and the
            // suite falls back to brute-force mining (~9 min instead of ~8 s). If you do
            // change the coinbase, re-harvest the table from the FOUND_NONCE output.
            txCoinbase.vin[0].scriptSig = CScript() << (::ChainActive().Height() + 1);
            txCoinbase.vin[0].scriptSig.push_back(blockinfo[i].extranonce);
            txCoinbase.vin[0].scriptSig.push_back(::ChainActive().Height());
            txCoinbase.vout[0].scriptPubKey = CScript();
            pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
            if (txFirst.size() == 0)
                baseheight = ::ChainActive().Height();
            if (txFirst.size() < 4)
                txFirst.push_back(pblock->vtx[0]);
            pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
            pblock->nNonce = blockinfo[i].nonce;

            // This will usually succeed in the first round as we take the nonce from blockinfo
            // It's however useful when adding new blocks with unknown nonces (you should add the found block to blockinfo)
            if (!CheckProofOfWork(pblock->GetHash(), pblock->nBits, chainparams.GetConsensus())) {
                pblock->nNonce = FindNonceParallel(*pblock, chainparams.GetConsensus(), pblock->nNonce);
            }
            printf("FOUND_NONCE {%d, 0x%08x},\n", blockinfo[i].extranonce, pblock->nNonce);
        }
        std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
        BOOST_CHECK(Assert(m_node.chainman)->ProcessNewBlock(chainparams, shared_pblock, true, nullptr));
        pblock->hashPrevBlock = pblock->GetHash();
    };

    for ([[maybe_unused]] const auto& _ : blockinfo) {
        createAndProcessEmptyBlock();
    }

    {
    LOCK(cs_main);
    LOCK(m_node.mempool->cs);

    // Just to make sure we can still make simple blocks
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));

    const CAmount BLOCKSUBSIDY = std::min({txFirst[0]->vout[0].nValue, txFirst[1]->vout[0].nValue, txFirst[2]->vout[0].nValue, txFirst[3]->vout[0].nValue});
    // LOWFEE must still clear blockMinFeeRate, or the assembler's fee gate (a `return`, not a
    // `continue`) aborts selection outright and the block comes back empty. Osmium's subsidy is
    // ~0.095 coin, so a plain fraction of it lands below 1 sat/byte for the multi-kB transactions
    // this test reuses -- derive it from the fee rate as well and take whichever is larger.
    const CAmount LOWFEE = std::max<CAmount>(BLOCKSUBSIDY / 2000, blockMinFeeRate.GetFee(20000));
    const CAmount HIGHFEE = BLOCKSUBSIDY / 10;
    const CAmount HIGHERFEE = 4 * (BLOCKSUBSIDY / 10);

    // block sigops > limit: 1000 CHECKMULTISIG + 1
    tx.vin.resize(1);
    // NOTE: OP_NOP is used to force 20 SigOps for the CHECKMULTISIG
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_0 << OP_0 << OP_NOP << OP_CHECKMULTISIG << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 2001; ++i)
    {
    tx.vout[0].nValue -= 1;
        hash = tx.GetHash();
        bool spendsCoinbase = i == 0; // only first tx spends coinbase
        // If we don't set the # of sig ops in the CTxMemPoolEntry, template creation fails
        m_node.mempool->addUnchecked(entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(spendsCoinbase).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
    }

    BOOST_CHECK_EXCEPTION(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-blk-sigops"));
    m_node.mempool->clear();

    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vout[0].nValue = BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 1001; ++i)
    {
        // Only needs to stay distinct and positive: the fee is declared via entry.Fee().
        // Decrementing by LOWFEE would underflow here -- 1001 * LOWFEE exceeds Osmium's subsidy.
        tx.vout[0].nValue -= 1;
        hash = tx.GetHash();
        bool spendsCoinbase = i == 0; // only first tx spends coinbase
        // If we do set the # of sig ops in the CTxMemPoolEntry, template creation passes
        m_node.mempool->addUnchecked(entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(spendsCoinbase).SigOps(20).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
    }
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    m_node.mempool->clear();

    // block size > limit
    tx.vin[0].scriptSig = CScript();
    // 18 * (520char + DROP) + OP_1 = 9433 bytes
    std::vector<unsigned char> vchData(520);
    for (unsigned int i = 0; i < 18; ++i)
        tx.vin[0].scriptSig << vchData << OP_DROP;
    tx.vin[0].scriptSig << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vout[0].nValue = BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 128; ++i)
    {
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetHash();
        bool spendsCoinbase = i == 0; // only first tx spends coinbase
        m_node.mempool->addUnchecked(entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(spendsCoinbase).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
    }
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    m_node.mempool->clear();

    // orphan in mempool, template creation fails
    hash = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(LOWFEE).Time(GetTime()).FromTx(tx));
    BOOST_CHECK_EXCEPTION(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-txns-inputs-missingorspent"));
    m_node.mempool->clear();

    // child with higher feerate than parent
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vout[0].nValue = BLOCKSUBSIDY-HIGHFEE;
    hash = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vin[0].prevout.hash = hash;
    tx.vin.resize(2);
    tx.vin[1].scriptSig = CScript() << OP_1;
    tx.vin[1].prevout.hash = txFirst[0]->GetHash();
    tx.vin[1].prevout.n = 0;
    tx.vout[0].nValue = tx.vout[0].nValue+BLOCKSUBSIDY-HIGHERFEE; //First txn output + fresh coinbase - new txn fee
    hash = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(HIGHERFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    m_node.mempool->clear();

    // coinbase in mempool, template creation fails
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_1;
    tx.vout[0].nValue = 0;
    hash = tx.GetHash();
    // give it a fee so it'll get mined
    m_node.mempool->addUnchecked(entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));
    // Should throw bad-cb-multiple
    BOOST_CHECK_EXCEPTION(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-cb-multiple"));
    m_node.mempool->clear();

    // double spend txn pair in mempool, template creation fails
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout[0].nValue = BLOCKSUBSIDY-HIGHFEE;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    hash = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vout[0].scriptPubKey = CScript() << OP_2;
    hash = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK_EXCEPTION(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-txns-inputs-missingorspent"));
    m_node.mempool->clear();

    // subsidy changing
    // int nHeight = ::ChainActive().Height();
    // // Create an actual 209999-long block chain (without valid blocks).
    // while (::ChainActive().Tip()->nHeight < 209999) {
    //     CBlockIndex* prev = ::ChainActive().Tip();
    //     CBlockIndex* next = new CBlockIndex();
    //     next->phashBlock = new uint256(InsecureRand256());
    //     pcoinsTip->SetBestBlock(next->GetBlockHash());
    //     next->pprev = prev;
    //     next->nHeight = prev->nHeight + 1;
    //     next->BuildSkip();
    //     ::ChainActive().SetTip(next);
    // }
    //BOOST_CHECK(pblocktemplate = BlockAssembler(chainparams).CreateNewBlock(scriptPubKey));
    // // Extend to a 210000-long block chain.
    // while (::ChainActive().Tip()->nHeight < 210000) {
    //     CBlockIndex* prev = ::ChainActive().Tip();
    //     CBlockIndex* next = new CBlockIndex();
    //     next->phashBlock = new uint256(InsecureRand256());
    //     pcoinsTip->SetBestBlock(next->GetBlockHash());
    //     next->pprev = prev;
    //     next->nHeight = prev->nHeight + 1;
    //     next->BuildSkip();
    //     ::ChainActive().SetTip(next);
    // }
    //BOOST_CHECK(pblocktemplate = BlockAssembler(chainparams).CreateNewBlock(scriptPubKey));

    // invalid (pre-p2sh) txn in mempool, template creation fails
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout[0].nValue = BLOCKSUBSIDY-LOWFEE;
    script = CScript() << OP_0;
    tx.vout[0].scriptPubKey = GetScriptForDestination(ScriptHash(script));
    hash = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vin[0].prevout.hash = hash;
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(script.begin(), script.end());
    tx.vout[0].nValue -= LOWFEE;
    hash = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));
    // Should throw block-validation-failed
    BOOST_CHECK_EXCEPTION(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("block-validation-failed"));
    m_node.mempool->clear();

    // // Delete the dummy blocks again.
    // while (::ChainActive().Tip()->nHeight > nHeight) {
    //     CBlockIndex* del = ::ChainActive().Tip();
    //     ::ChainActive().SetTip(del->pprev);
    //     ::ChainstateActive().CoinsTip().SetBestBlock(del->pprev->GetBlockHash());
    //     delete del->phashBlock;
    //     delete del;
    // }

    // non-final txs in mempool
    SetMockTime(::ChainActive().Tip()->GetMedianTimePast()+1);
    int flags = LOCKTIME_VERIFY_SEQUENCE|LOCKTIME_MEDIAN_TIME_PAST;
    // height map
    std::vector<int> prevheights;

    // relative height locked
    tx.nVersion = 2;
    tx.vin.resize(1);
    prevheights.resize(1);
    tx.vin[0].prevout.hash = txFirst[0]->GetHash(); // only 1 transaction
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].nSequence = ::ChainActive().Tip()->nHeight + 1; // txFirst[0] is the 2nd block
    prevheights[0] = baseheight + 1;
    tx.vout.resize(1);
    tx.vout[0].nValue = BLOCKSUBSIDY-HIGHFEE;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    tx.nLockTime = 0;
    hash = tx.GetHash();
    const uint256 hash_rel_height = hash;
    m_node.mempool->addUnchecked(entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK(CheckFinalTx(::ChainActive().Tip(), CTransaction(tx), flags)); // Locktime passes
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), flags)); // Sequence locks fail
    BOOST_CHECK(SequenceLocks(CTransaction(tx), flags, prevheights, CreateBlockIndex(::ChainActive().Tip()->nHeight + 2))); // Sequence locks pass on 2nd block

    // relative time locked
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | (((::ChainActive().Tip()->GetMedianTimePast()+1-::ChainActive()[1]->GetMedianTimePast()) >> CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) + 1); // txFirst[1] is the 3rd block
    prevheights[0] = baseheight + 2;
    hash = tx.GetHash();
    const uint256 hash_rel_time = hash;
    m_node.mempool->addUnchecked(entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(CheckFinalTx(::ChainActive().Tip(), CTransaction(tx), flags)); // Locktime passes
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), flags)); // Sequence locks fail

    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++)
        ::ChainActive().Tip()->GetAncestor(::ChainActive().Tip()->nHeight - i)->nTime += 512;                                // Trick the MedianTimePast
    BOOST_CHECK(SequenceLocks(CTransaction(tx), flags, prevheights, CreateBlockIndex(::ChainActive().Tip()->nHeight + 1))); // Sequence locks pass 512 seconds later
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++)
        ::ChainActive().Tip()->GetAncestor(::ChainActive().Tip()->nHeight - i)->nTime -= 512; //undo tricked MTP

    // absolute height locked
    tx.vin[0].prevout.hash = txFirst[2]->GetHash();
    tx.vin[0].nSequence = CTxIn::SEQUENCE_FINAL - 1;
    prevheights[0] = baseheight + 3;
    tx.nLockTime = ::ChainActive().Tip()->nHeight + 1;
    hash = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(!CheckFinalTx(::ChainActive().Tip(), CTransaction(tx), flags)); // Locktime fails
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), flags)); // Sequence locks pass
    BOOST_CHECK(IsFinalTx(CTransaction(tx), ::ChainActive().Tip()->nHeight + 2, ::ChainActive().Tip()->GetMedianTimePast())); // Locktime passes on 2nd block

    // absolute time locked
    tx.vin[0].prevout.hash = txFirst[3]->GetHash();
    tx.nLockTime = ::ChainActive().Tip()->GetMedianTimePast();
    prevheights.resize(1);
    prevheights[0] = baseheight + 4;
    hash = tx.GetHash();
    hash_abs_time = hash;
    m_node.mempool->addUnchecked(entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(!CheckFinalTx(::ChainActive().Tip(), CTransaction(tx), flags)); // Locktime fails
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), flags)); // Sequence locks pass
    BOOST_CHECK(IsFinalTx(CTransaction(tx), ::ChainActive().Tip()->nHeight + 2, ::ChainActive().Tip()->GetMedianTimePast() + 1)); // Locktime passes 1 second later

    // mempool-dependent transactions (not added)
    tx.vin[0].prevout.hash = hash;
    prevheights[0] = ::ChainActive().Tip()->nHeight + 1;
    tx.nLockTime = 0;
    tx.vin[0].nSequence = 0;
    BOOST_CHECK(CheckFinalTx(::ChainActive().Tip(), CTransaction(tx), flags)); // Locktime passes
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), flags)); // Sequence locks pass
    tx.vin[0].nSequence = 1;
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), flags)); // Sequence locks fail
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG;
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), flags)); // Sequence locks pass
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | 1;
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), flags)); // Sequence locks fail
    // The absolute height/time locked txs are excluded by CreateNewBlock's own IsFinalTx check.
    // The two RELATIVE (BIP68) locked txs are not: BlockAssembler does not evaluate sequence
    // locks, it relies on mempool acceptance having done so -- and these were injected with
    // addUnchecked, which bypasses that. Osmium enforces BIP68 at block level (SequenceLocks in
    // ConnectBlock, validation.cpp), so TestBlockValidity rejects the resulting template.
    // Upstream's "this still generates a valid template" note predates that enforcement.
    // Asserting the throw pins BIP68 enforcement reaching block validity.
    BOOST_CHECK_EXCEPTION(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-txns-nonfinal"));
    // However if we advance height by 1 and time by 512, all of them should be mined
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++)
        ::ChainActive().Tip()->GetAncestor(::ChainActive().Tip()->nHeight - i)->nTime += 512; //Trick the MedianTimePast

    // BlockAssembler does not evaluate sequence locks -- it trusts mempool acceptance to have
    // done so, and addUnchecked bypassed that. While these two BIP68-locked txs are queued,
    // ConnectBlock rejects EVERY template built from this mempool, including the one
    // createAndProcessEmptyBlock needs. Their enforcement is already asserted above; drop them
    // so the step below can test what it exists to test -- the ABSOLUTE height/time locked txs
    // becoming final once the chain advances by a block and 512 seconds.
    {
        LOCK(m_node.mempool->cs);
        for (const uint256& h : {hash_rel_height, hash_rel_time}) {
            if (const CTransactionRef txref = m_node.mempool->get(h)) {
                m_node.mempool->removeRecursive(*txref, MemPoolRemovalReason::MANUAL);
            }
        }
    }

    } // unlock cs_main while calling createAndProcessEmptyBlock

    // Mine an empty block
    createAndProcessEmptyBlock();

    {
    LOCK(cs_main);

    SetMockTime(::ChainActive().Tip()->GetMedianTimePast() + 1);

    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    // Both absolute-locked txs became minable once the chain advanced -- but not into the same
    // block. createAndProcessEmptyBlock() above is no longer empty on this chain: with the BIP68
    // txs dropped, the absolute TIME locked tx was already final and got mined into it, which is
    // asserted here. That leaves the absolute HEIGHT locked tx (nLockTime 120, final at 121) for
    // this template. Upstream expected 5 in one block because BIP68 was unenforced and nothing
    // was minable earlier.
    BOOST_CHECK(!m_node.mempool->exists(hash_abs_time));
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 2U);
    } // unlock cs_main while calling InvalidateBlock

    BlockValidationState state;
    ::ChainstateActive().InvalidateBlock(state, WITH_LOCK(cs_main, return ::ChainActive().Tip()));

    SetMockTime(0);
    m_node.mempool->clear();

    LOCK2(cs_main, m_node.mempool->cs);
    TestPackageSelection(chainparams, scriptPubKey, txFirst);

    fCheckpointsEnabled = true;
}

BOOST_AUTO_TEST_SUITE_END()
