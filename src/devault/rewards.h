// Copyright (c) 2019 The DeVault developers
// Copyright (c) 2019 Jon Spock
// Copyright (c) 2026 The DeVault developers (V2 port)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEVAULT_DEVAULT_REWARDS_H
#define DEVAULT_DEVAULT_REWARDS_H

#include <amount.h>
#include <devault/coinreward.h>
#include <devault/rewardsview.h>
#include <primitives/blockhash.h>
#include <sync.h>

#include <cstdint>
#include <map>
#include <memory>
#include <set>

class CBlock;
class CMutableTransaction;
namespace Consensus {
struct Params;
}

/**
 * DeVault cold-reward engine (V2 rebuild of legacy DeVault devault/rewards.cpp).
 *
 * Cold rewards pay an interest-like inflation to long-held large UTXOs: at most one UTXO per block
 * (the oldest eligible) earns a payout to its own scriptPubKey in coinbase vout[1], never on a
 * superblock. The economics are IDENTICAL to legacy for the drop-in fork (see 3D.8).
 *
 * Differences from legacy that fix the shutdown bug (DEVAULT_COLD_REWARDS_DESIGN.md §3/§5):
 *  - The full reward state lives in an in-memory map (`rewardMap`); the DB is a pure persistence
 *    backing. Validation (FindReward/CheckReward) is READ-ONLY on the map — unlike legacy, which
 *    mutated the DB during ConnectBlock.
 *  - Mutations happen ONLY in the block hooks (UpdateWithBlock/UndoBlock/UpdateRewardsDB) and are
 *    staged (setDirty/setErased) for an atomic flush.
 *  - Flush() writes the dirty records + the chainstate best block in ONE batch, AFTER the chainstate
 *    flush, so the reward DB is never ahead of the chainstate; startup reconciliation is a forward
 *    replay. The inactivation height is persisted in each record, so the buggy in-memory
 *    `cachedInactives`/`GetInActivesFromDB(currentHeight)` re-stamp is gone.
 *
 * Threading: all access is under cs_main (ConnectBlock/DisconnectBlock/FlushStateToDisk/mining).
 */
class CColdRewards {
public:
    CColdRewards(const Consensus::Params &consensusParams, CRewardsViewDB *prdb, bool fMainNetIn);

    //! Populate the in-memory map from the backing DB; returns the DB's recorded best block.
    bool Load(BlockHash &dbBestBlock) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    // -- Read-only consensus core (no state mutation) --
    bool FindReward(const Consensus::Params &consensusParams, int Height, CTxOut &out)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool CheckReward(const Consensus::Params &consensusParams, int Height, const CTxOut &rewardPayment)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    //! Connect-path validation (lenient, via CheckReward — matches every historical block). fJustCheck
    //! extracts vout[1] without validating (the Phase-1 Option-B behaviour, kept for fast reindex).
    bool QuickValidate(const Consensus::Params &consensusParams, const CBlock &block, int nHeight,
                       Amount &reward, bool fJustCheck = false) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    //! Strict validation (must equal FindReward's canonical pick). NOT used on the connect path for the
    //! drop-in fork (we keep lenient — see 3D.8); retained for mining self-checks and a future hardfork.
    bool FullValidate(const Consensus::Params &consensusParams, const CBlock &block, int nHeight,
                      Amount &reward, bool fJustCheck = false) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    CTxOut GetPayment(const CRewardValue &coin, Amount reward) const;
    void FillPayments(const Consensus::Params &consensusParams, CMutableTransaction &txNew, int nHeight)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    // -- State maintenance (mutates the map; called from Connect/DisconnectBlock) --
    bool UpdateWithBlock(const Consensus::Params &consensusParams, const CBlock &block, int nHeight)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool UndoBlock(const Consensus::Params &consensusParams, const CBlock &block, int nHeight,
                   bool undoReward = true) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void UpdateRewardsDB(const Consensus::Params &consensusParams, int nNewHeight)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    // -- Persistence (3D.3) --
    //! Stage the dirty records + bestBlock into the DB in one atomic batch; clears the dirty sets.
    bool Flush(const BlockHash &bestBlock) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void SetBestBlock(const BlockHash &h) EXCLUSIVE_LOCKS_REQUIRED(cs_main) { hashBestBlock = h; }
    BlockHash GetBestBlock() const EXCLUSIVE_LOCKS_REQUIRED(cs_main) { return hashBestBlock; }

    int32_t GetNumberOfCandidates() const { return nNumCandidates; }

private:
    void Setup(const Consensus::Params &consensusParams);
    //! Restore the paid clock of the candidate last paid at `Height` (reorg of a cold-reward payout).
    bool RestoreRewardAtHeight(int Height);
    //! One-time historical purge of sub-min candidates (legacy ClearLowRewards at height 131491).
    void ClearLowRewards(const Amount minRewardBalance);

    // map mutation helpers (keep setDirty/setErased in sync)
    void PutInMap(const COutPoint &op, const CRewardValue &val);
    void EraseFromMap(const COutPoint &op);

    CRewardsViewDB *pdb;
    std::map<COutPoint, CRewardValue> rewardMap; // full in-memory state (active + recently-inactive)
    // Index of inactive (spent) records by their inactivation height, so the connect hook can prune
    // entries older than maxreorgdepth in O(pruned) instead of scanning the whole map every block.
    std::multimap<uint32_t, COutPoint> inactiveByHeight;
    std::set<COutPoint> setDirty;                // records added/modified since the last flush
    std::set<COutPoint> setErased;               // records removed since the last flush
    BlockHash hashBestBlock;                      // block height the in-memory state corresponds to
    COutPoint rewardKey;                          // last selection (FindReward/CheckReward) -> UpdateRewardsDB
    int64_t nMinBlocks;
    Amount nMinReward;
    bool fMainNet;
    int32_t nNumCandidates = 0;
};

//! Global engine instance (constructed in init.cpp once the chainstate is open). Null if cold rewards
//! are not active for this run (e.g. the wallet tool).
extern std::unique_ptr<CColdRewards> g_coldRewards;

#endif // DEVAULT_DEVAULT_REWARDS_H
