// Copyright (c) 2019 The DeVault developers
// Copyright (c) 2019 Jon Spock
// Copyright (c) 2026 The DeVault developers (V2 port)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <devault/rewards.h>

#include <devault/rewards_calculation.h>

#include <consensus/params.h>
#include <logging.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <tinyformat.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <validation.h> // DEFAULT_MAX_REORG_DEPTH, cs_main

#include <vector>

std::unique_ptr<CColdRewards> g_coldRewards;

namespace {
//! Superblock after which sub-minimum candidates stop being paid and are purged (legacy constant).
const int clearLowRewardsSuperBlock = 6;
} // namespace

std::string CRewardValue::ToString() const {
    return strprintf("CR(value=%s creation=%d height=%d oldHeight=%d payCount=%d active=%d "
                     "inactiveHeight=%d script=%s)",
                     GetValue().ToString(), creationHeight, height, OldHeight, payCount, active,
                     inactiveHeight, HexStr(txout.scriptPubKey));
}

CColdRewards::CColdRewards(const Consensus::Params &consensusParams,
                           std::unique_ptr<CRewardsViewDB> prdb, bool fMainNetIn)
    : pdb(std::move(prdb)), fMainNet(fMainNetIn) {
    Setup(consensusParams);
}

void CColdRewards::Setup(const Consensus::Params &consensusParams) {
    nMinBlocks = consensusParams.nMinRewardBlocks;
    nMinReward = consensusParams.nMinReward;
}

// -- in-memory map mutation helpers (keep the dirty sets in sync for the atomic flush) --

void CColdRewards::PutInMap(const COutPoint &op, const CRewardValue &val) {
    rewardMap[op] = val;
    setDirty.insert(op);
    setErased.erase(op);
}

void CColdRewards::EraseFromMap(const COutPoint &op) {
    rewardMap.erase(op);
    setErased.insert(op);
    setDirty.erase(op);
}

bool CColdRewards::Load(BlockHash &dbBestBlock) {
    rewardMap.clear();
    inactiveByHeight.clear();
    setDirty.clear();
    setErased.clear();
    if (!pdb->LoadAll(rewardMap)) {
        return false;
    }
    for (const auto &[op, val] : rewardMap) {
        if (!val.IsActive()) {
            inactiveByHeight.emplace(val.GetInactiveHeight(), op);
        }
    }
    dbBestBlock = pdb->GetBestBlock();
    hashBestBlock = dbBestBlock;
    return true;
}

// -- Read-only consensus core --

CTxOut CColdRewards::GetPayment(const CRewardValue &coinreward, Amount reward) const {
    return CTxOut(reward, coinreward.txout.scriptPubKey);
}

// Determine which coin gets the reward and how much (ported from legacy FindReward). READ-ONLY: unlike
// legacy this performs no DB erases — low-reward candidates are merely skipped here (the actual purge
// is a one-time staged mutation in QuickValidate at height 131491).
bool CColdRewards::FindReward(const Consensus::Params &consensusParams, int Height,
                             CTxOut &rewardPayment) {
    CRewardValue sel_reward;
    COutPoint minKey;
    int minHeight = Height;
    Amount selAmount;
    bool found = false;
    int64_t count = 0;
    const int64_t nBlocksPerPeriod = consensusParams.nBlocksPerYear / 12;
    const Amount minRewardBalance = consensusParams.getMinRewardBalance(Height);
    const bool stop_lowrewards = (Height >= clearLowRewardsSuperBlock * nBlocksPerPeriod);

    for (const auto &[key, the_reward] : rewardMap) {
        if (!the_reward.IsActive()) {
            continue;
        }
        if (stop_lowrewards && (the_reward.GetValue() < minRewardBalance)) {
            continue; // sub-min candidate: skipped (purged separately, not here)
        }
        count++; // count of active (non-low) candidates
        const int nHeight = the_reward.GetHeight();
        if (nHeight <= minHeight) { // only consider candidates at least as old as the best so far
            const int HeightDiff = Height - nHeight;
            if (HeightDiff > nMinBlocks) {
                const Amount balance = the_reward.GetValue();
                // change 1.0.2: use the UTXO's nHeight (not the current Height) as the rate basis.
                const Amount reward = CalculateReward(consensusParams, nHeight, HeightDiff, balance);
                if (reward >= nMinReward) {
                    const Amount minHReward = reward;
                    if (nHeight < minHeight) {
                        // strictly older -> new best
                        selAmount = minHReward;
                        minHeight = nHeight;
                        minKey = key;
                        sel_reward = the_reward;
                    } else if (minHReward > selAmount) {
                        // same height, higher reward -> choose it
                        selAmount = minHReward;
                        minHeight = nHeight;
                        minKey = key;
                        sel_reward = the_reward;
                    } else if (minHReward == selAmount && key < minKey) {
                        // same height, same reward -> choose the smaller outpoint key
                        selAmount = minHReward;
                        minHeight = nHeight;
                        minKey = key;
                        sel_reward = the_reward;
                    }
                    found = true;
                }
            }
        }
    }

    if (found) {
        rewardPayment = GetPayment(sel_reward, selAmount);
        rewardKey = minKey;
    }
    nNumCandidates = count;
    return found;
}

// Confirm a coinbase reward payout corresponds to SOME eligible candidate at its address (ported from
// legacy CheckReward — the lenient connect-path check). The address compare is a direct scriptPubKey
// equality: legacy round-tripped through GetDestFromTxOut/GetScriptForDestination, which is the identity
// for the standard P2PKH/P2SH scripts every historical reward was paid to. READ-ONLY.
bool CColdRewards::CheckReward(const Consensus::Params &consensusParams, int Height,
                             const CTxOut &rewardPayment) {
    const CScript &script_ref = rewardPayment.scriptPubKey;

    // Collect all matching active candidates into a COutPoint-ordered map (legacy used a std::map too,
    // so the tie-break below is deterministic and order-independent of the underlying store).
    std::map<COutPoint, CRewardValue> vals;
    for (const auto &[key, val] : rewardMap) {
        if (!val.IsActive()) {
            continue;
        }
        const int nHeight = val.GetHeight();
        const int HeightDiff = Height - nHeight;
        if ((HeightDiff > nMinBlocks) && (script_ref == val.scriptPubKey())) {
            const Amount balance = val.GetValue();
            const Amount reward = CalculateReward(consensusParams, nHeight, HeightDiff, balance);
            if (reward == rewardPayment.nValue) {
                vals.emplace(key, val);
            }
        }
    }

    if (vals.empty()) {
        return false;
    }

    // Multiple matches possible (same address+block, or older-smaller matching newer-bigger). Take the
    // oldest (largest HeightDiff); on a tie the smallest outpoint (first in the COutPoint-ordered map).
    int minHeight = 0;
    for (const auto &[key, val] : vals) {
        const int HeightDiff = Height - int(val.GetHeight());
        if (HeightDiff > minHeight) {
            minHeight = HeightDiff;
            rewardKey = key;
        }
    }
    return true;
}

void CColdRewards::FillPayments(const Consensus::Params &consensusParams, CMutableTransaction &txNew,
                                int nHeight) {
    CTxOut out;
    if (FindReward(consensusParams, nHeight, out)) {
        txNew.vout.push_back(out);
    }
}

bool CColdRewards::QuickValidate(const Consensus::Params &consensusParams, const CBlock &block,
                                 int nHeight, Amount &reward, bool fJustCheck) {
    const auto &txCoinbase = block.vtx[0];
    const size_t size = txCoinbase->vout.size();

    if (fJustCheck) {
        reward = (size > 1) ? txCoinbase->vout[1].nValue : Amount::zero();
        return true;
    }

    // One-time clear-out of sub-minimum candidates (+1 because this function does not run on a
    // superblock). On mainnet this fires at 6*21915 + 1 = 131491. It is a STAGED map mutation (flushed
    // atomically), not a direct DB write during validation, so it does not reintroduce the shutdown bug.
    const int64_t nBlocksPerPeriod = consensusParams.nBlocksPerYear / 12;
    if (nHeight == clearLowRewardsSuperBlock * nBlocksPerPeriod + 1) {
        ClearLowRewards(consensusParams.getMinRewardBalance(nHeight));
    }

    if (size > 1) {
        const CTxOut &coinbase_reward = txCoinbase->vout[1];
        reward = coinbase_reward.nValue;
        if (!CheckReward(consensusParams, nHeight, coinbase_reward)) {
            LogPrintf("ERROR: Cold Reward invalid at height %d (value %s)\n", nHeight,
                      coinbase_reward.nValue.ToString());
            return false;
        }
    } else {
        reward = Amount::zero();
    }
    return true;
}

bool CColdRewards::FullValidate(const Consensus::Params &consensusParams, const CBlock &block,
                                int nHeight, Amount &reward, bool fJustCheck) {
    const auto &txCoinbase = block.vtx[0];
    const size_t size = txCoinbase->vout.size();

    if (fJustCheck) {
        reward = (size > 1) ? txCoinbase->vout[1].nValue : Amount::zero();
        return true;
    }

    CTxOut out;
    if (FindReward(consensusParams, nHeight, out)) {
        reward = out.nValue;
        if (size > 1) {
            const CTxOut &coinbase_reward = txCoinbase->vout[1];
            const bool valid = (out == coinbase_reward);
            if (!valid) {
                LogPrintf("ERROR: Cold Reward TxOut mismatch at height %d\n", nHeight);
            }
            return valid;
        }
        // FindReward expected a reward but the coinbase has none.
        if (((nHeight == 110068) || (nHeight == 110070)) && fMainNet) {
            // Nov 2019 hard-fork DB-patch heights: a stale DB entry; drop it and accept no reward.
            EraseFromMap(rewardKey);
            reward = Amount::zero();
            return true;
        }
        LogPrintf("WARNING: cold reward expected in DB but missing from coinbase at height %d\n",
                  nHeight);
        return false;
    }

    // No reward expected -> coinbase must have size 1.
    const bool valid = (size == 1);
    if (!valid) {
        LogPrintf("WARNING: cold reward in coinbase at height %d but none expected\n", nHeight);
    }
    reward = Amount::zero();
    return valid;
}

// -- State maintenance (mutates the map; called from Connect/DisconnectBlock) --

bool CColdRewards::UpdateWithBlock(const Consensus::Params &consensusParams, const CBlock &block,
                                   int nHeight) {
    const Amount minRewardBalance = consensusParams.getMinRewardBalance(nHeight);

    // Pass 1: add a candidate for each non-coinbase output >= min (creationHeight == nHeight marks it
    // "added this block", used in pass 2 to detect outputs created and spent within the same block).
    for (const auto &tx : block.vtx) {
        if (tx->IsCoinBase()) {
            continue;
        }
        const TxId txid = tx->GetId();
        for (size_t n = 0; n < tx->vout.size(); ++n) {
            const CTxOut &out = tx->vout[n];
            if (out.nValue >= minRewardBalance) {
                PutInMap(COutPoint(txid, uint32_t(n)),
                         CRewardValue(out, nHeight, nHeight, nHeight));
            }
        }
    }

    // Pass 2: spent inputs that are candidates -> inactivate (or, if created in this same block,
    // remove entirely, matching legacy which dropped same-block create+spend from the additions).
    for (const auto &tx : block.vtx) {
        if (tx->IsCoinBase()) {
            continue;
        }
        for (const CTxIn &in : tx->vin) {
            const COutPoint &op = in.prevout;
            auto it = rewardMap.find(op);
            if (it == rewardMap.end()) {
                continue;
            }
            if (it->second.GetCreationHeight() == uint32_t(nHeight)) {
                EraseFromMap(op); // created and spent within this block
            } else {
                CRewardValue val = it->second;
                val.SetActive(false);
                val.SetInactiveHeight(uint32_t(nHeight));
                PutInMap(op, val);
                inactiveByHeight.emplace(uint32_t(nHeight), op);
            }
        }
    }

    // Prune inactive records that can no longer be reorged (older than maxreorgdepth). This is the
    // mutation-in-the-connect-hook replacement for legacy's prune-during-validation; it is O(pruned)
    // thanks to the inactiveByHeight index and does not affect candidate selection (inactives are
    // never selected). A stale index entry (its record was reactivated or re-inactivated) is skipped.
    const int64_t maxreorgdepth = gArgs.GetArg("-maxreorgdepth", DEFAULT_MAX_REORG_DEPTH);
    const int64_t threshold = int64_t(nHeight) - maxreorgdepth;
    while (!inactiveByHeight.empty() && int64_t(inactiveByHeight.begin()->first) < threshold) {
        const auto front = inactiveByHeight.begin();
        const uint32_t h = front->first;
        const COutPoint op = front->second;
        inactiveByHeight.erase(front);
        auto it = rewardMap.find(op);
        if (it != rewardMap.end() && !it->second.IsActive() && it->second.GetInactiveHeight() == h) {
            EraseFromMap(op);
        }
    }
    return true;
}

bool CColdRewards::UndoBlock(const Consensus::Params &consensusParams, const CBlock &block,
                             int nHeight, bool undoReward) {
    // Pass 1: re-activate inputs that this block had inactivated (restore spent candidates).
    for (const auto &tx : block.vtx) {
        if (tx->IsCoinBase()) {
            continue;
        }
        for (const CTxIn &in : tx->vin) {
            const COutPoint &op = in.prevout;
            auto it = rewardMap.find(op);
            if (it == rewardMap.end()) {
                continue;
            }
            CRewardValue val = it->second;
            if (!val.IsActive()) {
                val.SetActive(true);
            } else {
                val.SetHeight(val.GetOldHeight());
            }
            if (val.GetHeight() >= uint32_t(nHeight)) {
                val.SetHeight(val.GetOldHeight());
            }
            PutInMap(op, val); // stale inactiveByHeight entry (if any) is handled by the prune guard
        }
    }

    // Pass 2: remove outputs this block added, and rewind the coinbase reward clock.
    for (const auto &tx : block.vtx) {
        if (!tx->IsCoinBase()) {
            const TxId txid = tx->GetId();
            for (size_t n = 0; n < tx->vout.size(); ++n) {
                const COutPoint op(txid, uint32_t(n));
                if (rewardMap.count(op)) {
                    EraseFromMap(op);
                }
            }
        } else if ((tx->vout.size() > 1) && !consensusParams.IsSuperBlock(nHeight) && undoReward) {
            RestoreRewardAtHeight(nHeight);
        }
    }
    return true;
}

bool CColdRewards::RestoreRewardAtHeight(int Height) {
    for (auto &[key, val] : rewardMap) {
        if (val.GetHeight() == uint32_t(Height)) {
            CRewardValue v = val;
            v.SetHeight(v.GetOldHeight());
            v.payCount--; // matches legacy (uint wrap on 0 is preserved but never reached in practice)
            PutInMap(key, v);
            return true;
        }
    }
    return false;
}

void CColdRewards::UpdateRewardsDB(const Consensus::Params &consensusParams, int nNewHeight) {
    auto it = rewardMap.find(rewardKey);
    if (it == rewardMap.end()) {
        return;
    }
    const CRewardValue coinreward = it->second;
    const Amount minRewardBalance = consensusParams.getMinRewardBalance(nNewHeight);
    if (coinreward.GetValue() >= minRewardBalance) {
        CRewardValue newReward(coinreward);
        newReward.SetOldHeight(newReward.GetHeight()); // shift last-paid/creation height to OldHeight
        newReward.SetHeight(nNewHeight);
        newReward.payCount++;
        PutInMap(rewardKey, newReward);
    } else {
        EraseFromMap(rewardKey); // dropped below the min balance -> stop tracking (legacy behaviour)
    }
}

void CColdRewards::ClearLowRewards(const Amount minRewardBalance) {
    std::vector<COutPoint> toErase;
    for (const auto &[key, val] : rewardMap) {
        if (val.GetValue() < minRewardBalance) {
            toErase.push_back(key);
        }
    }
    for (const auto &op : toErase) {
        EraseFromMap(op);
    }
}

// -- Persistence --

CColdRewards::Stats CColdRewards::GetStats() const {
    Stats s;
    s.records = int64_t(rewardMap.size());
    for (const auto &[op, val] : rewardMap) {
        if (val.IsActive()) {
            ++s.active;
        }
    }
    s.bestBlock = hashBestBlock;
    return s;
}

bool CColdRewards::Flush(const BlockHash &bestBlock) {
    std::map<COutPoint, CRewardValue> writes;
    for (const auto &op : setDirty) {
        auto it = rewardMap.find(op);
        if (it != rewardMap.end()) {
            writes.emplace(op, it->second);
        }
    }
    const std::vector<COutPoint> erases(setErased.begin(), setErased.end());
    if (!pdb->BatchWrite(writes, erases, bestBlock)) {
        return false;
    }
    setDirty.clear();
    setErased.clear();
    hashBestBlock = bestBlock;
    return true;
}
