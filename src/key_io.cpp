// Copyright (c) 2014-2016 The Bitcoin Core developers
// Copyright (c) 2019-2025 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>

#include <base58.h>
#include <cashaddr.h>
#include <cashaddrenc.h>
#include <chainparams.h>
#include <config.h>
#include <script/script.h>
#include <util/overloaded.h>
#include <util/strencodings.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <variant>

CKey DecodeSecret(const std::string &str) {
    CKey key;

    // DeVault: canonical WIF is a cashaddr private key with the "dvtpriv" prefix
    // (SECRET_TYPE=2; payload = the 32 raw key bytes, no compression flag). Ported verbatim from
    // legacy dstencode.cpp::DecodeSecret. DeVault has no uncompressed keys, so a decoded key is
    // always COMPRESSED. Try this first, then fall back to standard Base58Check WIF (interop).
    {
        auto [prefix, payload] =
            cashaddr::Decode(str, Params().CashAddrSecretPrefix());
        if (prefix == Params().CashAddrSecretPrefix() && !payload.empty()) {
            // The 5-bit payload's trailing padding must be < 5 bits and all zero.
            const size_t extrabits = (payload.size() * 5) % 8;
            if (extrabits < 5 && (payload.back() & ((1u << extrabits) - 1)) == 0) {
                std::vector<uint8_t> data;
                data.reserve(payload.size() * 5 / 8);
                if (ConvertBits<5, 8, false>(
                        [&](uint8_t c) { data.push_back(c); },
                        payload.begin(), payload.end()) &&
                    data.size() == 33 &&            // 1 version byte + 32 key bytes
                    (data[0] & 0x80) == 0 &&        // first (reserved) bit must be 0
                    ((data[0] >> 3) & 0x1f) == 2) { // SECRET_TYPE
                    key.Set(data.begin() + 1, data.end(), /*fCompressed=*/true);
                }
                memory_cleanse(data.data(), data.size());
            }
        }
        if (!payload.empty()) {
            memory_cleanse(payload.data(), payload.size());
        }
        if (key.IsValid()) {
            return key;
        }
    }

    // Standard Base58Check WIF (also accepted, for interop with non-DeVault tooling).
    std::vector<uint8_t> data;
    if (DecodeBase58Check(str, data, 34)) {
        const std::vector<uint8_t> &privkey_prefix =
            Params().Base58Prefix(CChainParams::SECRET_KEY);
        if ((data.size() == 32 + privkey_prefix.size() ||
             (data.size() == 33 + privkey_prefix.size() && data.back() == 1)) &&
            std::equal(privkey_prefix.begin(), privkey_prefix.end(),
                       data.begin())) {
            bool compressed = data.size() == 33 + privkey_prefix.size();
            key.Set(data.begin() + privkey_prefix.size(),
                    data.begin() + privkey_prefix.size() + 32, compressed);
        }
    }
    if (!data.empty()) {
        memory_cleanse(data.data(), data.size());
    }
    return key;
}

std::string EncodeSecret(const CKey &key) {
    assert(key.IsValid());
    // DeVault [2B]: canonical WIF = cashaddr with the "dvtpriv" prefix, matching legacy
    // dstencode.cpp::EncodeSecret byte-for-byte. version_byte = SECRET_TYPE<<3 (SECRET_TYPE=2 in
    // legacy dstencode.h); the dvtpriv prefix disambiguates it from BCHN's type-2 token address.
    // Payload = the 32 raw key bytes -- NO trailing compression flag (DeVault keys are always
    // compressed). DecodeSecret still accepts standard Base58 WIF for interop.
    static constexpr uint8_t SECRET_TYPE = 2;
    std::vector<uint8_t> data = {uint8_t(SECRET_TYPE << 3)};
    data.insert(data.end(), key.begin(), key.end());
    std::vector<uint8_t> converted;
    converted.reserve((data.size() * 8 + 4) / 5);
    ConvertBits<8, 5, true>([&](uint8_t c) { converted.push_back(c); },
                            data.begin(), data.end());
    std::string ret = cashaddr::Encode(Params().CashAddrSecretPrefix(), converted);
    memory_cleanse(data.data(), data.size());
    memory_cleanse(converted.data(), converted.size());
    return ret;
}

CExtPubKey DecodeExtPubKey(const std::string &str) {
    CExtPubKey key;
    std::vector<uint8_t> data;
    if (DecodeBase58Check(str, data, 78)) {
        const std::vector<uint8_t> &prefix =
            Params().Base58Prefix(CChainParams::EXT_PUBLIC_KEY);
        if (data.size() == BIP32_EXTKEY_SIZE + prefix.size() &&
            std::equal(prefix.begin(), prefix.end(), data.begin())) {
            key.Decode(data.data() + prefix.size());
        }
    }
    return key;
}

std::string EncodeExtPubKey(const CExtPubKey &key) {
    std::vector<uint8_t> data =
        Params().Base58Prefix(CChainParams::EXT_PUBLIC_KEY);
    size_t size = data.size();
    data.resize(size + BIP32_EXTKEY_SIZE);
    key.Encode(data.data() + size);
    std::string ret = EncodeBase58Check(data);
    return ret;
}

CExtKey DecodeExtKey(const std::string &str) {
    CExtKey key;
    std::vector<uint8_t> data;
    if (DecodeBase58Check(str, data, 78)) {
        const std::vector<uint8_t> &prefix =
            Params().Base58Prefix(CChainParams::EXT_SECRET_KEY);
        if (data.size() == BIP32_EXTKEY_SIZE + prefix.size() &&
            std::equal(prefix.begin(), prefix.end(), data.begin())) {
            key.Decode(data.data() + prefix.size());
        }
    }
    return key;
}

std::string EncodeExtKey(const CExtKey &key) {
    std::vector<uint8_t> data =
        Params().Base58Prefix(CChainParams::EXT_SECRET_KEY);
    size_t size = data.size();
    data.resize(size + BIP32_EXTKEY_SIZE);
    key.Encode(data.data() + size);
    std::string ret = EncodeBase58Check(data);
    memory_cleanse(data.data(), data.size());
    return ret;
}

std::string EncodeDestination(const CTxDestination &dest, const Config &config, const bool tokenAwareAddress) {
    const CChainParams &params = config.GetChainParams();
    return config.UseCashAddrEncoding() ? EncodeCashAddr(dest, params, tokenAwareAddress)
                                        : EncodeLegacyAddr(dest, params);
}

CTxDestination DecodeDestination(const std::string &addr, const CChainParams &params, bool *tokenAwareAddressOut) {
    CTxDestination dst = DecodeCashAddr(addr, params, tokenAwareAddressOut);
    if (IsValidDestination(dst)) {
        return dst;
    }
    if (tokenAwareAddressOut) *tokenAwareAddressOut = false; // legacy is never a token-aware address
    return DecodeLegacyAddr(addr, params);
}

bool IsValidDestinationString(const std::string &str, const CChainParams &params, bool *tokenAwareAddressOut) {
    return IsValidDestination(DecodeDestination(str, params, tokenAwareAddressOut));
}

std::string EncodeLegacyAddr(const CTxDestination &dest, const CChainParams &params) {
    return std::visit(
        util::Overloaded{
            [&params](const CKeyID &id) {
                std::vector<uint8_t> data = params.Base58Prefix(CChainParams::PUBKEY_ADDRESS);
                data.insert(data.end(), id.begin(), id.end());
                return EncodeBase58Check(data);
            },
            [&params](const ScriptID &id) {
                std::vector<uint8_t> data = params.Base58Prefix(CChainParams::SCRIPT_ADDRESS);
                data.insert(data.end(), id.begin(), id.end());
                return EncodeBase58Check(data);
            },
            [](const CNoDestination &) { return std::string{}; }
        }, dest);
}

CTxDestination DecodeLegacyAddr(const std::string &str, const CChainParams &params) {
    std::vector<uint8_t> data;
    uint160 hash{uint160::Uninitialized};
    if (!DecodeBase58Check(str, data, 33 /* max size is 33 (was 21 before p2sh_32), 33 is to support p2sh_32 */)) {
        return CNoDestination();
    }
    // base58-encoded Bitcoin addresses.
    // Public-key-hash-addresses have version 0 (or 111 testnet).
    // The data vector contains RIPEMD160(SHA256(pubkey)), where pubkey is
    // the serialized public key.
    const std::vector<uint8_t> &pubkey_prefix =
        params.Base58Prefix(CChainParams::PUBKEY_ADDRESS);
    if (data.size() == hash.size() + pubkey_prefix.size() &&
        std::equal(pubkey_prefix.begin(), pubkey_prefix.end(), data.begin())) {
        std::copy(data.begin() + pubkey_prefix.size(), data.end(),
                  hash.begin());
        return CKeyID(hash);
    }
    // Script-hash-addresses have version 5 (or 196 testnet).
    // The data vector contains RIPEMD160(SHA256(cscript)), where cscript is
    // the serialized redemption script.
    const std::vector<uint8_t> &script_prefix = params.Base58Prefix(CChainParams::SCRIPT_ADDRESS);
    if (data.size() == hash.size() + script_prefix.size() &&
        std::equal(script_prefix.begin(), script_prefix.end(), data.begin())) {
        std::copy(data.begin() + script_prefix.size(), data.end(), hash.begin());
        return ScriptID(hash); // p2sh_20
    }
    // p2sh_32 support
    // The data vector contains SHA256(SHA256(cscript)), where cscript is
    // the serialized redemption script.
    uint256 hash32{uint256::Uninitialized};
    if (data.size() == hash32.size() + script_prefix.size() &&
        std::equal(script_prefix.begin(), script_prefix.end(), data.begin())) {
        std::copy(data.begin() + script_prefix.size(), data.end(), hash32.begin());
        return ScriptID(hash32); // p2sh_32
    }
    return CNoDestination();
}
