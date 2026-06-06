// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 The DeVault developers
// Copyright (c) 2019 Jon Spock
// Copyright (c) 2026 The DeVault developers (V2 port)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEVAULT_DEVAULT_COINREWARD_H
#define DEVAULT_DEVAULT_COINREWARD_H

#include <primitives/transaction.h>
#include <serialize.h>

#include <cstdint>
#include <string>

//! Reward DB key prefix (a record per candidate UTXO). Records are keyed `std::make_pair(DB_REWARD,
//! outpoint)` — the BCHN txdb convention (char + COutPoint), used self-consistently for writes and the
//! cursor. (Legacy DeVault declared a separate `CRewardKey` that serialized n as VARINT while its writes
//! went through `pair<char,COutPoint>` with a fixed uint32 n — an inconsistency that's moot here because
//! the V2 reward DB is a local cache rebuilt by reindex, never read in the legacy on-disk format.)
static const char DB_REWARD = 'R';

/**
 * Value stored per cold-reward candidate.
 *
 * Ported from legacy DeVault devault/coinreward.h. The reward state is a projection of the
 * chainstate: one record per output whose value >= getMinRewardBalance(creationHeight). `active`
 * marks it unspent.
 *
 * `height`     — creation height, or the height of the LAST payout once paid (the "age clock").
 * `OldHeight`  — the previous `height` (so a payout can be undone on reorg). was_paid() == (height != OldHeight).
 *
 * DeVault V2 [3D]: a record is kept (inactive) after its UTXO is spent so a reorg within
 * maxreorgdepth can restore it. The legacy engine tracked the inactivation height in a separate
 * in-memory `cachedInactives` map that was lost on restart and re-stamped at the *current* height
 * (`GetInActivesFromDB`) — a root cause of the shutdown desync. We instead PERSIST the inactivation
 * height in the record itself (`inactiveHeight`), so it survives restart and the in-memory map and
 * its faulty startup re-stamp are gone entirely.
 */
struct CRewardValue {
    CTxOut txout;
    uint32_t creationHeight;
    uint32_t OldHeight;
    uint32_t height;
    uint32_t payCount;
    uint8_t version = 2; // V2: bumped from legacy 1 (adds inactiveHeight)
    bool active;
    uint32_t inactiveHeight; // DeVault V2 [3D]: height at which this candidate was inactivated (0 if active)

    CScript scriptPubKey() const { return txout.scriptPubKey; }
    CTxOut &GetTxOut() { return txout; }
    const CTxOut &GetTxOut() const { return txout; }
    Amount GetValue() const { return txout.nValue; }
    uint32_t GetCreationHeight() const { return creationHeight; }
    uint32_t GetOldHeight() const { return OldHeight; }
    uint32_t GetHeight() const { return height; }
    uint32_t GetPayCount() const { return payCount; }
    uint8_t GetVersion() const { return version; }
    void SetVersion(const uint8_t nVersion) { version = nVersion; }
    void SetHeight(uint32_t h) { height = h; }
    void SetOldHeight(uint32_t h) { OldHeight = h; }
    bool was_paid() const { return (GetHeight() != GetOldHeight()); }
    bool IsActive() const { return active; }
    void SetActive(bool a) { active = a; }
    uint32_t GetInactiveHeight() const { return inactiveHeight; }
    void SetInactiveHeight(uint32_t h) { inactiveHeight = h; }

    CRewardValue()
        : creationHeight(0), OldHeight(0), height(0), payCount(0), active(false),
          inactiveHeight(0) {}
    explicit CRewardValue(const CTxOut &ptr, uint32_t cH, uint32_t OldH, uint32_t NewH)
        : txout(ptr), creationHeight(cH), OldHeight(OldH), height(NewH), payCount(0), active(true),
          inactiveHeight(0) {}

    template <typename Stream> void Serialize(Stream &s) const {
        s << version;
        s << txout;
        s << creationHeight;
        s << OldHeight;
        s << height;
        s << active;
        s << payCount;
        s << inactiveHeight;
    }

    template <typename Stream> void Unserialize(Stream &s) {
        s >> version;
        s >> txout;
        s >> creationHeight;
        s >> OldHeight;
        s >> height;
        s >> active;
        s >> payCount;
        s >> inactiveHeight;
    }

    std::string ToString() const;
};

#endif // DEVAULT_DEVAULT_COINREWARD_H
