// Copyright 2018 The Beam Team
// Copyright (c) 2019 The DeVault developers
// Copyright (c) 2019 Jon Spock
// Copyright (c) 2026 The DeVault developers (V2 port)
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once
#include <wallet/dictionary.h>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>
#include <stdexcept>

// DeVault BIP39 mnemonic (ported from legacy devault/src/wallet/mnemonic.{h,cpp}). The seed is the
// STANDARD BIP39 seed (PBKDF2-HMAC-SHA512, salt "mnemonic", 2048 iterations, 64 bytes -- no user
// passphrase), and the DeVault HD wallet derives BIP44 m/44'/<ExtCoinType>'/account'/change/index
// from it via the standard BIP32 master (HMAC-SHA512 "Bitcoin seed"). Restore validation is LENIENT
// like legacy: it requires 12/24 dictionary words but does NOT verify the BIP39 checksum. The
// BLS-key derivation path from legacy is intentionally dropped (V2 carries no BLS crypto).
namespace mnemonic {

class MnemonicException : public std::runtime_error {
public:
    explicit MnemonicException(const std::string &msg) : std::runtime_error(msg.c_str()) {}
    explicit MnemonicException(const char *msg) : std::runtime_error(msg) {}
};

typedef std::vector<std::string> WordList;

// Standard BIP39 seed (64 bytes) from a word list (PBKDF2-HMAC-SHA512, salt "mnemonic", 2048 iters).
std::vector<uint8_t> decodeMnemonic(const WordList &words);

WordList mapBitsToMnemonic(std::vector<uint8_t> &data, const Dictionary &dict);
WordList getListOfAllWordInLanguage(const Dictionary &dict);

bool isAllowedWord(const std::string &word, const Dictionary &dict);
bool isValidMnemonic(const WordList &words, const Dictionary &dict);
bool isValidSeedPhrase(const std::string &seedphrase);

// Defaults to 12 words; the only other valid setting is 24.
std::tuple<WordList, std::vector<uint8_t>> GenerateSeedPhrase(int nWords = 12);
// (valid?, words, 64-byte BIP39 seed). Validation is lenient (dictionary + count, no checksum).
std::tuple<bool, WordList, std::vector<uint8_t>> CheckSeedPhrase(const std::string &phrase);

} // namespace mnemonic
