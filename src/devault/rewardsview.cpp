// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 The DeVault developers
// Copyright (c) 2019 Jon Spock
// Copyright (c) 2026 The DeVault developers (V2 port)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <devault/rewardsview.h>

using KeyType = std::pair<char, COutPoint>;

CRewardsViewDB::CRewardsViewDB(const fs::path &path, size_t nCacheSize, bool fMemory, bool fWipe)
    : db(path, nCacheSize, fMemory, fWipe, /*obfuscate=*/true) {}

bool CRewardsViewDB::BatchWrite(const std::map<COutPoint, CRewardValue> &writes,
                                const std::vector<COutPoint> &erases, const BlockHash &bestBlock) {
    CDBBatch batch(db);
    for (const auto &[outpoint, value] : writes) {
        batch.Write(KeyType(DB_REWARD, outpoint), value);
    }
    for (const auto &outpoint : erases) {
        batch.Erase(KeyType(DB_REWARD, outpoint));
    }
    // The best block goes in the SAME batch as the record changes, so the DB content and its recorded
    // height advance atomically — the reward DB can never be ahead of its best block (the property that
    // makes startup reconciliation a forward-replay and removes the legacy shutdown desync).
    if (!bestBlock.IsNull()) {
        batch.Write(DB_REWARD_BESTBLOCK, bestBlock);
    }
    return db.WriteBatch(batch, /*fSync=*/false);
}

BlockHash CRewardsViewDB::GetBestBlock() const {
    BlockHash hashBestChain;
    if (!db.Read(DB_REWARD_BESTBLOCK, hashBestChain)) {
        return BlockHash();
    }
    return hashBestChain;
}

bool CRewardsViewDB::LoadAll(std::map<COutPoint, CRewardValue> &out) const {
    std::unique_ptr<CRewardsViewDBCursor> pcursor(Cursor());
    while (pcursor->Valid()) {
        COutPoint key;
        CRewardValue val;
        if (!pcursor->GetKey(key)) {
            break;
        }
        if (!pcursor->GetValue(val)) {
            return false;
        }
        out.emplace(key, val);
        pcursor->Next();
    }
    return true;
}

CRewardsViewDBCursor *CRewardsViewDB::Cursor() const {
    CRewardsViewDBCursor *i = new CRewardsViewDBCursor(const_cast<CDBWrapper &>(db).NewIterator());
    // Seek to the first 'R' record. The best-block key 'b' (0x62) sorts after all 'R' keys (0x52),
    // so the scan stops cleanly when GetKey() fails to parse it as a record key.
    i->pcursor->Seek(DB_REWARD);
    if (!i->pcursor->Valid() || !i->pcursor->GetKey(i->keyTmp)) {
        i->keyTmp.first = 0;
    }
    return i;
}

bool CRewardsViewDBCursor::GetKey(COutPoint &key) const {
    if (keyTmp.first == DB_REWARD) {
        key = keyTmp.second;
        return true;
    }
    return false;
}

void CRewardsViewDBCursor::Next() {
    pcursor->Next();
    if (!pcursor->Valid() || !pcursor->GetKey(keyTmp)) {
        // Invalidate the cached key past the last record so Valid()/GetKey() return false.
        keyTmp.first = 0;
    }
}
