// Copyright (c) 2019 The DeVault developers
// Copyright (c) 2019 Jon Spock
// Copyright (c) 2026 The DeVault developers (V2 port)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <devault/rewards_calculation.h>

#include <consensus/params.h>

#include <cstdint>

Amount CalculateReward(const Consensus::Params &consensusParams, int Height, int HeightDiff,
                       Amount balance) {
    size_t year_number = Height / consensusParams.nBlocksPerYear;
    if (year_number > consensusParams.nPerCentPerYear.size() - 1) {
        year_number = consensusParams.nPerCentPerYear.size() - 1;
    }
    int64_t percent = consensusParams.nPerCentPerYear[year_number];
    int64_t nRewardRatePerBlockReciprocal = (100 * consensusParams.nBlocksPerYear) / percent;
    Amount nMinReward = consensusParams.nMinReward;
    // legacy `balance.toInt()` -> V2 `balance / SATOSHI` (both the raw satoshi int64)
    int64_t balance_int = balance / SATOSHI;
    int64_t reward_per_block = (balance_int / nRewardRatePerBlockReciprocal);
    // multiply by 100 so when quantized by the divide below the resolution is 1% of COIN.
    // legacy `Amount::COIN_PRECISION` (1e8) -> V2 `COIN / SATOSHI`.
    int64_t reward_x100 = (int64_t(100) * HeightDiff * reward_per_block) / (COIN / SATOSHI);
    // Now compensate for the multiply-by-100 and divide by COIN -> reward is a multiple of 0.01 COIN
    // (1e6 sat), which is spock-aligned (a multiple of 1e5 sat), so no further quantization is needed.
    Amount reward = reward_x100 * (COIN / 100);
    if (reward < nMinReward) {
        reward = Amount::zero();
    }
    return reward;
}
