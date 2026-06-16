#ifndef _YESOD_TEST_POLY_POLY_RUNTIME_H_
#define _YESOD_TEST_POLY_POLY_RUNTIME_H_

#include <cstdint>
#include <iosfwd>
#include <vector>

namespace yesod::test_support::poly {

class Mint {
public:
    static constexpr int32_t MOD = 998244353;

    Mint() = default;
    explicit Mint(int64_t value);

    [[nodiscard]] int32_t value() const;

    [[nodiscard]] Mint operator+() const;
    [[nodiscard]] Mint operator-() const;
    [[nodiscard]] Mint operator+(const Mint& rhs) const;
    [[nodiscard]] Mint operator-(const Mint& rhs) const;
    [[nodiscard]] Mint operator*(const Mint& rhs) const;
    [[nodiscard]] Mint operator/(const Mint& rhs) const;
    Mint& operator+=(const Mint& rhs);
    Mint& operator-=(const Mint& rhs);
    Mint& operator*=(const Mint& rhs);

    [[nodiscard]] bool operator==(const Mint& rhs) const;
    [[nodiscard]] bool operator!=(const Mint& rhs) const;

private:
    int32_t m_value = 0;
};

class Poly {
public:
    Poly() = default;
    explicit Poly(std::vector<Mint> coefficients);
    explicit Poly(Mint constant);

    [[nodiscard]] int32_t length() const;
    [[nodiscard]] Mint coeff(int32_t index) const;
    [[nodiscard]] const std::vector<Mint>& coefficients() const;

    [[nodiscard]] Poly slice(int32_t start, int32_t end) const;
    [[nodiscard]] Poly shiftLeft(int32_t amount) const;
    [[nodiscard]] Poly shiftRight(int32_t amount) const;
    [[nodiscard]] Poly operator+(const Poly& rhs) const;
    [[nodiscard]] Poly operator-(const Poly& rhs) const;
    [[nodiscard]] Poly operator*(const Poly& rhs) const;
    [[nodiscard]] Poly operator*(const Mint& rhs) const;

private:
    std::vector<Mint> m_coefficients;
};

[[nodiscard]] Poly operator*(const Mint& lhs, const Poly& rhs);
std::ostream& operator<<(std::ostream& os, const Poly& poly);

} // namespace yesod::test_support::poly

#endif
