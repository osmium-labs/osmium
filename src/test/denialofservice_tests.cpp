// Copyright (c) 2011-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Unit tests for denial-of-service detection/prevention code

#include <banman.h>
#include <chainparams.h>
#include <llmq/context.h>
#include <net.h>
#include <net_processing.h>
#include <pubkey.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <script/standard.h>
#include <test/util/net.h>
#include <test/util/setup_common.h>
#include <util/string.h>
#include <util/system.h>
#include <util/time.h>
#include <validation.h>
#include <governance/governance.h>

#include <array>
#include <stdint.h>

#include <boost/test/unit_test.hpp>


// Tests these internal-to-net_processing.cpp methods:
extern bool AddOrphanTx(const CTransactionRef& tx, NodeId peer);
extern void EraseOrphansFor(NodeId peer);
extern unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans);

struct COrphanTx {
    CTransactionRef tx;
    NodeId fromPeer;
    int64_t nTimeExpire;
};
extern std::map<uint256, COrphanTx> mapOrphanTransactions GUARDED_BY(g_cs_orphans);

static CService ip(uint32_t i)
{
    struct in_addr s;
    s.s_addr = i;
    return CService(CNetAddr(s), Params().GetDefaultPort());
}

static NodeId id = 0;

void UpdateLastBlockAnnounceTime(NodeId node, int64_t time_in_seconds);

BOOST_FIXTURE_TEST_SUITE(denialofservice_tests, TestingSetup)

// Test eviction of an outbound peer whose chain never advances
// Mock a node connection, and use mocktime to simulate a peer
// which never sends any headers messages.  PeerLogic should
// decide to evict that outbound peer, after the appropriate timeouts.
// Note that we protect 4 outbound nodes from being subject to
// this logic; this test takes advantage of that protection only
// being applied to nodes which send headers with sufficient
// work.
BOOST_AUTO_TEST_CASE(outbound_slow_chain_eviction)
{
    const CChainParams& chainparams = Params();
    auto connman = std::make_unique<CConnman>(0x1337, 0x1337, *m_node.addrman);
    auto peerLogic = PeerManager::make(chainparams, *connman, *m_node.addrman, nullptr, *m_node.scheduler,
                                       *m_node.chainman, *m_node.mempool, *m_node.govman, m_node.cj_ctx,
                                       m_node.llmq_ctx, false);

    // Mock an outbound peer
    CAddress addr1(ip(0xa0b0c001), NODE_NONE);
    CNode dummyNode1(id++, ServiceFlags(NODE_NETWORK), INVALID_SOCKET, addr1, 0, 0, CAddress(), "", ConnectionType::OUTBOUND_FULL_RELAY);
    dummyNode1.SetCommonVersion(PROTOCOL_VERSION);

    peerLogic->InitializeNode(&dummyNode1);
    dummyNode1.fSuccessfullyConnected = true;

    // This test requires that we have a chain with non-zero work.
    {
        LOCK(cs_main);
        BOOST_CHECK(::ChainActive().Tip() != nullptr);
        BOOST_CHECK(::ChainActive().Tip()->nChainWork > 0);
    }

    // Test starts here
    {
        LOCK(dummyNode1.cs_sendProcessing);
        BOOST_CHECK(peerLogic->SendMessages(&dummyNode1)); // should result in getheaders
    }
    {
        LOCK(dummyNode1.cs_vSend);
        BOOST_CHECK(dummyNode1.vSendMsg.size() > 0);
        dummyNode1.vSendMsg.clear();
        dummyNode1.nSendMsgSize = 0;
    }

    int64_t nStartTime = GetTime();
    // Wait 21 minutes
    SetMockTime(nStartTime+21*60);
    {
        LOCK(dummyNode1.cs_sendProcessing);
        BOOST_CHECK(peerLogic->SendMessages(&dummyNode1)); // should result in getheaders
    }
    {
        LOCK(dummyNode1.cs_vSend);
        BOOST_CHECK(dummyNode1.vSendMsg.size() > 0);
    }
    // Wait 3 more minutes
    SetMockTime(nStartTime+24*60);
    {
        LOCK(dummyNode1.cs_sendProcessing);
        BOOST_CHECK(peerLogic->SendMessages(&dummyNode1)); // should result in disconnect
    }
    BOOST_CHECK(dummyNode1.fDisconnect == true);
    SetMockTime(0);

    peerLogic->FinalizeNode(dummyNode1);
}

static void AddRandomOutboundPeer(std::vector<CNode*>& vNodes, PeerManager& peerLogic, ConnmanTestMsg& connman)
{
    CAddress addr(ip(g_insecure_rand_ctx.randbits(32)), NODE_NONE);
    vNodes.emplace_back(new CNode(id++, ServiceFlags(NODE_NETWORK), INVALID_SOCKET, addr, 0, 0, CAddress(), "", ConnectionType::OUTBOUND_FULL_RELAY));
    CNode &node = *vNodes.back();
    node.SetCommonVersion(PROTOCOL_VERSION);

    peerLogic.InitializeNode(&node);
    node.fSuccessfullyConnected = true;

    connman.AddTestNode(node);
}

BOOST_AUTO_TEST_CASE(stale_tip_peer_management)
{
    const CChainParams& chainparams = Params();
    auto connman = std::make_unique<ConnmanTestMsg>(0x1337, 0x1337, *m_node.addrman);
    auto peerLogic = PeerManager::make(chainparams, *connman, *m_node.addrman, nullptr, *m_node.scheduler,
                                       *m_node.chainman, *m_node.mempool, *m_node.govman, m_node.cj_ctx,
                                       m_node.llmq_ctx, false);

    constexpr int max_outbound_full_relay = MAX_OUTBOUND_FULL_RELAY_CONNECTIONS;
    CConnman::Options options;
    options.nMaxConnections = DEFAULT_MAX_PEER_CONNECTIONS;
    options.m_max_outbound_full_relay = max_outbound_full_relay;
    options.nMaxFeeler = MAX_FEELER_CONNECTIONS;

    connman->Init(options);
    std::vector<CNode *> vNodes;

    // Mock some outbound peers
    for (int i = 0; i < max_outbound_full_relay; ++i) {
        AddRandomOutboundPeer(vNodes, *peerLogic, *connman);
    }

    peerLogic->CheckForStaleTipAndEvictPeers();

    // No nodes should be marked for disconnection while we have no extra peers
    for (const CNode *node : vNodes) {
        BOOST_CHECK(node->fDisconnect == false);
    }

    SetMockTime(GetTime() + 3 * chainparams.GetConsensus().nPowTargetSpacing + 1);

    // Now tip should definitely be stale, and we should look for an extra
    // outbound peer
    peerLogic->CheckForStaleTipAndEvictPeers();
    BOOST_CHECK(connman->GetTryNewOutboundPeer());

    // Still no peers should be marked for disconnection
    for (const CNode *node : vNodes) {
        BOOST_CHECK(node->fDisconnect == false);
    }

    // If we add one more peer, something should get marked for eviction
    // on the next check (since we're mocking the time to be in the future, the
    // required time connected check should be satisfied).
    AddRandomOutboundPeer(vNodes, *peerLogic, *connman);

    peerLogic->CheckForStaleTipAndEvictPeers();
    for (int i = 0; i < max_outbound_full_relay; ++i) {
        BOOST_CHECK(vNodes[i]->fDisconnect == false);
    }
    // Last added node should get marked for eviction
    BOOST_CHECK(vNodes.back()->fDisconnect == true);

    vNodes.back()->fDisconnect = false;

    // Update the last announced block time for the last
    // peer, and check that the next newest node gets evicted.
    UpdateLastBlockAnnounceTime(vNodes.back()->GetId(), GetTime());

    peerLogic->CheckForStaleTipAndEvictPeers();
    for (int i = 0; i < max_outbound_full_relay - 1; ++i) {
        BOOST_CHECK(vNodes[i]->fDisconnect == false);
    }
    BOOST_CHECK(vNodes[max_outbound_full_relay-1]->fDisconnect == true);
    BOOST_CHECK(vNodes.back()->fDisconnect == false);

    for (const CNode *node : vNodes) {
        peerLogic->FinalizeNode(*node);
    }

    connman->ClearTestNodes();
}

BOOST_AUTO_TEST_CASE(DoS_banning)
{
    const CChainParams& chainparams = Params();
    auto banman = std::make_unique<BanMan>(GetDataDir() / "banlist", nullptr, DEFAULT_MISBEHAVING_BANTIME);
    auto connman = std::make_unique<CConnman>(0x1337, 0x1337, *m_node.addrman);
    auto peerLogic = PeerManager::make(chainparams, *connman, *m_node.addrman, banman.get(), *m_node.scheduler,
                                       *m_node.chainman, *m_node.mempool, *m_node.govman, m_node.cj_ctx,
                                       m_node.llmq_ctx, false);

    banman->ClearBanned();
    CAddress addr1(ip(0xa0b0c001), NODE_NONE);
    CNode dummyNode1(id++, NODE_NETWORK, INVALID_SOCKET, addr1, 0, 0, CAddress(), "", ConnectionType::INBOUND);
    dummyNode1.SetCommonVersion(PROTOCOL_VERSION);
    peerLogic->InitializeNode(&dummyNode1);
    dummyNode1.fSuccessfullyConnected = true;
    peerLogic->Misbehaving(dummyNode1.GetId(), 100); // Should get banned
    {
        LOCK(dummyNode1.cs_sendProcessing);
        BOOST_CHECK(peerLogic->SendMessages(&dummyNode1));
    }
    BOOST_CHECK(banman->IsDiscouraged(addr1));
    BOOST_CHECK(!banman->IsDiscouraged(ip(0xa0b0c001|0x0000ff00))); // Different IP, not banned

    CAddress addr2(ip(0xa0b0c002), NODE_NONE);
    CNode dummyNode2(id++, NODE_NETWORK, INVALID_SOCKET, addr2, 1, 1, CAddress(), "", ConnectionType::INBOUND);
    dummyNode2.SetCommonVersion(PROTOCOL_VERSION);
    peerLogic->InitializeNode(&dummyNode2);
    dummyNode2.fSuccessfullyConnected = true;
    peerLogic->Misbehaving(dummyNode2.GetId(), 50);
    {
        LOCK(dummyNode2.cs_sendProcessing);
        BOOST_CHECK(peerLogic->SendMessages(&dummyNode2));
    }
    BOOST_CHECK(!banman->IsDiscouraged(addr2)); // 2 not banned yet...
    BOOST_CHECK(banman->IsDiscouraged(addr1));  // ... but 1 still should be
    peerLogic->Misbehaving(dummyNode2.GetId(), 50);
    {
        LOCK(dummyNode2.cs_sendProcessing);
        BOOST_CHECK(peerLogic->SendMessages(&dummyNode2));
    }
    BOOST_CHECK(banman->IsDiscouraged(addr2));

    peerLogic->FinalizeNode(dummyNode1);
    peerLogic->FinalizeNode(dummyNode2);
}

BOOST_AUTO_TEST_CASE(DoS_banscore)
{
    const CChainParams& chainparams = Params();
    auto banman = std::make_unique<BanMan>(GetDataDir() / "banlist", nullptr, DEFAULT_MISBEHAVING_BANTIME);
    auto connman = std::make_unique<CConnman>(0x1337, 0x1337, *m_node.addrman);
    auto peerLogic = PeerManager::make(chainparams, *connman, *m_node.addrman, banman.get(), *m_node.scheduler,
                                       *m_node.chainman, *m_node.mempool, *m_node.govman, m_node.cj_ctx,
                                       m_node.llmq_ctx, false);

    banman->ClearBanned();
    gArgs.ForceSetArg("-banscore", "111"); // because 11 is my favorite number
    CAddress addr1(ip(0xa0b0c001), NODE_NONE);
    CNode dummyNode1(id++, NODE_NETWORK, INVALID_SOCKET, addr1, 3, 1, CAddress(), "", ConnectionType::INBOUND);
    dummyNode1.SetCommonVersion(PROTOCOL_VERSION);
    peerLogic->InitializeNode(&dummyNode1);
    dummyNode1.fSuccessfullyConnected = true;
    {
        peerLogic->Misbehaving(dummyNode1.GetId(), 100);
    }
    {
        LOCK(dummyNode1.cs_sendProcessing);
        BOOST_CHECK(peerLogic->SendMessages(&dummyNode1));
    }
    BOOST_CHECK(!banman->IsDiscouraged(addr1));
    {
        peerLogic->Misbehaving(dummyNode1.GetId(), 10);
    }
    {
        LOCK(dummyNode1.cs_sendProcessing);
        BOOST_CHECK(peerLogic->SendMessages(&dummyNode1));
    }
    BOOST_CHECK(!banman->IsDiscouraged(addr1));
    {
        peerLogic->Misbehaving(dummyNode1.GetId(), 1);
    }
    {
        LOCK(dummyNode1.cs_sendProcessing);
        BOOST_CHECK(peerLogic->SendMessages(&dummyNode1));
    }
    BOOST_CHECK(banman->IsDiscouraged(addr1));
    gArgs.ForceSetArg("-banscore", ToString(DEFAULT_BANSCORE_THRESHOLD));

    peerLogic->FinalizeNode(dummyNode1);
}

BOOST_AUTO_TEST_CASE(DoS_bantime)
{
    const CChainParams& chainparams = Params();
    auto banman = std::make_unique<BanMan>(GetDataDir() / "banlist", nullptr, DEFAULT_MISBEHAVING_BANTIME);
    auto connman = std::make_unique<CConnman>(0x1337, 0x1337, *m_node.addrman);
    auto peerLogic = PeerManager::make(chainparams, *connman, *m_node.addrman, banman.get(), *m_node.scheduler,
                                       *m_node.chainman, *m_node.mempool, *m_node.govman, m_node.cj_ctx,
                                       m_node.llmq_ctx, false);

    banman->ClearBanned();
    int64_t nStartTime = GetTime();
    SetMockTime(nStartTime); // Overrides future calls to GetTime()

    CAddress addr(ip(0xa0b0c001), NODE_NONE);
    CNode dummyNode(id++, NODE_NETWORK, INVALID_SOCKET, addr, 4, 4, CAddress(), "", ConnectionType::INBOUND);
    dummyNode.SetCommonVersion(PROTOCOL_VERSION);
    peerLogic->InitializeNode(&dummyNode);
    dummyNode.fSuccessfullyConnected = true;

    peerLogic->Misbehaving(dummyNode.GetId(), 100);
    {
        LOCK(dummyNode.cs_sendProcessing);
        BOOST_CHECK(peerLogic->SendMessages(&dummyNode));
    }
    BOOST_CHECK(banman->IsDiscouraged(addr));

    peerLogic->FinalizeNode(dummyNode);
}

static CTransactionRef RandomOrphan()
{
    std::map<uint256, COrphanTx>::iterator it;
    LOCK2(cs_main, g_cs_orphans);
    it = mapOrphanTransactions.lower_bound(InsecureRand256());
    if (it == mapOrphanTransactions.end())
        it = mapOrphanTransactions.begin();
    return it->second.tx;
}

static void MakeNewKeyWithFastRandomContext(CKey& key)
{
    std::vector<unsigned char> keydata;
    keydata = g_insecure_rand_ctx.randbytes(32);
    key.Set(keydata.data(), keydata.data() + keydata.size(), /*fCompressedIn*/ true);
    assert(key.IsValid());
}

BOOST_AUTO_TEST_CASE(DoS_mapOrphans)
{
    // This test had non-deterministic coverage due to
    // randomly selected seeds.
    // This seed is chosen so that all branches of the function
    // ecdsa_signature_parse_der_lax are executed during this test.
    // Specifically branches that run only when an ECDSA
    // signature's R and S values have leading zeros.
    g_insecure_rand_ctx = FastRandomContext{uint256{33}};

    CKey key;
    MakeNewKeyWithFastRandomContext(key);
    FillableSigningProvider keystore;
    BOOST_CHECK(keystore.AddKey(key));

    // 50 orphan transactions:
    for (int i = 0; i < 50; i++)
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.n = 0;
        tx.vin[0].prevout.hash = InsecureRand256();
        tx.vin[0].scriptSig << OP_1;
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(PKHash(key.GetPubKey()));

        AddOrphanTx(MakeTransactionRef(tx), i);
    }

    // ... and 50 that depend on other orphans:
    for (int i = 0; i < 50; i++)
    {
        CTransactionRef txPrev = RandomOrphan();

        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.n = 0;
        tx.vin[0].prevout.hash = txPrev->GetHash();
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(PKHash(key.GetPubKey()));
        BOOST_CHECK(SignSignature(keystore, *txPrev, tx, 0, SIGHASH_ALL));

        AddOrphanTx(MakeTransactionRef(tx), i);
    }

    // This really-big orphan should be ignored:
    for (int i = 0; i < 10; i++)
    {
        CTransactionRef txPrev = RandomOrphan();

        CMutableTransaction tx;
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(PKHash(key.GetPubKey()));
        tx.vin.resize(2777);
        for (unsigned int j = 0; j < tx.vin.size(); j++)
        {
            tx.vin[j].prevout.n = j;
            tx.vin[j].prevout.hash = txPrev->GetHash();
        }
        BOOST_CHECK(SignSignature(keystore, *txPrev, tx, 0, SIGHASH_ALL));
        // Re-use same signature for other inputs
        // (they don't have to be valid for this test)
        for (unsigned int j = 1; j < tx.vin.size(); j++)
            tx.vin[j].scriptSig = tx.vin[0].scriptSig;

        BOOST_CHECK(!AddOrphanTx(MakeTransactionRef(tx), i));
    }

    LOCK2(cs_main, g_cs_orphans);
    // Test EraseOrphansFor:
    for (NodeId i = 0; i < 3; i++)
    {
        size_t sizeBefore = mapOrphanTransactions.size();
        EraseOrphansFor(i);
        BOOST_CHECK(mapOrphanTransactions.size() < sizeBefore);
    }

    // Test LimitOrphanTxSize() function:
    LimitOrphanTxSize(40);
    BOOST_CHECK(mapOrphanTransactions.size() <= 40);
    LimitOrphanTxSize(10);
    BOOST_CHECK(mapOrphanTransactions.size() <= 10);
    LimitOrphanTxSize(0);
    BOOST_CHECK(mapOrphanTransactions.empty());
}

// SocketHandler() polls without waiting while it has work. A node can sit in mapReceivableNodes
// without being read (paused, sending, disconnecting); counting it as work turns the zero-timeout
// poll into a spin that a peer can hold for as long as it keeps that state.
BOOST_AUTO_TEST_CASE(socket_handler_polls_only_for_receivable_nodes)
{
    auto connman = std::make_unique<ConnmanTestMsg>(0x1337, 0x1337, *m_node.addrman);
    auto make_node = [] {
        return std::make_unique<CNode>(id++, NODE_NONE, INVALID_SOCKET, CAddress(), 0, 0, CAddress(), "", ConnectionType::INBOUND);
    };
    BOOST_CHECK(!connman->OnlyPollNow());

    auto readable = make_node();
    connman->AddReceivableNode(*readable);
    BOOST_CHECK(connman->OnlyPollNow());

    readable->fPauseRecv = true;
    BOOST_CHECK(!connman->OnlyPollNow());
    readable->fPauseRecv = false;

    readable->nSendMsgSize = 1;
    BOOST_CHECK(!connman->OnlyPollNow());
    readable->nSendMsgSize = 0;

    readable->fDisconnect = true;
    BOOST_CHECK(!connman->OnlyPollNow());
    readable->fDisconnect = false;

    // One unreadable node must not hide a readable one.
    auto paused = make_node();
    paused->fPauseRecv = true;
    connman->AddReceivableNode(*paused);
    BOOST_CHECK(connman->OnlyPollNow());
    connman->RemoveReceivableNode(*readable);
    BOOST_CHECK(!connman->OnlyPollNow());
    connman->RemoveReceivableNode(*paused);
}

#ifdef USE_WAKEUP_PIPE
// Queue one message from a connected peer and process it. The flood size decides whether queueing
// it paused the peer; returns whether processing it woke the socket thread.
static bool ProcessOneMessageWakesSelect(TestingSetup& setup, unsigned int receive_flood_size, bool expect_paused)
{
    auto connman = std::make_unique<ConnmanTestMsg>(0x1337, 0x1337, *setup.m_node.addrman);
    CConnman::Options options;
    options.nReceiveFloodSize = receive_flood_size;
    connman->Init(options);
    BOOST_REQUIRE(connman->OpenWakeupPipe());
    auto peerLogic = PeerManager::make(Params(), *connman, *setup.m_node.addrman, nullptr, *setup.m_node.scheduler,
                                       *setup.m_node.chainman, *setup.m_node.mempool, *setup.m_node.govman,
                                       setup.m_node.cj_ctx, setup.m_node.llmq_ctx, false);

    CNode node(id++, NODE_NETWORK, INVALID_SOCKET, CAddress(ip(0xa0b0c002), NODE_NONE), 0, 0, CAddress(), "", ConnectionType::INBOUND);
    node.SetCommonVersion(PROTOCOL_VERSION);
    node.nVersion = PROTOCOL_VERSION;
    peerLogic->InitializeNode(&node);
    node.fSuccessfullyConnected = true;

    // An unknown command is only logged, so processing it has no other effect.
    CSerializedNetMsg msg;
    msg.command = "unknowncmd";
    connman->ReceiveMsgFrom(node, msg);
    BOOST_REQUIRE_EQUAL(node.fPauseRecv, expect_paused);

    std::atomic<bool> interrupt{false};
    peerLogic->ProcessMessages(&node, interrupt);
    BOOST_CHECK(!node.fPauseRecv);
    const bool woke{connman->WakeupPipeSignaled()};

    peerLogic->FinalizeNode(node);
    connman->CloseWakeupPipe();
    return woke;
}

// Nothing else tells the socket thread that a paused peer can be read again, so without a wakeup
// the peer waits out SELECT_TIMEOUT_MILLISECONDS, which halves throughput to a peer that pauses often.
BOOST_AUTO_TEST_CASE(process_messages_wakes_select_on_unpause)
{
    // With a flood size of 0, queueing any message pauses the peer.
    BOOST_CHECK(ProcessOneMessageWakesSelect(*this, 0, /*expect_paused=*/true));
}

BOOST_AUTO_TEST_CASE(process_messages_does_not_wake_select_without_unpause)
{
    BOOST_CHECK(!ProcessOneMessageWakesSelect(*this, 1000 * 1000, /*expect_paused=*/false));
}
#endif // USE_WAKEUP_PIPE

BOOST_AUTO_TEST_SUITE_END()
