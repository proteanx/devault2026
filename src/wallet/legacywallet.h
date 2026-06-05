// Copyright (c) 2026 The DeVault developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_LEGACYWALLET_H
#define BITCOIN_WALLET_LEGACYWALLET_H

#include <fs.h>
#include <support/allocators/secure.h>
#include <uint256.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

class WalletLocation;

/**
 * DeVault [3A.3]: set true by a caller (the GUI) that is about to migrate the legacy wallet itself, so
 * the startup detector skips its "legacy wallet found — run migratelegacywallet" warning. That warning
 * is for users who have not yet acted; when migration is already underway it is redundant (and, in the
 * GUI, a modal warning would block init before the migration can run).
 */
extern std::atomic<bool> g_legacy_migration_pending;

/**
 * DeVault [3A.2]: detect a legacy (pre-V2, Dash-Core-style HD) DeVault `wallet.dat`.
 *
 * Legacy DeVault wallets are purely HD: the BIP39 seed lives in a single "chdchain" record and every
 * derived address is stored as an "hdpubkey" record (no private keys are ever written to disk). A V2
 * native BIP39 wallet instead uses an "hdchain" record plus "key"/"ckey" records and has no "hdpubkey"
 * concept at all. The two on-disk formats are therefore mutually exclusive, and the presence of a
 * "chdchain" or "hdpubkey" record uniquely and reliably identifies a legacy wallet.
 *
 * Such a wallet CANNOT be loaded by V2 (the record layouts are incompatible); attempting to load it
 * would fail and brick node startup. This detector lets init.cpp skip a legacy wallet and prompt the
 * user to migrate it with `migratelegacywallet` instead. See DEVAULT_WALLET_MIGRATION_DESIGN.md §6.
 *
 * The wallet file is opened read-only and is never modified. Returns false for a non-existent path, a
 * non-wallet / unreadable file, or a V2 native wallet.
 */
bool IsLegacyDeVaultWallet(const WalletLocation &location);

/**
 * DeVault [3A.3]: the decrypted, re-creatable contents of a legacy DeVault wallet.
 *
 * Everything needed to reproduce the wallet natively: the BIP39 mnemonic/seed (the keys are all
 * HD-derived from it), the per-account derivation counters (so the exact set of previously-issued
 * addresses can be re-derived), and the address book / scripts to carry over. See the design note
 * §4-§5, §7-§9. Secret fields use secure allocators and should be cleared by the caller after use.
 */
struct LegacyWalletContents {
    //! True if the source wallet was encrypted (production legacy wallets always are).
    bool encrypted = false;
    //! The recovered BIP39 recovery phrase (may be empty in the rare seed-only wallet).
    SecureString mnemonic;
    //! The recovered 64-byte BIP39 seed.
    SecureVector seed;
    //! chdchain.id == Hash(seed): the integrity tag / AES IV source of the source wallet.
    uint256 seedId;

    //! EC account (index 0) derivation counters: how many external/internal keys were ever issued.
    uint32_t externalCounter = 0;
    uint32_t internalCounter = 0;

    //! Address book: (address string, label) and (address string, purpose) pairs.
    std::vector<std::pair<std::string, std::string>> addressNames;
    std::vector<std::pair<std::string, std::string>> addressPurposes;
    //! Per-destination metadata: ((address, key), value).
    std::vector<std::pair<std::pair<std::string, std::string>, std::string>> destData;
    //! Redeem scripts ("cscript") and watch-only scripts ("watchs"), as raw serialized script bytes.
    std::vector<std::vector<uint8_t>> redeemScripts;
    std::vector<std::vector<uint8_t>> watchScripts;

    //! BLS (3A.4): true if the wallet used the BLS account (index 1). V2 cannot re-derive BLS keys.
    bool hasBLS = false;
    uint32_t blsExternalCounter = 0;
    uint32_t blsInternalCounter = 0;
};

/**
 * DeVault [3A.3]: read and decrypt a legacy DeVault `wallet.dat`, recovering its seed/mnemonic, the
 * derivation counters, and the address book, into `out`.
 *
 * Reproduces the inverse of the legacy `CWallet::Unlock` + `DecryptHDChain` (design note §5). The file
 * is opened read-only; callers MUST pass a COPY in a private directory, because opening it creates
 * Berkeley DB environment files alongside it (never open a wallet inside ~/.devault). On wrong
 * passphrase / corruption / not-a-legacy-wallet, returns false and sets `error`.
 */
bool ReadLegacyWallet(const fs::path &legacyWalletFile, const SecureString &passphrase,
                      LegacyWalletContents &out, std::string &error);

#endif // BITCOIN_WALLET_LEGACYWALLET_H
