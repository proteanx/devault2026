// Copyright (c) 2019 The DeVault developers
// Copyright (c) 2019 Jon Spock
// Copyright (c) 2026 The DeVault developers (V2 port)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef DEVAULT_DEVAULT_BUDGET_H
#define DEVAULT_DEVAULT_BUDGET_H

#include <amount.h>

class CBlock;
class CChainParams;

/**
 * DeVault budget (ported from legacy DeVault devault/budget.cpp).
 *
 * On a "superblock" (every nBlocksPerYear/12 blocks), the coinbase must pay a hardcoded set of
 * budget addresses their configured amounts. The budget is EXTRA inflation (added to the block
 * reward), computed from the per-block Shark subsidy. Three payout epochs switch at the 5th and
 * 15th superblock.
 *
 * On a superblock: validates that the coinbase contains an output to each budget address with the
 * exact expected amount, and sets nBudgetReward to the total budget paid; returns false if any
 * payout is wrong (which makes ConnectBlock reject the block).
 * On a non-superblock: sets nBudgetReward = 0 and returns true.
 */
bool CheckSuperBlockBudget(const CBlock &block, int nHeight, const Amount &nBlockSubsidy,
                           const CChainParams &chainparams, Amount &nBudgetReward);

#endif // DEVAULT_DEVAULT_BUDGET_H
