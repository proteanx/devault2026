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

#include <wallet/mnemonic.h>

#include <crypto/pkcs5_pbkdf2.h>
#include <crypto/sha256.h>
#include <random.h>
#include <util/string.h>

#include <algorithm>
#include <cassert>

namespace mnemonic {
namespace {
const size_t bitsPerWord = 11;
const uint8_t byteBits = 8;
const std::string passphrasePrefix = "mnemonic";
const size_t hmacIterations = 2048;
const size_t sizeHash = 512 >> 3; // 64 bytes

uint8_t shiftBits(size_t bit) { return (1 << (byteBits - (bit % byteBits) - 1)); }

// Split on any whitespace, dropping empty tokens (so the canonical single-space-joined sentence is
// reproduced regardless of incidental leading/trailing/repeated whitespace in the input).
WordList splitWords(const std::string &phrase) {
    WordList words;
    std::string cur;
    for (const char c : phrase) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
            if (!cur.empty()) {
                words.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) {
        words.push_back(cur);
    }
    return words;
}
} // namespace

// Matches the standard BIP39 seed (e.g. iancoleman.io/bip39 with an empty passphrase): the PBKDF2
// passphrase is the space-joined sentence, the salt is "mnemonic", 2048 HMAC-SHA512 iterations.
std::vector<uint8_t> decodeMnemonic(const WordList &words) {
    const std::string sentence = Join(words, " ");
    std::vector<uint8_t> passphrase(sentence.begin(), sentence.end());
    std::vector<uint8_t> salt(passphrasePrefix.begin(), passphrasePrefix.end());
    std::vector<uint8_t> hash(sizeHash);

    const int result =
        pkcs5_pbkdf2(passphrase.data(), passphrase.size(), salt.data(), salt.size(),
                     hash.data(), hash.size(), hmacIterations);
    if (result != 0) {
        throw MnemonicException("pbkdf2 returned bad result");
    }
    return hash;
}

WordList mapBitsToMnemonic(std::vector<uint8_t> &data, const Dictionary &dict) {
    // 16 bytes (128 bits) -> 12 words; 32 bytes -> 24 words. A checksum byte is appended.
    assert((data.size() == 16) || (data.size() == 32));
    const int wordCount = (data.size() == 16) ? 12 : 24;

    uint8_t checksum[CSHA256::OUTPUT_SIZE];
    CSHA256().Write(&data[0], data.size()).Finalize(checksum);

    WordList words;
    size_t bit = 0;
    data.push_back(checksum[0]);

    for (int word = 0; word < wordCount; word++) {
        size_t position = 0;
        for (size_t loop = 0; loop < bitsPerWord; loop++) {
            bit = (word * bitsPerWord + loop);
            position <<= 1;
            const auto byte = bit / byteBits;
            if ((data[byte] & shiftBits(bit)) > 0) {
                position++;
            }
        }
        words.push_back(dict[position]);
    }
    return words;
}

WordList getListOfAllWordInLanguage(const Dictionary &dict) {
    WordList words;
    for (unsigned long i = 0; i < dict.size(); i++) {
        words.push_back(dict[i]);
    }
    return words;
}

bool isAllowedWord(const std::string &word, const Dictionary &dict) {
    assert(std::is_sorted(dict.begin(), dict.end()));
    return std::binary_search(dict.begin(), dict.end(), word);
}

// LENIENT validation, matching legacy DeVault: 12/24 dictionary words; the BIP39 checksum is NOT
// verified (this is why e.g. "fee" x12 is accepted as a valid DeVault seed phrase).
bool isValidMnemonic(const WordList &words, const Dictionary &dict) {
    return ((words.size() == 12) || (words.size() == 24)) &&
           std::all_of(words.begin(), words.end(),
                       [&dict](const std::string &w) { return isAllowedWord(w, dict); });
}

bool isValidSeedPhrase(const std::string &seedphrase) {
    return isValidMnemonic(splitWords(seedphrase), language::en);
}

std::tuple<bool, WordList, std::vector<uint8_t>> CheckSeedPhrase(const std::string &phrase) {
    std::vector<uint8_t> seed;
    WordList words = splitWords(phrase);
    if (isValidMnemonic(words, language::en)) {
        seed = decodeMnemonic(words);
        return std::make_tuple(true, words, seed);
    }
    return std::make_tuple(false, words, seed);
}

std::tuple<WordList, std::vector<uint8_t>> GenerateSeedPhrase(int nWords) {
    assert((nWords == 12) || (nWords == 24));
    const int size = (nWords == 12) ? 16 : 32;
    std::vector<uint8_t> keydata(size);
    GetStrongRandBytes(keydata.data(), keydata.size());
    WordList words = mapBitsToMnemonic(keydata, language::en);
    std::vector<uint8_t> seed = decodeMnemonic(words);
    return std::make_tuple(words, seed);
}

} // namespace mnemonic
