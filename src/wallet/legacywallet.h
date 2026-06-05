// Copyright (c) 2026 The DeVault developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_LEGACYWALLET_H
#define BITCOIN_WALLET_LEGACYWALLET_H

class WalletLocation;

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

#endif // BITCOIN_WALLET_LEGACYWALLET_H
