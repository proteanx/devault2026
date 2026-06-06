// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 The DeVault developers
// Copyright (c) 2019 Jon Spock
// Copyright (c) 2026 The DeVault developers (V2 port)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEVAULT_DEVAULT_REWARDSVIEW_H
#define DEVAULT_DEVAULT_REWARDSVIEW_H

#include <dbwrapper.h>
#include <devault/coinreward.h>
#include <fs.h>
#include <primitives/blockhash.h>

#include <map>
#include <memory>
#include <utility>
#include <vector>

//! Key under which the reward DB stores the block hash its contents correspond to (DeVault V2 [3D],
//! for crash reconciliation). Distinct from the per-record 'R' prefix.
static const char DB_REWARD_BESTBLOCK = 'b';

/** Cursor over the cold-reward DB (ported from legacy DeVault; used to load the full state at startup). */
class CRewardsViewDBCursor {
public:
    bool GetKey(COutPoint &key) const;
    bool GetValue(CRewardValue &coin) const { return pcursor->GetValue(coin); }
    unsigned int GetValueSize() const { return pcursor->GetValueSize(); }
    bool Valid() const { return keyTmp.first == DB_REWARD; }
    void Next();

private:
    CRewardsViewDBCursor(CDBIterator *pcursorIn) : pcursor(pcursorIn) {}
    std::unique_ptr<CDBIterator> pcursor;
    std::pair<char, COutPoint> keyTmp;
    friend class CRewardsViewDB;
};

/**
 * On-disk backing store for the cold-reward state (a LevelDB at <datadir>/coldrewards).
 *
 * DeVault V2 [3D]: this is a pure persistence backing for the engine's in-memory reward map. The
 * engine flushes its dirty records + the chainstate's best-block in a single atomic batch
 * (BatchWrite), so the DB content is ALWAYS exactly at its stored best block — never ahead. That,
 * plus flushing AFTER the chainstate, makes the legacy shutdown desync structurally impossible.
 */
class CRewardsViewDB {
protected:
    CDBWrapper db;

public:
    explicit CRewardsViewDB(const fs::path &path, size_t nCacheSize, bool fMemory = false,
                            bool fWipe = false);

    bool GetReward(const COutPoint &outpoint, CRewardValue &coin) const {
        return db.Read(std::make_pair(DB_REWARD, outpoint), coin);
    }
    bool HaveReward(const COutPoint &outpoint) const {
        return db.Exists(std::make_pair(DB_REWARD, outpoint));
    }

    //! Atomically write a set of record changes + the best block in ONE batch. This is the only path
    //! that advances the DB's best block, so the DB and its best block can never disagree. fSync forces
    //! the write durable (used only at clean shutdown -- the bestBlock write makes the batch non-empty
    //! so LevelDB actually flushes its log buffer, unlike an empty Sync()).
    bool BatchWrite(const std::map<COutPoint, CRewardValue> &writes,
                    const std::vector<COutPoint> &erases, const BlockHash &bestBlock,
                    bool fSync = false);

    //! The block hash the DB contents correspond to (null if never written).
    BlockHash GetBestBlock() const;

    //! Load every reward record into `out` (used to populate the engine's in-memory map at startup).
    bool LoadAll(std::map<COutPoint, CRewardValue> &out) const;

    bool IsEmpty() { return db.IsEmpty(); }
    void Sync() { db.Sync(); }
    size_t EstimateSize() const { return db.EstimateSize(DB_REWARD, char(DB_REWARD + 1)); }

    CRewardsViewDBCursor *Cursor() const;
};

#endif // DEVAULT_DEVAULT_REWARDSVIEW_H
