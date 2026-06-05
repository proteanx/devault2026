// Copyright (c) 2026 The DeVault developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/legacywallet.h>

#include <clientversion.h>
#include <fs.h>
#include <logging.h>
#include <streams.h>
#include <wallet/db.h> // BerkeleyDatabase/BerkeleyBatch + <db_cxx.h> (Dbc, DB_NOTFOUND)
#include <wallet/walletutil.h> // WalletLocation

#include <exception>
#include <memory>
#include <string>

bool IsLegacyDeVaultWallet(const WalletLocation &location) {
    const fs::path wallet_path = location.GetPath();

    // Resolve the actual BDB data file. A WalletLocation path may be either the wallet directory
    // (the default "" wallet resolves to <walletdir>) or the wallet.dat file itself. Only an
    // existing file can be a legacy wallet; a missing path is simply a wallet still to be created.
    fs::path data_file;
    if (fs::is_regular_file(wallet_path)) {
        data_file = wallet_path;
    } else if (fs::is_directory(wallet_path)) {
        data_file = wallet_path / "wallet.dat";
    } else {
        return false;
    }
    if (!fs::exists(data_file)) {
        return false;
    }

    try {
        // BerkeleyDatabase::Create()/SplitWalletPath() resolves the same dir-vs-file split, so the
        // unmodified location path opens the correct wallet.dat. Open read-only ("r" => no DB_CREATE,
        // fReadOnly=true) and do not flush on close: this never writes wallet records.
        std::unique_ptr<BerkeleyDatabase> database =
            BerkeleyDatabase::Create(wallet_path);
        BerkeleyBatch batch(*database, "r", /*fFlushOnClose=*/false);

        Dbc *cursor = batch.GetCursor();
        if (cursor == nullptr) {
            return false;
        }

        bool is_legacy = false;
        while (true) {
            CDataStream ssKey(SER_DISK, CLIENT_VERSION);
            CDataStream ssValue(SER_DISK, CLIENT_VERSION);
            int ret = batch.ReadAtCursor(cursor, ssKey, ssValue);
            if (ret == DB_NOTFOUND) {
                break;
            }
            if (ret != 0) {
                // Read error: do not claim "legacy"; let the normal verify path surface it.
                break;
            }

            // The record-type tag is the first object serialized in the key. We only need it; the
            // value is ignored (and never decrypted — detection requires no passphrase).
            std::string strType;
            try {
                ssKey >> strType;
            } catch (const std::exception &) {
                // Skip a record whose key we cannot parse; keep scanning.
                continue;
            }

            // Legacy-only record tags (see DEVAULT_WALLET_MIGRATION_DESIGN.md §6):
            //   "chdchain" — the Dash-style HD seed record (V2 native wallets use "hdchain").
            //   "hdpubkey" — a per-address pubkey record (V2 native wallets have no such record).
            if (strType == "chdchain" || strType == "hdpubkey") {
                is_legacy = true;
                break;
            }
        }
        cursor->close();
        return is_legacy;
    } catch (const std::exception &e) {
        // Could not open/iterate the file as a BDB wallet (corrupt, not a wallet, version mismatch).
        // That is not our concern here — the normal CWallet::Verify path reports such errors.
        LogPrintf("%s: could not inspect %s as a legacy wallet: %s\n", __func__,
                  data_file.string(), e.what());
        return false;
    }
}
