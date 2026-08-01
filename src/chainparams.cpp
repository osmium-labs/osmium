// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Copyright (c) 2014-2024 The Dash Core developers
// Copyright (c) 2022-2024 The Raptoreum Core developers
// Copyright (c) 2024 The Osmium Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>

#include <chainparamsseeds.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <deploymentinfo.h>
#include <llmq/params.h>
#include <util/ranges.h>
#include <util/system.h>
#include <util/underlying.h>
#include <versionbits.h>

#include <arith_uint256.h>

#include <assert.h>

static size_t lastCheckMnCount = 0;
static int lastCheckHeight = -1;

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

static CBlock CreateDevNetGenesisBlock(const uint256 &prevBlockHash, const std::string& devNetName, uint32_t nTime, uint32_t nNonce, uint32_t nBits, const CAmount& genesisReward)
{
    assert(!devNetName.empty());

    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    // put height (BIP34) and devnet name into coinbase
    txNew.vin[0].scriptSig = CScript() << 1 << std::vector<unsigned char>(devNetName.begin(), devNetName.end());
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = CScript() << OP_RETURN;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = 4;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock = prevBlockHash;
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "Are You Not Entertained? - Maximus Decimus Meridius";
    const CScript genesisOutputScript = CScript() << ParseHex("042bc48e0a28ee4f3d019ce839c1b09c8833b9f13ca8753080bf45771df0f2b4d2bed1b44aefce24fa164778e6fdc7fb56e973ed775fdb53935ba90f3ae8d16c4b") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

static CBlock FindDevNetGenesisBlock(const CBlock &prevBlock, const CAmount& reward)
{
    std::string devNetName = gArgs.GetDevNetName();
    assert(!devNetName.empty());

    CBlock block = CreateDevNetGenesisBlock(prevBlock.GetHash(), devNetName, prevBlock.nTime + 1, 0, prevBlock.nBits, reward);

    arith_uint256 bnTarget;
    bnTarget.SetCompact(block.nBits);

    for (uint32_t nNonce = 0; nNonce < UINT32_MAX; nNonce++) {
        block.nNonce = nNonce;

        uint256 hash = block.GetHash();
        if (UintToArith256(hash) <= bnTarget)
            return block;
    }

    // This is very unlikely to happen as we start the devnet with a very low difficulty. In many cases even the first
    // iteration of the above loop will give a result already
    error("FindDevNetGenesisBlock: could not find devnet genesis block for %s", devNetName);
    assert(false);
}

bool CChainParams::IsValidMNActivation(int nBit, int64_t timePast) const
{
    assert(nBit < VERSIONBITS_NUM_BITS);

    for (int index = 0; index < Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++index) {
        if (consensus.vDeployments[index].bit == nBit) {
            auto& deployment = consensus.vDeployments[index];
            if (timePast > deployment.nTimeout || timePast < deployment.nStartTime) {
                LogPrintf("%s: activation by bit=%d deployment='%s' is out of time range start=%lld timeout=%lld\n", __func__, nBit, VersionBitsDeploymentInfo[Consensus::DeploymentPos(index)].name, deployment.nStartTime, deployment.nTimeout);
                continue;
            }
            if (!deployment.useEHF) {
                LogPrintf("%s: trying to set MnEHF for non-masternode activation fork bit=%d\n", __func__, nBit);
                return false;
            }
            LogPrintf("%s: set MnEHF for bit=%d is valid\n", __func__, nBit);
            return true;
        }
    }
    LogPrintf("%s: WARNING: unknown MnEHF fork bit=%d\n", __func__, nBit);
    return true;
}

std::optional<Consensus::LLMQParams> CChainParams::GetLLMQ(Consensus::LLMQType llmqType) const
{
    for (const auto& llmq_param : consensus.llmqs) {
        if (llmq_param.second.type == llmqType) {
            return std::make_optional(llmq_param.second);
        }
    }
    return std::nullopt;
}

/**
 * Main network on which people trade goods and services.
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = CBaseChainParams::MAIN;
        consensus.nSubsidyHalvingInterval = 1200 * 30; // ~ one month;
        consensus.BIP16Height = 0;
        consensus.nMasternodePaymentsStartBlock = 250; // not true, but it's oknMasternodePaymentsIncreaseBlock
        consensus.nInstantSendConfirmationsRequired = 6;
        consensus.nInstantSendKeepLock = 24;
        consensus.nBudgetPaymentsStartBlock = 250;
        consensus.nBudgetPaymentsCycleBlocks = 250;
        consensus.nBudgetPaymentsWindowBlocks = 100;
        consensus.nSuperblockStartBlock = 18000; // ~ 15 days;
        consensus.nSuperblockStartHash = uint256();
        consensus.nSuperblockCycle = 1200 * 30; // ~ one month
        consensus.nSuperblockMaturityWindow = 1200 * 3; // ~3 days before actual Superblock is emitted
        consensus.nGovernanceMinQuorum = 10;
        consensus.nGovernanceFilterElements = 20000;
        consensus.nMasternodeMinimumConfirmations = 15;
        consensus.BIP34Height = 1; // BIP34 activated immediately
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1; // BIP65 activated immediately
        consensus.BIP66Height = 1; // BIP66 activated immediately
        consensus.BIP147Height = 1; // BIP147 activated immediately
        consensus.CSVHeight = 1; // BIP68 activated immediately
        consensus.DIP0001Height = 2; // DIP0001 activated immediately
        consensus.DIP0003Height = 2; // DIP0003 activated immediately
        consensus.DIP0003EnforcementHeight = 2; // DIP0003 activated immediately
        consensus.DIP0003EnforcementHash = uint256();
        consensus.DIP0008Height = 2; // DIP0008 activated immediately
        consensus.BRRHeight = 999999;
        consensus.DIP0020Height = 150;
        consensus.DIP0024Height = 350;
        consensus.DIP0024QuorumsHeight = 350;
        consensus.V19Height = 350;
        consensus.MinBIP9WarningHeight = 350 + 960; // V19 activation height + miner confirmation window
				consensus.EvonodeHeight = 355000;
        consensus.powLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 20
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Maximus: 1 day
        consensus.nPowTargetSpacing = 72; // Maximus: 72 seconds
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nPowDGWHeight = 60;
        consensus.nRuleChangeActivationThreshold = 1140; // 95% of 1200
        consensus.nMinerConfirmationWindow = 1200; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        consensus.vDeployments[Consensus::DEPLOYMENT_V20].bit = 9;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nTimeout = 1740787200;        // March 1, 2025
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nWindowSize = 4032;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nThresholdStart = 3226;       // 80% of 4032
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nThresholdMin = 2420;         // 60% of 4032
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nFalloffCoeff = 5;            // this corresponds to 10 periods

        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].bit = 10;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nTimeout = 1740787200;      // March 1, 2025
        // NOTE: nWindowSize for MN_RR __MUST__ be greater than or equal to nSuperblockMaturityWindow for CSuperblock::GetPaymentsLimit() to work correctly
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nWindowSize = 4032;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nThresholdStart = 3226;     // 80% of 4032
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nThresholdMin = 2420;       // 60% of 4032
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nFalloffCoeff = 5;          // this corresponds to 10 periods
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].useEHF = true;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("00000000000000000000000000000000000000000000003c73865f236aa8227d8");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x5e44758ba1fee742f612f2a2ba80f14119302bd3095532e466f00f9a44be36a0"); // 694145

        // AuxPoW parameters
        consensus.nAuxpowChainId = 0x0045;
        consensus.fStrictChainId = true;

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0x4d; // M
        pchMessageStart[1] = 0x41; // A
        pchMessageStart[2] = 0x58; // X
        pchMessageStart[3] = 0x49; // I
        nDefaultPort = 9939;
        nDefaultPlatformP2PPort = 26656;
        nDefaultPlatformHTTPPort = 443;
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 1;

        genesis = CreateGenesisBlock(1729976223, 510979, 0x1e0ffff0, 1, 1 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock == uint256S("0x00000df94c615d91d23f22f8facd46e4b06e17f4214fff88af826ef77fdac797"));
        assert(genesis.hashMerkleRoot == uint256S("0xef49e9579b9b5aaf4b1b5b91c391d49aeaec81e582beecdd04dbf9fe2c7d6337"));

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as an addrfetch if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        vSeeds.emplace_back("dnsseed.maximuschain.com");

        // Maximus addresses start with 'M'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,50);
        // Maximus script addresses start with '3'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,5);
        // Maximus private keys start with 'X'
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,75); // change?
        // Maximus BIP32 pubkeys start with 'xpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        // Maximus BIP32 prvkeys start with 'xprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        // Maximus BIP44 coin type is '5'
        nExtCoinType = 5;

        vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_main), std::end(chainparams_seed_main));

        // long living quorum params
        consensus.llmqs[Consensus::LLMQ_50_60] = Consensus::llmq_50_60;
        consensus.llmqs[Consensus::LLMQ_60_75] = Consensus::llmq_60_75;
        consensus.llmqs[Consensus::LLMQ_400_60] = Consensus::llmq_20_60;
        consensus.llmqs[Consensus::LLMQ_400_85] = Consensus::llmq_20_85;
        consensus.llmqs[Consensus::LLMQ_100_67] = Consensus::llmq_100_67;
        consensus.llmqTypeChainLocks = Consensus::LLMQ_400_60;
        consensus.llmqTypeDIP0024InstantSend = Consensus::LLMQ_60_75;
        consensus.llmqTypePlatform = Consensus::LLMQ_100_67;
        consensus.llmqTypeMnhf = Consensus::LLMQ_400_85;

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fRequireRoutableExternalIP = true;
        m_is_test_chain = false;
        fAllowMultipleAddressesFromGroup = false;
        fAllowMultiplePorts = false;
        nLLMQConnectionRetryTimeout = 60;
        m_is_mockable_chain = false;

        nPoolMinParticipants = 3;
        nPoolMaxParticipants = 20;
        nFulfilledRequestExpireTime = 60*60; // fulfilled requests expire in 1 hour

        vSporkAddresses = {"MFbLxEDk419iF2eEXXNTBGQE3xaGzeDpPB"};
        nMinSporkKeys = 1;

        std::vector<DevfeeRewardStructure> rewardStructures = {  {INT_MAX, 17}  }; // 5% dev fee
        consensus.nDevfeePayment = DevfeePayment(rewardStructures, 1);

        checkpointData = {
            {
                {0, uint256S("0x00000df94c615d91d23f22f8facd46e4b06e17f4214fff88af826ef77fdac797")},
                {265000, uint256S("0x7ce8cc72253a317c2308ae9600d5e02d7a724c3e0d2f4c72e18f6ba16b8167d1")},
                {694145, uint256S("0x5e44758ba1fee742f612f2a2ba80f14119302bd3095532e466f00f9a44be36a0")},
            }
        };

        m_assumeutxo_data = MapAssumeutxo{
         // TODO to be specified in a future patch.
        };

        // getchaintxstats 694145

        chainTxData = ChainTxData{
            1785565295,
            1093717,
            0.0174
        };
    }
};

/**
 * Testnet (v3): public test network which is reset from time to time.
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = CBaseChainParams::TESTNET;
        consensus.nSubsidyHalvingInterval = 1200 * 30;
        consensus.BIP16Height = 0;
        consensus.nMasternodePaymentsStartBlock = 250;
        consensus.nInstantSendConfirmationsRequired = 6;
        consensus.nInstantSendKeepLock = 24;
        consensus.nBudgetPaymentsStartBlock = 250;
        consensus.nBudgetPaymentsCycleBlocks = 250;
        consensus.nBudgetPaymentsWindowBlocks = 100;
        consensus.nSuperblockStartBlock = 350;
        consensus.nSuperblockStartHash = uint256();
        consensus.nSuperblockCycle = 50; // Superblocks can be issued frequently on testnet
        consensus.nSuperblockMaturityWindow = 10;
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 500;
        consensus.nMasternodeMinimumConfirmations = 1;
        consensus.BIP34Height = 1; // BIP34 activated immediately
        consensus.BIP65Height = 1; // BIP65 activated immediately
        consensus.BIP66Height = 1; // BIP66 activated immediately
        consensus.BIP147Height = 1; // BIP147 activated immediately
        consensus.CSVHeight = 1; // BIP68 activated immediately
        consensus.DIP0001Height = 2; // DIP0001 activated immediately
        consensus.DIP0003Height = 2; // DIP0003 activated immediately
        consensus.DIP0003EnforcementHeight = 2; // DIP0003 activated immediately
        consensus.DIP0003EnforcementHash = uint256();
        consensus.DIP0008Height = 2; // DIP0008 activated immediately
        consensus.BRRHeight = 999999999;
        consensus.DIP0020Height = 150;
        consensus.DIP0024Height = 300;
        consensus.DIP0024QuorumsHeight = 300;
        consensus.V19Height = 300;
        consensus.MinBIP9WarningHeight = 300 + 960; // v19 activation height + miner confirmation window
				consensus.EvonodeHeight = 1000;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 1
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Maximus: 1 day
        consensus.nPowTargetSpacing = 36; // Maximus: 36 seconds for testnet
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nPowDGWHeight = 60; // same as mainnet
        consensus.nRuleChangeActivationThreshold = 900; // 75% for testchains
        consensus.nMinerConfirmationWindow = 1200; // Faster than normal for testnet (960 instead of 2016)

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        consensus.vDeployments[Consensus::DEPLOYMENT_V20].bit = 9;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nThresholdStart = 80;         // 80% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nThresholdMin = 60;           // 60% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nFalloffCoeff = 5;            // this corresponds to 10 periods

        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].bit = 10;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nThresholdStart = 80;       // 80% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nThresholdMin = 60;         // 60% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nFalloffCoeff = 5;          // this corresponds to 10 periods
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].useEHF = true;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000000000013af825fd");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x5e32a92042c6b135fffb91adfab43673c14b0f5636183dcb0e2c201dee735dad"); // 170000

        // AuxPoW parameters
        consensus.nAuxpowChainId = 0x0045;
        consensus.fStrictChainId = true;
        
        pchMessageStart[0] = 0x74; // t
        pchMessageStart[1] = 0x4d; // M
        pchMessageStart[2] = 0x41; // A
        pchMessageStart[3] = 0x58; // X
        nDefaultPort = 19939;
        nDefaultPlatformP2PPort = 22000;
        nDefaultPlatformHTTPPort = 22001;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 1;

        genesis = CreateGenesisBlock(1753600292, 3956165, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock == uint256S("0x000003cd91b4da9957b3411486acbfce64a1ed961188879a024b1ab2f12aac5a"));
        assert(genesis.hashMerkleRoot == uint256S("0x1c2da9786370d27309c45314f0137207c36b9a8a2524de63938128e40afcd427"));

        vFixedSeeds.clear();
        vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_test), std::end(chainparams_seed_test));

        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        vSeeds.emplace_back("testnet-seed.maximuschain.com"); // Just a static list of stable node(s), only supports x9

        // Testnet Maximus addresses start with 'm'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,110);
        // Testnet Maximus script addresses start with '5' or '6'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,12);
        // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Testnet Maximus BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        // Testnet Maximus BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // Testnet Maximus BIP44 coin type is '1' (All coin's testnet default)
        nExtCoinType = 1;

        // long living quorum params
        consensus.llmqs[Consensus::LLMQ_50_60] = Consensus::llmq_50_60;
        consensus.llmqs[Consensus::LLMQ_60_75] = Consensus::llmq_60_75;
        consensus.llmqs[Consensus::LLMQ_400_60] = Consensus::llmq_20_60;
        consensus.llmqs[Consensus::LLMQ_400_85] = Consensus::llmq_20_85;
        consensus.llmqs[Consensus::LLMQ_100_67] = Consensus::llmq_100_67;
        consensus.llmqTypeChainLocks = Consensus::LLMQ_400_60;
        consensus.llmqTypeDIP0024InstantSend = Consensus::LLMQ_60_75;
        consensus.llmqTypePlatform = Consensus::LLMQ_100_67;
        consensus.llmqTypeMnhf = Consensus::LLMQ_400_85;

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fRequireRoutableExternalIP = true;
        m_is_test_chain = true;
        fAllowMultipleAddressesFromGroup = false;
        fAllowMultiplePorts = true;
        nLLMQConnectionRetryTimeout = 60;
        m_is_mockable_chain = false;

        nPoolMinParticipants = 2;
        nPoolMaxParticipants = 20;
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes

        vSporkAddresses = {"mcb2Wcx4TSc8ebNJtW1QF7M6MrUXKBVG7E"};
        nMinSporkKeys = 1;

        std::vector<DevfeeRewardStructure> rewardStructures = {  {INT_MAX, 17}  }; // 5% dev fee
        consensus.nDevfeePayment = DevfeePayment(rewardStructures, 1, "mVcB7S2DPdNAYfa5Snh56VKiFFQWLpMsTh");

        checkpointData = {
            {
                {0, uint256S("0x000003cd91b4da9957b3411486acbfce64a1ed961188879a024b1ab2f12aac5a")},
            }
        };

        m_assumeutxo_data = MapAssumeutxo{
            // TODO to be specified in a future patch.
        };

        // getchaintxstats
        chainTxData = ChainTxData{
            0,
            0,
            0
        };
    }
};

/**
 * Devnet: The Development network intended for developers use.
 */
class CDevNetParams : public CChainParams {
public:
    explicit CDevNetParams(const ArgsManager& args) {
        strNetworkID = CBaseChainParams::DEVNET;
        consensus.nSubsidyHalvingInterval = 1200 * 30; // ~ one month;
        consensus.BIP16Height = 0;
        consensus.nMasternodePaymentsStartBlock = 250; // not true, but it's ok
        consensus.nInstantSendConfirmationsRequired = 2;
        consensus.nInstantSendKeepLock = 6;
        consensus.nBudgetPaymentsStartBlock = 250;
        consensus.nBudgetPaymentsCycleBlocks = 250;
        consensus.nBudgetPaymentsWindowBlocks = 100;
        consensus.nSuperblockStartBlock = 4200; // NOTE: Should satisfy nSuperblockStartBlock > nBudgetPeymentsStartBlock
        consensus.nSuperblockStartHash = uint256(); // do not check this on devnet
        consensus.nSuperblockCycle = 24; // Superblocks can be issued hourly on devnet
        consensus.nSuperblockMaturityWindow = 8;
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 500;
        consensus.nMasternodeMinimumConfirmations = 1;
        consensus.BIP34Height = 1; // BIP34 activated immediately on devnet
        consensus.BIP65Height = 1; // BIP65 activated immediately on devnet
        consensus.BIP66Height = 1; // BIP66 activated immediately on devnet
        consensus.BIP147Height = 1; // BIP147 activated immediately on devnet
        consensus.CSVHeight = 1; // BIP68 activated immediately on devnet
        consensus.DIP0001Height = 2; // DIP0001 activated immediately on devnet
        consensus.DIP0003Height = 2; // DIP0003 activated immediately on devnet
        consensus.DIP0003EnforcementHeight = 2; // DIP0003 activated immediately on devnet
        consensus.DIP0003EnforcementHash = uint256();
        consensus.DIP0008Height = 2; // DIP0008 activated immediately on devnet
        consensus.BRRHeight = 300;
        consensus.DIP0020Height = 300;
        consensus.DIP0024Height = 300;
        consensus.DIP0024QuorumsHeight = 300;
        consensus.V19Height = 300;
        consensus.MinBIP9WarningHeight = 300 + 1200; // v19 activation height + miner confirmation window
				consensus.EvonodeHeight = 2000;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 1
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Maximus: 1 day
        consensus.nPowTargetSpacing = 72; // Maximus: 72 seconds
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nPowDGWHeight = 60;
        consensus.nRuleChangeActivationThreshold = 900; // 75% for testchains
        consensus.nMinerConfirmationWindow = 1200; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        consensus.vDeployments[Consensus::DEPLOYMENT_V20].bit = 9;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nWindowSize = 120;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nThresholdStart = 80; // 80% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nThresholdMin = 60;   // 60% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nFalloffCoeff = 5;     // this corresponds to 10 periods

        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].bit = 10;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nWindowSize = 120;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nThresholdStart = 80; // 80% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nThresholdMin = 60;   // 60% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nFalloffCoeff = 5;     // this corresponds to 10 periods
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].useEHF = true;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000000000000000000");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x000000000000000000000000000000000000000000000000000000000000000");

        // AuxPoW parameters
        consensus.nAuxpowChainId = 0x0045;
        consensus.fStrictChainId = false;

        pchMessageStart[0] = 0x64; // d
        pchMessageStart[1] = 0x4d; // M
        pchMessageStart[2] = 0x41; // A
        pchMessageStart[3] = 0x58; // X
        nDefaultPort = 19769;
        nDefaultPlatformP2PPort = 22100;
        nDefaultPlatformHTTPPort = 22101;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        UpdateDevnetSubsidyAndDiffParametersFromArgs(args);
        genesis = CreateGenesisBlock(1729976223, 0, 0x207fffff, 1, 1 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock == uint256S("0x781ba374cf40ff2051b0af34f5d4f0437dc58102bb9a263be2cd13c232f3b3f3"));
        assert(genesis.hashMerkleRoot == uint256S("0xef49e9579b9b5aaf4b1b5b91c391d49aeaec81e582beecdd04dbf9fe2c7d6337"));

        devnetGenesis = FindDevNetGenesisBlock(genesis, 50 * COIN);
        consensus.hashDevnetGenesisBlock = devnetGenesis.GetHash();

        vFixedSeeds.clear();
        vSeeds.clear();
        //vSeeds.push_back(CDNSSeedData("maximuschain.com",  "devnet-seed.maximuschain.com"));

        // Devnet Maximus addresses start with 'm'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,110);
        // Devnet Maximus script addresses start with '5' or '6'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,12);
        // Devnet private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Devnet Maximus BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        // Devnet Maximus BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // Devnet Maximus BIP44 coin type is '1' (All coin's devnet default)
        nExtCoinType = 1;

        // long living quorum params
        consensus.llmqs[Consensus::LLMQ_50_60] = Consensus::llmq_50_60;
        consensus.llmqs[Consensus::LLMQ_60_75] = Consensus::llmq_60_75;
        consensus.llmqs[Consensus::LLMQ_400_60] = Consensus::llmq_20_60;
        consensus.llmqs[Consensus::LLMQ_400_85] = Consensus::llmq_20_85;
        consensus.llmqs[Consensus::LLMQ_100_67] = Consensus::llmq_100_67;
        consensus.llmqs[Consensus::LLMQ_DEVNET] = Consensus::llmq_devnet;
        consensus.llmqs[Consensus::LLMQ_DEVNET_DIP0024] = Consensus::llmq_devnet_dip0024;
        consensus.llmqs[Consensus::LLMQ_DEVNET_PLATFORM] = Consensus::llmq_devnet_platform;
        consensus.llmqTypeChainLocks = Consensus::LLMQ_DEVNET;
        consensus.llmqTypeDIP0024InstantSend = Consensus::LLMQ_DEVNET_DIP0024;
        consensus.llmqTypePlatform = Consensus::LLMQ_DEVNET_PLATFORM;
        consensus.llmqTypeMnhf = Consensus::LLMQ_DEVNET;

        UpdateDevnetLLMQChainLocksFromArgs(args);
        UpdateDevnetLLMQInstantSendDIP0024FromArgs(args);
        UpdateDevnetLLMQPlatformFromArgs(args);
        UpdateDevnetLLMQMnhfFromArgs(args);
        UpdateLLMQDevnetParametersFromArgs(args);
        UpdateDevnetPowTargetSpacingFromArgs(args);

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fRequireRoutableExternalIP = true;
        m_is_test_chain = true;
        fAllowMultipleAddressesFromGroup = true;
        fAllowMultiplePorts = true;
        nLLMQConnectionRetryTimeout = 60;
        m_is_mockable_chain = false;

        nPoolMinParticipants = 2;
        nPoolMaxParticipants = 20;
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes

        // privKey: cQ4wWXtT5KyUij1W8EZ62rv1ZjPdBKMCwBEqE8FyHZMY1PtMQpX5
        vSporkAddresses = {"mMaFubnqzVmgcv6WQaGY54De1AHtu2Cf4N"};
        nMinSporkKeys = 1;

        // privKey: cMpZFsdby3atTn88escMgKBAFBB47u9hbP7Trd31WYyroViJhRid
        std::vector<DevfeeRewardStructure> rewardStructures = {  {INT_MAX, 17}  }; // 5% dev fee
        consensus.nDevfeePayment = DevfeePayment(rewardStructures, 200, "mKAa421v424nLG8M217gtr8RAQVQxzsCzH");

        checkpointData = (CCheckpointData) {
            {
                { 0, uint256S("0x781ba374cf40ff2051b0af34f5d4f0437dc58102bb9a263be2cd13c232f3b3f3")},
                { 1, devnetGenesis.GetHash() },
            }
        };

        chainTxData = ChainTxData{
            devnetGenesis.GetBlockTime(), // * UNIX timestamp of devnet genesis block
            2,                            // * we only have 2 coinbase transactions when a devnet is started up
            0.01                          // * estimated number of transactions per second
        };
    }

    /**
     * Allows modifying the subsidy and difficulty devnet parameters.
     */
    void UpdateDevnetSubsidyAndDiffParameters(int nMinimumDifficultyBlocks, int nHighSubsidyBlocks, int nHighSubsidyFactor)
    {
        consensus.nMinimumDifficultyBlocks = nMinimumDifficultyBlocks;
        consensus.nHighSubsidyBlocks = nHighSubsidyBlocks;
        consensus.nHighSubsidyFactor = nHighSubsidyFactor;
    }
    void UpdateDevnetSubsidyAndDiffParametersFromArgs(const ArgsManager& args);

    /**
     * Allows modifying the LLMQ type for ChainLocks.
     */
    void UpdateDevnetLLMQChainLocks(Consensus::LLMQType llmqType)
    {
        consensus.llmqTypeChainLocks = llmqType;
    }
    void UpdateDevnetLLMQChainLocksFromArgs(const ArgsManager& args);

    /**
     * Allows modifying the LLMQ type for InstantSend (DIP0024).
     */
    void UpdateDevnetLLMQDIP0024InstantSend(Consensus::LLMQType llmqType)
    {
        consensus.llmqTypeDIP0024InstantSend = llmqType;
    }

    /**
     * Allows modifying the LLMQ type for Platform.
     */
    void UpdateDevnetLLMQPlatform(Consensus::LLMQType llmqType)
    {
        consensus.llmqTypePlatform = llmqType;
    }

    /**
     * Allows modifying the LLMQ type for Mnhf.
     */
    void UpdateDevnetLLMQMnhf(Consensus::LLMQType llmqType)
    {
        consensus.llmqTypeMnhf = llmqType;
    }

    /**
     * Allows modifying PowTargetSpacing
     */
    void UpdateDevnetPowTargetSpacing(int64_t nPowTargetSpacing)
    {
        consensus.nPowTargetSpacing = nPowTargetSpacing;
    }

    /**
     * Allows modifying parameters of the devnet LLMQ
     */
    void UpdateLLMQDevnetParameters(int size, int threshold)
    {
        auto params = consensus.llmqs.find(Consensus::LLMQ_DEVNET);
        assert(params != consensus.llmqs.end());
        params->second.size = size;
        params->second.minSize = threshold;
        params->second.threshold = threshold;
        params->second.dkgBadVotesThreshold = threshold;
    }
    void UpdateLLMQDevnetParametersFromArgs(const ArgsManager& args);
    void UpdateDevnetLLMQInstantSendFromArgs(const ArgsManager& args);
    void UpdateDevnetLLMQInstantSendDIP0024FromArgs(const ArgsManager& args);
    void UpdateDevnetLLMQPlatformFromArgs(const ArgsManager& args);
    void UpdateDevnetLLMQMnhfFromArgs(const ArgsManager& args);
    void UpdateDevnetPowTargetSpacingFromArgs(const ArgsManager& args);
};

/**
 * Regression test: intended for private networks only. Has minimal difficulty to ensure that
 * blocks can be found instantly.
 */
class CRegTestParams : public CChainParams {
public:
    explicit CRegTestParams(const ArgsManager& args) {
        strNetworkID =  CBaseChainParams::REGTEST;
        consensus.nSubsidyHalvingInterval = 1200; // ~ one day
        consensus.BIP16Height = 0; // always enforce P2SH BIP16 on regtest
        consensus.nMasternodePaymentsStartBlock = 250;
        consensus.nInstantSendConfirmationsRequired = 6;
        consensus.nInstantSendKeepLock = 24;
        consensus.nBudgetPaymentsStartBlock = 250;
        consensus.nBudgetPaymentsCycleBlocks = 250;
        consensus.nBudgetPaymentsWindowBlocks = 100;
        consensus.nSuperblockStartBlock = 1800; // ~ 1.5 days;
        consensus.nSuperblockStartHash = uint256(); // do not check this on regtest
        consensus.nSuperblockCycle = 1200; // ~ one day
        consensus.nSuperblockMaturityWindow = 120;
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 100;
        consensus.nMasternodeMinimumConfirmations = 1;
        consensus.BIP34Height = 20; // BIP34 activated on regtest (Used in functional tests)
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 40; // BIP65 activated on regtest (Used in functional tests)
        consensus.BIP66Height = 20; // BIP66 activated on regtest (Used in functional tests)
        consensus.BIP147Height = 10; // BIP147 activated on regtest (Used in functional tests)
        consensus.CSVHeight = 10; // CSV activated on regtest (Used in rpc activation tests)
        consensus.DIP0001Height = 10;
        consensus.DIP0003Height = 20;
        consensus.DIP0003EnforcementHeight = 30;
        consensus.DIP0003EnforcementHash = uint256();
        consensus.DIP0008Height = 20;
        consensus.BRRHeight = 999999; // see block_reward_reallocation_tests
        consensus.DIP0020Height = 150;
        consensus.DIP0024Height = 350;
        consensus.DIP0024QuorumsHeight = 350;
        consensus.V19Height = 350;
        consensus.MinBIP9WarningHeight = 0;
				consensus.EvonodeHeight = 200;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 1
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Maximus: 1 day
        consensus.nPowTargetSpacing = 72; // Maximus: 72 seconds
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nPowDGWHeight = 60; // same as mainnet
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        consensus.vDeployments[Consensus::DEPLOYMENT_V20].bit = 9;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nWindowSize = 400;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nThresholdStart = 384; // 80% of 480
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nThresholdMin = 288;   // 60% of 480
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nFalloffCoeff = 5;     // this corresponds to 10 periods

        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].bit = 10;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nWindowSize = 12;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nThresholdStart = 9; // 80% of 12
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nThresholdMin = 7;   // 60% of 7
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nFalloffCoeff = 5;     // this corresponds to 10 periods
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].useEHF = true;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        // AuxPoW parameters
        consensus.nAuxpowChainId = 0x0045;
        consensus.fStrictChainId = true;

        pchMessageStart[0] = 0x72; // r
        pchMessageStart[1] = 0x4d; // M
        pchMessageStart[2] = 0x41; // A
        pchMessageStart[3] = 0x58; // X
        nDefaultPort = 19869;
        nDefaultPlatformP2PPort = 22200;
        nDefaultPlatformHTTPPort = 22201;
        nPruneAfterHeight = gArgs.GetBoolArg("-fastprune", false) ? 100 : 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        UpdateActivationParametersFromArgs(args);
        UpdateDIP3ParametersFromArgs(args);
        UpdateDIP8ParametersFromArgs(args);
        UpdateBudgetParametersFromArgs(args);

        genesis = CreateGenesisBlock(1417713337, 0, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock == uint256S("0x5916643204422cd3de320273fe7568d921ae43e858c271d17f39fa8f15e21282"));
        assert(genesis.hashMerkleRoot == uint256S("0x1c2da9786370d27309c45314f0137207c36b9a8a2524de63938128e40afcd427"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = true;
        fRequireRoutableExternalIP = false;
        m_is_test_chain = true;
        fAllowMultipleAddressesFromGroup = true;
        fAllowMultiplePorts = true;
        nLLMQConnectionRetryTimeout = 1; // must be lower then the LLMQ signing session timeout so that tests have control over failing behavior
        m_is_mockable_chain = true;

        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes
        nPoolMinParticipants = 2;
        nPoolMaxParticipants = 20;

        // privKey: cQ4wWXtT5KyUij1W8EZ62rv1ZjPdBKMCwBEqE8FyHZMY1PtMQpX5
        vSporkAddresses = {"mMaFubnqzVmgcv6WQaGY54De1AHtu2Cf4N"};
        nMinSporkKeys = 1;

        // privKey: cMpZFsdby3atTn88escMgKBAFBB47u9hbP7Trd31WYyroViJhRid
        std::vector<DevfeeRewardStructure> rewardStructures = {  {INT_MAX, 17}  }; // 5% dev fee
        consensus.nDevfeePayment = DevfeePayment(rewardStructures, 50, "mKAa421v424nLG8M217gtr8RAQVQxzsCzH");

        checkpointData = {
            {
                {0, uint256S("0x5916643204422cd3de320273fe7568d921ae43e858c271d17f39fa8f15e21282")},
            }
        };

        m_assumeutxo_data = MapAssumeutxo{
            {
                110,
                {AssumeutxoHash{uint256S("0x927d43dbac32e62d607da7e2f1c12274cd30c341eae38a7e6019083c655c7145")}, 170}, // Maximus regtest: hash+coin-count regenerated (devfee adds a 2nd coinbase UTXO past height 50)
            },
            {
                210,
                {AssumeutxoHash{uint256S("0xd4c97d32882583b057efc3dce673e44204851435e6ffcef20346e69cddc7c91e")}, 210}, // TODO: inherited Dash value, not yet regenerated for Maximus (no test exercises height 210 currently)
            },
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        // Regtest Maximus addresses start with 'm'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,110);
        // Regtest Maximus script addresses start with '5' or '6'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,12);
        // Regtest private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Regtest Maximus BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        // Regtest Maximus BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // Regtest Maximus BIP44 coin type is '1' (All coin's testnet default)
        nExtCoinType = 1;

        // long living quorum params
        consensus.llmqs[Consensus::LLMQ_TEST] = Consensus::llmq_test;
        consensus.llmqs[Consensus::LLMQ_TEST_INSTANTSEND] = Consensus::llmq_test_instantsend;
        consensus.llmqs[Consensus::LLMQ_TEST_V17] = Consensus::llmq_test_v17;
        consensus.llmqs[Consensus::LLMQ_TEST_DIP0024] = Consensus::llmq_test_dip0024;
        consensus.llmqs[Consensus::LLMQ_TEST_PLATFORM] = Consensus::llmq_test_platform;
        consensus.llmqTypeChainLocks = Consensus::LLMQ_TEST;
        consensus.llmqTypeDIP0024InstantSend = Consensus::LLMQ_TEST_DIP0024;
        consensus.llmqTypePlatform = Consensus::LLMQ_TEST_PLATFORM;
        consensus.llmqTypeMnhf = Consensus::LLMQ_TEST;

        UpdateLLMQTestParametersFromArgs(args, Consensus::LLMQ_TEST);
        UpdateLLMQTestParametersFromArgs(args, Consensus::LLMQ_TEST_INSTANTSEND);
        UpdateLLMQInstantSendDIP0024FromArgs(args);
    }

    /**
     * Allows modifying the Version Bits regtest parameters.
     */
    void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout, int64_t nWindowSize, int64_t nThresholdStart, int64_t nThresholdMin, int64_t nFalloffCoeff, int64_t nUseEHF)
    {
        consensus.vDeployments[d].nStartTime = nStartTime;
        consensus.vDeployments[d].nTimeout = nTimeout;
        if (nWindowSize != -1) {
            consensus.vDeployments[d].nWindowSize = nWindowSize;
        }
        if (nThresholdStart != -1) {
            consensus.vDeployments[d].nThresholdStart = nThresholdStart;
        }
        if (nThresholdMin != -1) {
            consensus.vDeployments[d].nThresholdMin = nThresholdMin;
        }
        if (nFalloffCoeff != -1) {
            consensus.vDeployments[d].nFalloffCoeff = nFalloffCoeff;
        }
        if (nUseEHF != -1) {
            consensus.vDeployments[d].useEHF = nUseEHF > 0;
        }
    }
    void UpdateActivationParametersFromArgs(const ArgsManager& args);

    /**
     * Allows modifying the DIP3 activation and enforcement height
     */
    void UpdateDIP3Parameters(int nActivationHeight, int nEnforcementHeight)
    {
        consensus.DIP0003Height = nActivationHeight;
        consensus.DIP0003EnforcementHeight = nEnforcementHeight;
    }
    void UpdateDIP3ParametersFromArgs(const ArgsManager& args);

    /**
     * Allows modifying the DIP8 activation height
     */
    void UpdateDIP8Parameters(int nActivationHeight)
    {
        consensus.DIP0008Height = nActivationHeight;
    }
    void UpdateDIP8ParametersFromArgs(const ArgsManager& args);

    /**
     * Allows modifying the budget regtest parameters.
     */
    void UpdateBudgetParameters(int nMasternodePaymentsStartBlock, int nBudgetPaymentsStartBlock, int nSuperblockStartBlock)
    {
        consensus.nMasternodePaymentsStartBlock = nMasternodePaymentsStartBlock;
        consensus.nBudgetPaymentsStartBlock = nBudgetPaymentsStartBlock;
        consensus.nSuperblockStartBlock = nSuperblockStartBlock;
    }
    void UpdateBudgetParametersFromArgs(const ArgsManager& args);

    /**
     * Allows modifying parameters of the test LLMQ
     */
    void UpdateLLMQTestParameters(int size, int threshold, const Consensus::LLMQType llmqType)
    {
        auto params = consensus.llmqs.find(llmqType);
        assert(params != consensus.llmqs.end());
        params->second.size = size;
        params->second.minSize = threshold;
        params->second.threshold = threshold;
        params->second.dkgBadVotesThreshold = threshold;
    }

    /**
     * Allows modifying the LLMQ type for InstantSend (DIP0024).
     */
    void UpdateLLMQDIP0024InstantSend(Consensus::LLMQType llmqType)
    {
        consensus.llmqTypeDIP0024InstantSend = llmqType;
    }

    void UpdateLLMQTestParametersFromArgs(const ArgsManager& args, const Consensus::LLMQType llmqType);
    void UpdateLLMQInstantSendDIP0024FromArgs(const ArgsManager& args);
};

void CRegTestParams::UpdateActivationParametersFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-vbparams")) return;

    for (const std::string& strDeployment : args.GetArgs("-vbparams")) {
        std::vector<std::string> vDeploymentParams = SplitString(strDeployment, ':');
        if (vDeploymentParams.size() != 3 && vDeploymentParams.size() != 5 && vDeploymentParams.size() != 8) {
            throw std::runtime_error("Version bits parameters malformed, expecting "
                    "<deployment>:<start>:<end> or "
                    "<deployment>:<start>:<end>:<window>:<threshold> or "
                    "<deployment>:<start>:<end>:<window>:<thresholdstart>:<thresholdmin>:<falloffcoeff>:<useehf>");
        }
        int64_t nStartTime, nTimeout, nWindowSize = -1, nThresholdStart = -1, nThresholdMin = -1, nFalloffCoeff = -1, nUseEHF = -1;
        if (!ParseInt64(vDeploymentParams[1], &nStartTime)) {
            throw std::runtime_error(strprintf("Invalid nStartTime (%s)", vDeploymentParams[1]));
        }
        if (!ParseInt64(vDeploymentParams[2], &nTimeout)) {
            throw std::runtime_error(strprintf("Invalid nTimeout (%s)", vDeploymentParams[2]));
        }
        if (vDeploymentParams.size() >= 5) {
            if (!ParseInt64(vDeploymentParams[3], &nWindowSize)) {
                throw std::runtime_error(strprintf("Invalid nWindowSize (%s)", vDeploymentParams[3]));
            }
            if (!ParseInt64(vDeploymentParams[4], &nThresholdStart)) {
                throw std::runtime_error(strprintf("Invalid nThresholdStart (%s)", vDeploymentParams[4]));
            }
        }
        if (vDeploymentParams.size() == 8) {
            if (!ParseInt64(vDeploymentParams[5], &nThresholdMin)) {
                throw std::runtime_error(strprintf("Invalid nThresholdMin (%s)", vDeploymentParams[5]));
            }
            if (!ParseInt64(vDeploymentParams[6], &nFalloffCoeff)) {
                throw std::runtime_error(strprintf("Invalid nFalloffCoeff (%s)", vDeploymentParams[6]));
            }
            if (!ParseInt64(vDeploymentParams[7], &nUseEHF)) {
                throw std::runtime_error(strprintf("Invalid nUseEHF (%s)", vDeploymentParams[7]));
            }
        }
        bool found = false;
        for (int j=0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j) {
            if (vDeploymentParams[0] == VersionBitsDeploymentInfo[j].name) {
                UpdateVersionBitsParameters(Consensus::DeploymentPos(j), nStartTime, nTimeout, nWindowSize, nThresholdStart, nThresholdMin, nFalloffCoeff, nUseEHF);
                found = true;
                LogPrintf("Setting version bits activation parameters for %s to start=%ld, timeout=%ld, window=%ld, thresholdstart=%ld, thresholdmin=%ld, falloffcoeff=%ld, useehf=%ld\n",
                          vDeploymentParams[0], nStartTime, nTimeout, nWindowSize, nThresholdStart, nThresholdMin, nFalloffCoeff, nUseEHF);
                break;
            }
        }
        if (!found) {
            throw std::runtime_error(strprintf("Invalid deployment (%s)", vDeploymentParams[0]));
        }
    }
}

void CRegTestParams::UpdateDIP3ParametersFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-dip3params")) return;

    std::string strParams = args.GetArg("-dip3params", "");
    std::vector<std::string> vParams = SplitString(strParams, ':');
    if (vParams.size() != 2) {
        throw std::runtime_error("DIP3 parameters malformed, expecting <activation>:<enforcement>");
    }
    int nDIP3ActivationHeight, nDIP3EnforcementHeight;
    if (!ParseInt32(vParams[0], &nDIP3ActivationHeight)) {
        throw std::runtime_error(strprintf("Invalid activation height (%s)", vParams[0]));
    }
    if (!ParseInt32(vParams[1], &nDIP3EnforcementHeight)) {
        throw std::runtime_error(strprintf("Invalid enforcement height (%s)", vParams[1]));
    }
    LogPrintf("Setting DIP3 parameters to activation=%ld, enforcement=%ld\n", nDIP3ActivationHeight, nDIP3EnforcementHeight);
    UpdateDIP3Parameters(nDIP3ActivationHeight, nDIP3EnforcementHeight);
}

void CRegTestParams::UpdateDIP8ParametersFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-dip8params")) return;

    std::string strParams = args.GetArg("-dip8params", "");
    std::vector<std::string> vParams = SplitString(strParams, ':');
    if (vParams.size() != 1) {
        throw std::runtime_error("DIP8 parameters malformed, expecting <activation>");
    }
    int nDIP8ActivationHeight;
    if (!ParseInt32(vParams[0], &nDIP8ActivationHeight)) {
        throw std::runtime_error(strprintf("Invalid activation height (%s)", vParams[0]));
    }
    LogPrintf("Setting DIP8 parameters to activation=%ld\n", nDIP8ActivationHeight);
    UpdateDIP8Parameters(nDIP8ActivationHeight);
}

void CRegTestParams::UpdateBudgetParametersFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-budgetparams")) return;

    std::string strParams = args.GetArg("-budgetparams", "");
    std::vector<std::string> vParams = SplitString(strParams, ':');
    if (vParams.size() != 3) {
        throw std::runtime_error("Budget parameters malformed, expecting <masternode>:<budget>:<superblock>");
    }
    int nMasternodePaymentsStartBlock, nBudgetPaymentsStartBlock, nSuperblockStartBlock;
    if (!ParseInt32(vParams[0], &nMasternodePaymentsStartBlock)) {
        throw std::runtime_error(strprintf("Invalid masternode start height (%s)", vParams[0]));
    }
    if (!ParseInt32(vParams[1], &nBudgetPaymentsStartBlock)) {
        throw std::runtime_error(strprintf("Invalid budget start block (%s)", vParams[1]));
    }
    if (!ParseInt32(vParams[2], &nSuperblockStartBlock)) {
        throw std::runtime_error(strprintf("Invalid superblock start height (%s)", vParams[2]));
    }
    LogPrintf("Setting budget parameters to masternode=%ld, budget=%ld, superblock=%ld\n", nMasternodePaymentsStartBlock, nBudgetPaymentsStartBlock, nSuperblockStartBlock);
    UpdateBudgetParameters(nMasternodePaymentsStartBlock, nBudgetPaymentsStartBlock, nSuperblockStartBlock);
}

void CRegTestParams::UpdateLLMQTestParametersFromArgs(const ArgsManager& args, const Consensus::LLMQType llmqType)
{
    assert(llmqType == Consensus::LLMQ_TEST || llmqType == Consensus::LLMQ_TEST_INSTANTSEND);

    std::string cmd_param{"-llmqtestparams"}, llmq_name{"LLMQ_TEST"};
    if (llmqType == Consensus::LLMQ_TEST_INSTANTSEND) {
        cmd_param = "-llmqtestinstantsendparams";
        llmq_name = "LLMQ_TEST_INSTANTSEND";
    }

    if (!args.IsArgSet(cmd_param)) return;

    std::string strParams = args.GetArg(cmd_param, "");
    std::vector<std::string> vParams = SplitString(strParams, ':');
    if (vParams.size() != 2) {
        throw std::runtime_error(strprintf("%s parameters malformed, expecting <size>:<threshold>", llmq_name));
    }
    int size, threshold;
    if (!ParseInt32(vParams[0], &size)) {
        throw std::runtime_error(strprintf("Invalid %s size (%s)", llmq_name, vParams[0]));
    }
    if (!ParseInt32(vParams[1], &threshold)) {
        throw std::runtime_error(strprintf("Invalid %s threshold (%s)", llmq_name, vParams[1]));
    }
    LogPrintf("Setting %s parameters to size=%ld, threshold=%ld\n", llmq_name, size, threshold);
    UpdateLLMQTestParameters(size, threshold, llmqType);
}

void CRegTestParams::UpdateLLMQInstantSendDIP0024FromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-llmqtestinstantsenddip0024")) return;

    const auto& llmq_params_opt = GetLLMQ(consensus.llmqTypeDIP0024InstantSend);
    assert(llmq_params_opt.has_value());

    std::string strLLMQType = gArgs.GetArg("-llmqtestinstantsenddip0024", std::string(llmq_params_opt->name));

    Consensus::LLMQType llmqType = Consensus::LLMQ_NONE;
    for (const auto& params : consensus.llmqs) {
        if (params.second.name == strLLMQType) {
            llmqType = params.second.type;
        }
    }
    if (llmqType == Consensus::LLMQ_NONE) {
        throw std::runtime_error("Invalid LLMQ type specified for -llmqtestinstantsenddip0024.");
    }
    LogPrintf("Setting llmqtestinstantsenddip0024 to %ld\n", ToUnderlying(llmqType));
    UpdateLLMQDIP0024InstantSend(llmqType);
}

void CDevNetParams::UpdateDevnetSubsidyAndDiffParametersFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-minimumdifficultyblocks") && !args.IsArgSet("-highsubsidyblocks") && !args.IsArgSet("-highsubsidyfactor")) return;

    int nMinimumDifficultyBlocks = gArgs.GetArg("-minimumdifficultyblocks", consensus.nMinimumDifficultyBlocks);
    int nHighSubsidyBlocks = gArgs.GetArg("-highsubsidyblocks", consensus.nHighSubsidyBlocks);
    int nHighSubsidyFactor = gArgs.GetArg("-highsubsidyfactor", consensus.nHighSubsidyFactor);
    LogPrintf("Setting minimumdifficultyblocks=%ld, highsubsidyblocks=%ld, highsubsidyfactor=%ld\n", nMinimumDifficultyBlocks, nHighSubsidyBlocks, nHighSubsidyFactor);
    UpdateDevnetSubsidyAndDiffParameters(nMinimumDifficultyBlocks, nHighSubsidyBlocks, nHighSubsidyFactor);
}

void CDevNetParams::UpdateDevnetLLMQChainLocksFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-llmqchainlocks")) return;

    const auto& llmq_params_opt = GetLLMQ(consensus.llmqTypeChainLocks);
    assert(llmq_params_opt.has_value());

    std::string strLLMQType = gArgs.GetArg("-llmqchainlocks", std::string(llmq_params_opt->name));

    Consensus::LLMQType llmqType = Consensus::LLMQ_NONE;
    for (const auto& params : consensus.llmqs) {
        if (params.second.name == strLLMQType) {
            if (params.second.useRotation) {
                throw std::runtime_error("LLMQ type specified for -llmqchainlocks must NOT use rotation");
            }
            llmqType = params.second.type;
        }
    }
    if (llmqType == Consensus::LLMQ_NONE) {
        throw std::runtime_error("Invalid LLMQ type specified for -llmqchainlocks.");
    }
    LogPrintf("Setting llmqchainlocks to size=%ld\n", static_cast<uint8_t>(llmqType));
    UpdateDevnetLLMQChainLocks(llmqType);
}

void CDevNetParams::UpdateDevnetLLMQInstantSendDIP0024FromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-llmqinstantsenddip0024")) return;

    const auto& llmq_params_opt = GetLLMQ(consensus.llmqTypeDIP0024InstantSend);
    assert(llmq_params_opt.has_value());

    std::string strLLMQType = gArgs.GetArg("-llmqinstantsenddip0024", std::string(llmq_params_opt->name));

    Consensus::LLMQType llmqType = Consensus::LLMQ_NONE;
    for (const auto& params : consensus.llmqs) {
        if (params.second.name == strLLMQType) {
            if (!params.second.useRotation) {
                throw std::runtime_error("LLMQ type specified for -llmqinstantsenddip0024 must use rotation");
            }
            llmqType = params.second.type;
        }
    }
    if (llmqType == Consensus::LLMQ_NONE) {
        throw std::runtime_error("Invalid LLMQ type specified for -llmqinstantsenddip0024.");
    }
    LogPrintf("Setting llmqinstantsenddip0024 to size=%ld\n", static_cast<uint8_t>(llmqType));
    UpdateDevnetLLMQDIP0024InstantSend(llmqType);
}

void CDevNetParams::UpdateDevnetLLMQPlatformFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-llmqplatform")) return;

    const auto& llmq_params_opt = GetLLMQ(consensus.llmqTypePlatform);
    assert(llmq_params_opt.has_value());

    std::string strLLMQType = gArgs.GetArg("-llmqplatform", std::string(llmq_params_opt->name));

    Consensus::LLMQType llmqType = Consensus::LLMQ_NONE;
    for (const auto& params : consensus.llmqs) {
        if (params.second.name == strLLMQType) {
            llmqType = params.second.type;
        }
    }
    if (llmqType == Consensus::LLMQ_NONE) {
        throw std::runtime_error("Invalid LLMQ type specified for -llmqplatform.");
    }
    LogPrintf("Setting llmqplatform to size=%ld\n", static_cast<uint8_t>(llmqType));
    UpdateDevnetLLMQPlatform(llmqType);
}

void CDevNetParams::UpdateDevnetLLMQMnhfFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-llmqmnhf")) return;

    const auto& llmq_params_opt = GetLLMQ(consensus.llmqTypeMnhf);
    assert(llmq_params_opt.has_value());

    std::string strLLMQType = gArgs.GetArg("-llmqmnhf", std::string(llmq_params_opt->name));

    Consensus::LLMQType llmqType = Consensus::LLMQ_NONE;
    for (const auto& params : consensus.llmqs) {
        if (params.second.name == strLLMQType) {
            llmqType = params.second.type;
        }
    }
    if (llmqType == Consensus::LLMQ_NONE) {
        throw std::runtime_error("Invalid LLMQ type specified for -llmqmnhf.");
    }
    LogPrintf("Setting llmqmnhf to size=%ld\n", static_cast<uint8_t>(llmqType));
    UpdateDevnetLLMQMnhf(llmqType);
}

void CDevNetParams::UpdateDevnetPowTargetSpacingFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-powtargetspacing")) return;

    std::string strPowTargetSpacing = gArgs.GetArg("-powtargetspacing", "");

    int64_t powTargetSpacing;
    if (!ParseInt64(strPowTargetSpacing, &powTargetSpacing)) {
        throw std::runtime_error(strprintf("Invalid parsing of powTargetSpacing (%s)", strPowTargetSpacing));
    }

    if (powTargetSpacing < 1) {
        throw std::runtime_error(strprintf("Invalid value of powTargetSpacing (%s)", strPowTargetSpacing));
    }

    LogPrintf("Setting powTargetSpacing to %ld\n", powTargetSpacing);
    UpdateDevnetPowTargetSpacing(powTargetSpacing);
}

void CDevNetParams::UpdateLLMQDevnetParametersFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-llmqdevnetparams")) return;

    std::string strParams = args.GetArg("-llmqdevnetparams", "");
    std::vector<std::string> vParams = SplitString(strParams, ':');
    if (vParams.size() != 2) {
        throw std::runtime_error("LLMQ_DEVNET parameters malformed, expecting <size>:<threshold>");
    }
    int size, threshold;
    if (!ParseInt32(vParams[0], &size)) {
        throw std::runtime_error(strprintf("Invalid LLMQ_DEVNET size (%s)", vParams[0]));
    }
    if (!ParseInt32(vParams[1], &threshold)) {
        throw std::runtime_error(strprintf("Invalid LLMQ_DEVNET threshold (%s)", vParams[1]));
    }
    LogPrintf("Setting LLMQ_DEVNET parameters to size=%ld, threshold=%ld\n", size, threshold);
    UpdateLLMQDevnetParameters(size, threshold);
}

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<CChainParams> CreateChainParams(const ArgsManager& args, const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN) {
        return std::unique_ptr<CChainParams>(new CMainParams());
    } else if (chain == CBaseChainParams::TESTNET) {
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    } else if (chain == CBaseChainParams::DEVNET) {
        return std::unique_ptr<CChainParams>(new CDevNetParams(args));
    } else if (chain == CBaseChainParams::REGTEST) {
        return std::unique_ptr<CChainParams>(new CRegTestParams(args));
    }
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(gArgs, network);
}

void UpdateLLMQParams(size_t totalMnCount, int height) {
    globalChainParams->UpdateLLMQParams(totalMnCount, height);
}

bool IsMiningPhase(const Consensus::LLMQParams &params, int nHeight) {
    int phaseIndex = nHeight % params.dkgInterval;
    if (phaseIndex >= params.dkgMiningWindowStart && phaseIndex <= params.dkgMiningWindowEnd) {
        return true;
    }
    return false;
}

bool IsLLMQsMiningPhase(int nHeight) {
    for (auto &it: globalChainParams->GetConsensus().llmqs) {
        if (IsMiningPhase(it.second, nHeight)) {
            return true;
        }
    }
    return false;
}

void CChainParams::UpdateLLMQParams(size_t totalMnCount, int height) {
    if (Params().NetworkIDString() == CBaseChainParams::DEVNET || Params().NetworkIDString() == CBaseChainParams::REGTEST)
        return;

    bool isNotLLMQsMiningPhase;
    if ((lastCheckHeight < height && (lastCheckMnCount != totalMnCount) &&
        (isNotLLMQsMiningPhase = !IsLLMQsMiningPhase(height))) || lastCheckHeight == -1) {
        LogPrintf("---UpdateLLMQParams %d-%d-%ld-%ld-%d\n", lastCheckHeight, height, lastCheckMnCount, totalMnCount,
                  isNotLLMQsMiningPhase);
        lastCheckMnCount = totalMnCount;
        lastCheckHeight = height;
        bool isTestNet = strcmp(Params().NetworkIDString().c_str(), "test") == 0;
        if ((totalMnCount < 80 && isTestNet) || (totalMnCount < 100 && !isTestNet)) {
            consensus.llmqs[Consensus::LLMQ_50_60] = Consensus::llmq_10_60;
            consensus.llmqs[Consensus::LLMQ_60_75] = Consensus::llmq_10_75;
            consensus.llmqs[Consensus::LLMQ_400_60] = Consensus::llmq_20_60;
            consensus.llmqs[Consensus::LLMQ_400_85] = Consensus::llmq_20_85;
        } else if (totalMnCount < 200) {
            consensus.llmqs[Consensus::LLMQ_50_60] = Consensus::llmq_50_60;
            consensus.llmqs[Consensus::LLMQ_60_75] = Consensus::llmq_10_75;
            consensus.llmqs[Consensus::LLMQ_400_60] = Consensus::llmq_40_60;
            consensus.llmqs[Consensus::LLMQ_400_85] = Consensus::llmq_40_85;
        } else if (totalMnCount < 600) {
            consensus.llmqs[Consensus::LLMQ_50_60] = Consensus::llmq_50_60;
            consensus.llmqs[Consensus::LLMQ_60_75] = Consensus::llmq_20_75;
            consensus.llmqs[Consensus::LLMQ_400_60] = Consensus::llmq_40_60;
            consensus.llmqs[Consensus::LLMQ_400_85] = Consensus::llmq_40_85;
        } else {
            consensus.llmqs[Consensus::LLMQ_50_60] = Consensus::llmq_50_60;
            consensus.llmqs[Consensus::LLMQ_400_60] = Consensus::llmq_400_60;
            consensus.llmqs[Consensus::LLMQ_400_85] = Consensus::llmq_400_85;
            consensus.llmqs[Consensus::LLMQ_60_75] = Consensus::llmq_20_75;
            if (totalMnCount > 2000)
                consensus.llmqs[Consensus::LLMQ_60_75] = Consensus::llmq_60_75;
        }
    } else {
        if (totalMnCount < 80)
            consensus.llmqs[Consensus::LLMQ_60_75] = Consensus::llmq_10_75;
    }
}
