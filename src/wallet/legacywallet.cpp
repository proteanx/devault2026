// Copyright (c) 2026 The DeVault developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/legacywallet.h>

#include <clientversion.h>
#include <fs.h>
#include <hash.h> // Hash() (single-arg = SHA256d of a byte container, == legacy GetSeedHash)
#include <logging.h>
#include <serialize.h>
#include <streams.h>
#include <wallet/crypter.h> // CMasterKey, CCrypter, CKeyingMaterial, WALLET_CRYPTO_IV_SIZE
#include <wallet/db.h>       // BerkeleyDatabase/BerkeleyBatch + <db_cxx.h> (Dbc, DB_NOTFOUND)
#include <wallet/mnemonic.h> // mnemonic::CheckSeedPhrase (seed-match cross-check)
#include <wallet/walletutil.h> // WalletLocation

#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

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

// ---------------------------------------------------------------------------------------------------
// 3A.3: legacy wallet reader + decrypt
//
// Legacy on-disk structures (see DEVAULT_WALLET_MIGRATION_DESIGN.md §4; mirror of the legacy
// devault/src/wallet/hdchain.h serialization). These are deliberately self-contained — V2's own
// CHDChain has a different layout and must NOT be used to parse a legacy record.
// ---------------------------------------------------------------------------------------------------

namespace {

//! Legacy CHDAccount: per-account external/internal derivation counters.
struct LegacyHDAccount {
    uint32_t nExternalChainCounter = 0;
    uint32_t nInternalChainCounter = 0;

    SERIALIZE_METHODS(LegacyHDAccount, obj) {
        READWRITE(obj.nExternalChainCounter, obj.nInternalChainCounter);
    }
};

//! Legacy CHDChain (the "chdchain" record): seed + mnemonic (encrypted iff fCrypted) + counters.
struct LegacyHDChain {
    int nVersion = 0;
    uint256 id; // = Hash(plaintext seed); also the AES IV source for the seed and mnemonic.
    std::vector<uint8_t> vchSeed; // plaintext 64-byte seed, or AES ciphertext if fCrypted.
    std::vector<uint8_t> vchMnemonic; // plaintext BIP39 phrase, or AES ciphertext if fCrypted.
    bool fCrypted = false;
    std::map<uint32_t, LegacyHDAccount> mapAccounts;

    SERIALIZE_METHODS(LegacyHDChain, obj) {
        READWRITE(obj.nVersion, obj.id, obj.vchSeed, obj.vchMnemonic, obj.fCrypted,
                  obj.mapAccounts);
    }
};

//! AES-256-CBC decrypt a legacy secret (seed or mnemonic) with the wallet master key, using the
//! first 16 bytes of the chain id as the IV — the exact inverse of the legacy file-static
//! DecryptSecret() (devault/src/wallet/crypter.cpp). Returns false on failure.
bool DecryptLegacySecret(const CKeyingMaterial &vMasterKey,
                         const std::vector<uint8_t> &vchCiphertext, const uint256 &iv,
                         CKeyingMaterial &vchPlaintext) {
    CCrypter crypter;
    std::vector<uint8_t> chIV(WALLET_CRYPTO_IV_SIZE);
    std::memcpy(chIV.data(), iv.begin(), WALLET_CRYPTO_IV_SIZE);
    if (!crypter.SetKey(vMasterKey, chIV)) {
        return false;
    }
    return crypter.Decrypt(vchCiphertext, vchPlaintext);
}

} // namespace

bool ReadLegacyWallet(const fs::path &legacyWalletFile, const SecureString &passphrase,
                      LegacyWalletContents &out, std::string &error) {
    LegacyHDChain chain;
    bool haveChain = false;
    std::vector<CMasterKey> masterKeys;

    // --- Pass over every record, collecting what migration needs (design note §2) ---
    try {
        std::unique_ptr<BerkeleyDatabase> database =
            BerkeleyDatabase::Create(legacyWalletFile);
        BerkeleyBatch batch(*database, "r", /*fFlushOnClose=*/false);

        Dbc *cursor = batch.GetCursor();
        if (cursor == nullptr) {
            error = "Unable to open the legacy wallet database cursor.";
            return false;
        }

        while (true) {
            CDataStream ssKey(SER_DISK, CLIENT_VERSION);
            CDataStream ssValue(SER_DISK, CLIENT_VERSION);
            int ret = batch.ReadAtCursor(cursor, ssKey, ssValue);
            if (ret == DB_NOTFOUND) {
                break;
            }
            if (ret != 0) {
                cursor->close();
                error = "Error reading a record from the legacy wallet database.";
                return false;
            }

            std::string strType;
            try {
                ssKey >> strType;
            } catch (const std::exception &) {
                continue;
            }

            // Tolerate a single unparseable record (matches legacy LoadWallet) — keep scanning.
            try {
                if (strType == "chdchain") {
                    if (!haveChain) {
                        ssValue >> chain;
                        haveChain = true;
                    }
                } else if (strType == "mkey") {
                    unsigned int nID;
                    ssKey >> nID;
                    CMasterKey mkey;
                    ssValue >> mkey;
                    masterKeys.push_back(std::move(mkey));
                } else if (strType == "name") {
                    std::string addr, label;
                    ssKey >> addr;
                    ssValue >> label;
                    out.addressNames.emplace_back(std::move(addr), std::move(label));
                } else if (strType == "purpose") {
                    std::string addr, purpose;
                    ssKey >> addr;
                    ssValue >> purpose;
                    out.addressPurposes.emplace_back(std::move(addr), std::move(purpose));
                } else if (strType == "destdata") {
                    std::string addr, key, value;
                    ssKey >> addr;
                    ssKey >> key;
                    ssValue >> value;
                    out.destData.emplace_back(
                        std::make_pair(std::move(addr), std::move(key)), std::move(value));
                } else if (strType == "cscript") {
                    // value is a CScript; read its raw bytes (length-prefixed, == vector<uint8_t>).
                    std::vector<uint8_t> script;
                    ssValue >> script;
                    out.redeemScripts.push_back(std::move(script));
                } else if (strType == "watchs") {
                    // the watch-only script is in the KEY (("watchs"), CScript)).
                    std::vector<uint8_t> script;
                    ssKey >> script;
                    out.watchScripts.push_back(std::move(script));
                }
            } catch (const std::exception &) {
                // Skip this record; a non-key record we can live without.
                continue;
            }
        }
        cursor->close();
    } catch (const std::exception &e) {
        error = std::string("Could not open the legacy wallet: ") + e.what();
        return false;
    }

    if (!haveChain) {
        error = "No HD seed ('chdchain') record found — this is not a legacy DeVault HD wallet.";
        return false;
    }

    out.encrypted = chain.fCrypted;
    out.seedId = chain.id;

    // EC account 0 counters (how many keys were ever issued on each chain).
    if (auto it = chain.mapAccounts.find(0); it != chain.mapAccounts.end()) {
        out.externalCounter = it->second.nExternalChainCounter;
        out.internalCounter = it->second.nInternalChainCounter;
    }
    // BLS account 1 (3A.4): V2 cannot re-derive BLS keys; flag for reporting.
    if (auto it = chain.mapAccounts.find(/*BLS_ACCOUNT=*/1); it != chain.mapAccounts.end()) {
        out.hasBLS = true;
        out.blsExternalCounter = it->second.nExternalChainCounter;
        out.blsInternalCounter = it->second.nInternalChainCounter;
    }

    // --- Recover the seed + mnemonic (design note §5) ---
    CKeyingMaterial seedMat, mnemMat;
    if (chain.fCrypted) {
        if (masterKeys.empty()) {
            error = "The legacy wallet is encrypted but has no master key record.";
            return false;
        }
        bool unlocked = false;
        for (const CMasterKey &mkey : masterKeys) {
            CCrypter kdf;
            if (!kdf.SetKeyFromPassphrase(passphrase, mkey.vchSalt, mkey.nDeriveIterations,
                                          mkey.nDerivationMethod)) {
                continue;
            }
            CKeyingMaterial vMasterKey;
            if (!kdf.Decrypt(mkey.vchCryptedKey, vMasterKey)) {
                continue; // this master key did not unwrap under the passphrase; try the next.
            }
            if (!DecryptLegacySecret(vMasterKey, chain.vchSeed, chain.id, seedMat)) {
                continue;
            }
            // Integrity gate: the decrypted seed must hash to the stored chain id.
            if (Hash(seedMat) != chain.id) {
                seedMat.clear();
                continue; // wrong passphrase (or corruption): keep trying other master keys.
            }
            if (!chain.vchMnemonic.empty()) {
                DecryptLegacySecret(vMasterKey, chain.vchMnemonic, chain.id, mnemMat);
            }
            unlocked = true;
            break;
        }
        if (!unlocked) {
            error = "Could not decrypt the legacy wallet — is the passphrase correct?";
            return false;
        }
    } else {
        // Unencrypted legacy wallet: seed/mnemonic are stored in the clear.
        seedMat.assign(chain.vchSeed.begin(), chain.vchSeed.end());
        if (!chain.vchMnemonic.empty()) {
            mnemMat.assign(chain.vchMnemonic.begin(), chain.vchMnemonic.end());
        }
        if (Hash(seedMat) != chain.id) {
            error = "The legacy wallet seed failed its integrity check (corrupt wallet).";
            return false;
        }
    }

    out.seed.assign(seedMat.begin(), seedMat.end());
    out.mnemonic = SecureString(mnemMat.begin(), mnemMat.end());

    // --- Seed-match proof (design note §7 step 3): the mnemonic must reproduce the exact seed ---
    // V2's mnemonic->seed (PBKDF2) is a byte-identical port of legacy's, so this confirms that a
    // native V2 wallet created from this mnemonic will control the very same addresses/funds.
    if (!out.mnemonic.empty()) {
        auto [ok, words, hashWords] =
            mnemonic::CheckSeedPhrase(std::string(out.mnemonic.begin(), out.mnemonic.end()));
        if (!ok) {
            error = "The recovered recovery phrase is not a valid BIP39 phrase.";
            return false;
        }
        if (Hash(hashWords) != chain.id) {
            error = "The recovered recovery phrase does not reproduce the wallet seed "
                    "(refusing to migrate — addresses would not match).";
            return false;
        }
    }

    return true;
}
