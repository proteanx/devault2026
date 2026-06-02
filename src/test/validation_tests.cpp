// Copyright (c) 2011-2019 The Bitcoin Core developers
// Copyright (c) 2017-2021 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation.h>

#include <chainparams.h>
#include <clientversion.h>
#include <config.h>
#include <consensus/consensus.h>
#include <net.h>
#include <primitives/transaction.h>
#include <streams.h>
#include <util/system.h>

#include <test/setup_common.h>

#include <boost/signals2/signal.hpp>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <cstdio>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(validation_tests, TestingSetup)

// DeVault uses the "Shark" inflation curve (1G), NOT Bitcoin halving: the subsidy starts at the
// initial mining reward, ramps to a 1.5x peak at half a year, then decays ~1/height and stays
// positive forever (DeVault is perpetually inflationary). Validate those properties against the
// real chainparams. The Shark curve is also validated end-to-end by M2 (total_amount over all
// 1,696,136 blocks matches the oracle exactly).
BOOST_AUTO_TEST_CASE(block_subsidy_shark_curve_test) {
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    const Consensus::Params &cp = chainParams->GetConsensus();
    const int64_t initial = cp.nInitialMiningRewardInCoins;
    const int64_t peakHeight = cp.nBlocksPerYear / 2; // peak at half a year
    const Amount initialSubsidy = int64_t(initial) * COIN;
    const Amount peakSubsidy = int64_t(initial + initial / 2) * COIN; // 1.5x

    // Genesis-era subsidy is exactly the initial reward.
    BOOST_CHECK_EQUAL(GetBlockSubsidy(0, cp), initialSubsidy);
    // Ramps up to a 1.5x peak at half a year.
    BOOST_CHECK(GetBlockSubsidy(peakHeight / 2, cp) > initialSubsidy);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(peakHeight, cp), peakSubsidy);
    // Decays after the peak (strictly decreasing once past it).
    BOOST_CHECK(GetBlockSubsidy(2 * peakHeight, cp) < peakSubsidy);
    BOOST_CHECK(GetBlockSubsidy(10 * peakHeight, cp) <
                GetBlockSubsidy(2 * peakHeight, cp));

    // Positive, bounded and crash-free at every height the chain may ever reach
    // (the curve never halves to zero, unlike Bitcoin).
    for (int64_t h : {int64_t(1), int64_t(1000), int64_t(100000),
                      int64_t(1000000), int64_t(5000000), int64_t(14000000)}) {
        const Amount s = GetBlockSubsidy(h, cp);
        BOOST_CHECK(s > Amount::zero());
        BOOST_CHECK(MoneyRange(s));
    }
}

static CBlock makeLargeDummyBlock(const size_t num_tx) {
    CBlock block;
    block.vtx.reserve(num_tx);

    for (size_t i = 0; i < num_tx; i++) {
        block.vtx.push_back(MakeTransactionRef());
    }
    return block;
}

/**
 * Test that LoadExternalBlockFile works with the buffer size set below the
 * size of a large block. Currently, LoadExternalBlockFile has the buffer size
 * for CBufferedFile set to 2 * MAX_TX_SIZE. Test with a value of
 * 10 * MAX_TX_SIZE.
 */
BOOST_AUTO_TEST_CASE(validation_load_external_block_file) {
    fs::path tmpfile_name =
        SetDataDir("validation_load_external_block_file") / "block.dat";

    FILE *fp = fopen(tmpfile_name.string().c_str(), "wb+");

    BOOST_CHECK(fp != nullptr);

    const Config &config = GetConfig();
    const CChainParams &chainparams = config.GetChainParams();

    // serialization format is:
    // message start magic, size of block, block

    size_t nwritten = fwrite(std::begin(chainparams.DiskMagic()),
                             CMessageHeader::MESSAGE_START_SIZE, 1, fp);

    BOOST_CHECK_EQUAL(nwritten, 1UL);

    const auto &empty_tx = CTransaction::null;
    size_t empty_tx_size = GetSerializeSize(empty_tx, CLIENT_VERSION);

    size_t num_tx = (10 * MAX_TX_SIZE) / empty_tx_size;

    CBlock block = makeLargeDummyBlock(num_tx);

    BOOST_CHECK(GetSerializeSize(block, CLIENT_VERSION) > 2 * MAX_TX_SIZE);

    unsigned int size = GetSerializeSize(block, CLIENT_VERSION);
    {
        CAutoFile outs(fp, SER_DISK, CLIENT_VERSION);
        outs << size;
        outs << block;
        outs.release();
    }

    fseek(fp, 0, SEEK_SET);
    BOOST_CHECK_NO_THROW({ LoadExternalBlockFile(config, fp, 0); });
}

BOOST_AUTO_TEST_SUITE_END()
