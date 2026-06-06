// Copyright (c) 2026 The DeVault developers (V2 port)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <devault/rewards_calculation.h>

#include <amount.h>
#include <consensus/params.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>

BOOST_FIXTURE_TEST_SUITE(coldrewards_tests, BasicTestingSetup)

// CalculateReward only reads these three fields; isolate the test from chainparams by setting them
// to the legacy DeVault mainnet values directly.
static Consensus::Params MakeRewardParams() {
    Consensus::Params p;
    p.nBlocksPerYear = 262980; // 30*24*365.25
    p.nPerCentPerYear = {15, 12, 9, 7, 5};
    p.nMinReward = 50 * COIN;
    return p;
}

// Reference values computed independently from the legacy integer formula (see DEVAULT_COLD_REWARDS_DESIGN.md §2.3).
BOOST_AUTO_TEST_CASE(calculate_reward_values) {
    const Consensus::Params p = MakeRewardParams();

    // year 0 (15%): 10000 coins held ~1 period -> 125 COIN
    BOOST_CHECK_EQUAL(CalculateReward(p, 0, 21916, 10000 * COIN) / SATOSHI, int64_t(12'500'000'000));
    // below the 50-COIN minimum -> floored to zero
    BOOST_CHECK_EQUAL(CalculateReward(p, 0, 21916, 1000 * COIN) / SATOSHI, int64_t(0));
    // longer hold, bigger balance -> 624.99 COIN (note the 0.01-COIN quantization)
    BOOST_CHECK_EQUAL(CalculateReward(p, 0, 43830, 25000 * COIN) / SATOSHI, int64_t(62'499'000'000));
    // year 1 (12%, Height 300000 / 262980 == 1) -> 250.01 COIN
    BOOST_CHECK_EQUAL(CalculateReward(p, 300000, 21916, 25000 * COIN) / SATOSHI,
                      int64_t(25'001'000'000));
    // beyond year 4 -> clamps to the 5% tier -> 416.68 COIN
    BOOST_CHECK_EQUAL(CalculateReward(p, 1500000, 21916, 100000 * COIN) / SATOSHI,
                      int64_t(41'668'000'000));
    // exactly at the 50-COIN minimum boundary (paid, not floored)
    BOOST_CHECK_EQUAL(CalculateReward(p, 22377, 21916, 4000 * COIN) / SATOSHI, int64_t(5'000'000'000));
}

BOOST_AUTO_TEST_CASE(calculate_reward_invariants) {
    const Consensus::Params p = MakeRewardParams();

    // A negligible hold is floored to zero.
    BOOST_CHECK(CalculateReward(p, 0, 1, 1000 * COIN) == Amount::zero());

    // Every non-zero reward is a multiple of 0.01 COIN (1e6 sat), hence spock-aligned.
    const int64_t r = CalculateReward(p, 0, 43830, 25000 * COIN) / SATOSHI;
    BOOST_CHECK(r > 0);
    BOOST_CHECK_EQUAL(r % 1'000'000, int64_t(0));

    // Year-index clamping: a height far past year 4 uses the same (5%) tier as year 4.
    const Amount rFar = CalculateReward(p, 100 * 262980, 21916, 100000 * COIN);
    const Amount rYr4 = CalculateReward(p, 4 * 262980, 21916, 100000 * COIN);
    BOOST_CHECK(rFar == rYr4);
    BOOST_CHECK(rFar > Amount::zero());
}

BOOST_AUTO_TEST_SUITE_END()
