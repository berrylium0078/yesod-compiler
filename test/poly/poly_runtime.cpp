#include "poly/poly_runtime.h"

#include <algorithm>
#include <ostream>
#include <utility>

namespace yesod::test_support::poly {

Mint::Mint(int64_t value)
{
    value %= MOD;
    if (value < 0) {
        value += MOD;
    }
    m_value = static_cast<int32_t>(value);
}

int32_t Mint::value() const { return m_value; }

Mint Mint::operator+() const { return *this; }

Mint Mint::operator-() const { return Mint(-m_value); }

Mint Mint::operator+(const Mint& rhs) const
{
    return Mint(static_cast<int64_t>(m_value) + rhs.m_value);
}

Mint Mint::operator-(const Mint& rhs) const
{
    return Mint(static_cast<int64_t>(m_value) - rhs.m_value);
}

Mint Mint::operator*(const Mint& rhs) const
{
    return Mint(static_cast<int64_t>(m_value) * rhs.m_value);
}

namespace {

    [[nodiscard]] int64_t mpow(int64_t a, int64_t e)
    {
        int64_t r = 1;
        while (e > 0) {
            if (e & 1) {
                r = (r * a) % Mint::MOD;
            }
            a = (a * a) % Mint::MOD;
            e >>= 1;
        }
        return r;
    }

} // namespace

Mint Mint::operator/(const Mint& rhs) const
{
    if (rhs.m_value == 0) {
        // Division by zero is undefined; return 0 as a safe fallback.
        return Mint(0);
    }
    return Mint(static_cast<int64_t>(m_value) * mpow(rhs.m_value, MOD - 2));
}

Mint& Mint::operator+=(const Mint& rhs)
{
    *this = *this + rhs;
    return *this;
}

Mint& Mint::operator-=(const Mint& rhs)
{
    *this = *this - rhs;
    return *this;
}

Mint& Mint::operator*=(const Mint& rhs)
{
    *this = *this * rhs;
    return *this;
}

bool Mint::operator==(const Mint& rhs) const { return m_value == rhs.m_value; }

bool Mint::operator!=(const Mint& rhs) const { return !(*this == rhs); }

Poly::Poly(std::vector<Mint> coefficients)
    : m_coefficients(std::move(coefficients))
{
}

Poly::Poly(Mint constant)
    : m_coefficients { constant }
{
}

int32_t Poly::length() const
{
    return static_cast<int32_t>(m_coefficients.size());
}

Mint Poly::coeff(int32_t index) const
{
    if (index < 0 || index >= length()) {
        return Mint(0);
    }
    return m_coefficients[static_cast<size_t>(index)];
}

const std::vector<Mint>& Poly::coefficients() const { return m_coefficients; }

Poly Poly::slice(int32_t start, int32_t end) const
{
    if (end <= 0 || start >= end || start >= length()) {
        return Poly();
    }
    const int32_t resultLength = std::min(end, length());
    std::vector<Mint> result(static_cast<size_t>(resultLength), Mint(0));
    for (int32_t index = std::max(start, 0); index < resultLength; ++index) {
        result[static_cast<size_t>(index)] = coeff(index);
    }
    return Poly(std::move(result));
}

Poly Poly::shiftLeft(int32_t amount) const { return shiftRight(-amount); }

Poly Poly::shiftRight(int32_t amount) const
{
    if (amount >= length()) {
        return Poly();
    }
    const int32_t resultLength = length() - amount;
    std::vector<Mint> result(static_cast<size_t>(resultLength), Mint(0));
    for (int32_t index = 0; index < resultLength; ++index) {
        result[static_cast<size_t>(index)] = coeff(index + amount);
    }
    return Poly(std::move(result));
}

Poly Poly::operator+(const Poly& rhs) const
{
    const int32_t resultLength = std::max(length(), rhs.length());
    std::vector<Mint> result(static_cast<size_t>(resultLength), Mint(0));
    for (int32_t index = 0; index < resultLength; ++index) {
        result[static_cast<size_t>(index)] = coeff(index) + rhs.coeff(index);
    }
    return Poly(std::move(result));
}

Poly Poly::operator-(const Poly& rhs) const
{
    const int32_t resultLength = std::max(length(), rhs.length());
    std::vector<Mint> result(static_cast<size_t>(resultLength), Mint(0));
    for (int32_t index = 0; index < resultLength; ++index) {
        result[static_cast<size_t>(index)] = coeff(index) - rhs.coeff(index);
    }
    return Poly(std::move(result));
}

Poly Poly::operator*(const Poly& rhs) const
{
    if (length() == 0 || rhs.length() == 0) {
        return Poly();
    }
    const int32_t resultLength = length() + rhs.length() - 1;
    std::vector<Mint> result(static_cast<size_t>(resultLength), Mint(0));
    for (int32_t i = 0; i < length(); ++i) {
        for (int32_t j = 0; j < rhs.length(); ++j) {
            result[static_cast<size_t>(i + j)] += coeff(i) * rhs.coeff(j);
        }
    }
    return Poly(std::move(result));
}

Poly Poly::operator*(const Mint& rhs) const
{
    std::vector<Mint> result;
    result.reserve(m_coefficients.size());
    for (const auto& coefficient : m_coefficients) {
        result.push_back(coefficient * rhs);
    }
    return Poly(std::move(result));
}

Poly operator*(const Mint& lhs, const Poly& rhs) { return rhs * lhs; }

std::ostream& operator<<(std::ostream& os, const Poly& poly)
{
    os << poly.length() << ':';
    for (const auto& coefficient : poly.coefficients()) {
        os << ' ' << coefficient.value();
    }
    os << '\n';
    return os;
}

} // namespace yesod::test_support::poly
