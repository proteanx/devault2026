// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2022 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <serialize.h>

#include <cstdlib>
#include <ostream>
#include <string>
#include <type_traits>

struct Amount {
private:
    int64_t amount;

    explicit constexpr Amount(int64_t _amount) : amount(_amount) {}

public:
    constexpr Amount() noexcept : amount(0) {}
    constexpr Amount(const Amount &other) noexcept : amount(other.amount) {}

    /**
     * Assignement operator.
     */
    constexpr Amount &operator=(const Amount &other) noexcept {
        amount = other.amount;
        return *this;
    }

    static constexpr Amount zero() { return Amount(0); }
    static constexpr Amount satoshi() { return Amount(1); }

    /**
     * Implement standard operators
     */
    Amount &operator+=(const Amount a) {
        amount += a.amount;
        return *this;
    }
    Amount &operator-=(const Amount a) {
        amount -= a.amount;
        return *this;
    }

    /**
     * Equality
     */
    friend constexpr bool operator==(const Amount a, const Amount b) {
        return a.amount == b.amount;
    }
    friend constexpr bool operator!=(const Amount a, const Amount b) {
        return !(a == b);
    }

    /**
     * Comparison
     */
    friend constexpr bool operator<(const Amount a, const Amount b) {
        return a.amount < b.amount;
    }
    friend constexpr bool operator>(const Amount a, const Amount b) {
        return b < a;
    }
    friend constexpr bool operator<=(const Amount a, const Amount b) {
        return !(a > b);
    }
    friend constexpr bool operator>=(const Amount a, const Amount b) {
        return !(a < b);
    }

    /**
     * Unary minus
     */
    constexpr Amount operator-() const { return Amount(-amount); }

    /**
     * Addition and subtraction.
     */
    friend constexpr Amount operator+(const Amount a, const Amount b) {
        return Amount(a.amount + b.amount);
    }
    friend constexpr Amount operator-(const Amount a, const Amount b) {
        return a + -b;
    }

    /**
     * Multiplication
     */
    friend constexpr Amount operator*(const int64_t a, const Amount b) {
        return Amount(a * b.amount);
    }
    friend constexpr Amount operator*(const int a, const Amount b) {
        return Amount(a * b.amount);
    }

    /**
     * Division
     */
    constexpr int64_t operator/(const Amount b) const {
        return amount / b.amount;
    }
    constexpr Amount operator/(const int64_t b) const {
        return Amount(amount / b);
    }
    constexpr Amount operator/(const int b) const { return Amount(amount / b); }
    Amount &operator/=(const int64_t n) {
        amount /= n;
        return *this;
    }

    /**
     * Modulus
     */
    constexpr Amount operator%(const Amount b) const {
        return Amount(amount % b.amount);
    }
    constexpr Amount operator%(const int64_t b) const {
        return Amount(amount % b);
    }
    constexpr Amount operator%(const int b) const { return Amount(amount % b); }

    /**
     * Do not implement double ops to get an error with double and ensure
     * casting to integer is explicit.
     */
    friend constexpr Amount operator*(const double a, const Amount b) = delete;
    constexpr Amount operator/(const double b) const = delete;
    constexpr Amount operator%(const double b) const = delete;

    // ostream support
    friend std::ostream &operator<<(std::ostream &stream, const Amount &ca) {
        return stream << ca.amount;
    }

    std::string ToString(bool trimTrailingZeros = true, bool trimTrailingDecimalPoint = false) const;

    // serialization support
    SERIALIZE_METHODS(Amount, obj) { READWRITE(obj.amount); }
};

inline constexpr Amount SATOSHI = Amount::satoshi();
inline constexpr Amount CASH = 100 * SATOSHI;
inline constexpr Amount COIN = 100'000'000 * SATOSHI;

/**
 * DeVault [1F]: the unit of account is a "spock" = 0.001 DVT = 100000 satoshi (legacy
 * src/amount.h Amount::MIN_AMOUNT). Legacy Amount quantizes EVERY value up to a spock, rounding
 * UP, on each copy/arithmetic (Amount::QuantizeAmount); only raw (de)serialization is exact, so
 * block hashes are unaffected but the UTXO, the FORKID sighash, fees, and the supply total all
 * observe the rounded-up value. We keep BCHN's satoshi-granular Amount (so SATOSHI / fee-rate /
 * wallet code is untouched) and instead apply this spock round-up at the consensus boundaries
 * where legacy quantization is observable: values entering the UTXO (coins.cpp AddCoins) and
 * CTransaction::GetValueOut(). Round-up is correct for the non-negative amounts at those sites.
 * (Porting the full quantizing Amount class for the live chain's wallet/RPC display is deferred
 * to the post-fork phase -- same now-vs-later split as the cold-rewards engine.)
 */
inline constexpr int64_t SPOCK_SATS = 100'000; // legacy Amount::MIN_AMOUNT (0.001 DVT)
inline constexpr Amount SpockQuantize(const Amount a) {
    const int64_t am = a / SATOSHI;
    const int64_t residual = am % SPOCK_SATS;
    int64_t q = am - residual;
    if (residual > 0) { q += SPOCK_SATS; }
    return q * SATOSHI;
}

extern const std::string CURRENCY_UNIT;

/**
 * No amount larger than this (in satoshi) is valid.
 *
 * DeVault [1F]: this is *not* the total money supply (DeVault's is targeted between 4 and 5
 * billion DVT, TBD) but a sanity cap, set at 2 Billion due to the int64 arithmetic limit. As this
 * sanity check is used by consensus-critical validation code, the exact value of the MAX_MONEY
 * constant is consensus critical; in unusual circumstances like a(nother) overflow bug that
 * allowed for the creation of coins out of thin air, modification could lead to a fork.
 */
inline constexpr Amount MAX_MONEY = 2'000'000'000 * COIN; // 2 Billion (legacy DeVault amount.h)
inline bool MoneyRange(const Amount nValue) {
    return nValue >= Amount::zero() && nValue <= MAX_MONEY;
}
