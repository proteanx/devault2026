// Copyright (c) 2019 The DeVault developers
// Copyright (c) 2019 Jon Spock
// Copyright (c) 2026 The DeVault developers (V2 port)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEVAULT_DEVAULT_REWARDS_CALCULATION_H
#define DEVAULT_DEVAULT_REWARDS_CALCULATION_H

#include <amount.h>

namespace Consensus {
struct Params;
}

/**
 * The cold-reward amount for a UTXO (ported verbatim from legacy DeVault devault/rewards_calculation.cpp).
 *
 * Consensus-critical and must reproduce historical payouts bit-for-bit.
 *
 *   year      = clamp(Height / nBlocksPerYear, 0, nPerCentPerYear.size()-1)
 *   percent   = nPerCentPerYear[year]                          // {15,12,9,7,5}
 *   recip     = (100 * nBlocksPerYear) / percent
 *   reward    = ((100 * HeightDiff * (balance/recip)) / 1e8) * (COIN/100)   // quantized to 0.01 COIN
 *   if reward < nMinReward -> 0
 *
 * `Height` is the UTXO's own height (creation or last-payout), NOT the current block height (a v1.0.2
 * change). `HeightDiff = currentHeight - UTXO.height`.
 *
 * IMPORTANT (V2 spock semantics, see DEVAULT_COLD_REWARDS_DESIGN.md / 1F): legacy `Amount` quantized
 * every value UP to a spock (0.001 DVT) on copy, so `balance` here was always spock-quantized. V2 keeps
 * BCHN's satoshi-granular `Amount`, so the CALLER must pass the spock-quantized balance (i.e. the value
 * as it appears in the UTXO set, which 1F quantizes on AddCoins). This function is otherwise verbatim;
 * legacy `balance.toInt()` becomes `balance / SATOSHI`, and `Amount::COIN_PRECISION` (1e8) becomes
 * `COIN / SATOSHI`.
 */
Amount CalculateReward(const Consensus::Params &consensusParams, int Height, int HeightDiff,
                       Amount balance);

#endif // DEVAULT_DEVAULT_REWARDS_CALCULATION_H
