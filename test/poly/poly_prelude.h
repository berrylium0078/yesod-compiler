#ifndef _YESOD_TEST_POLY_POLY_PRELUDE_H_
#define _YESOD_TEST_POLY_POLY_PRELUDE_H_

#include <string>

namespace yesod::test_support::poly {

inline constexpr const char* POLY_TEST_PRELUDE = R"(void putpoly(poly f)
{
    putint(!f);
    putch(58);
    int i = 0;
    while (i < !f) {
        putch(32);
        putint(int(f[i]));
        i = i + 1;
    }
    putch(10);
}

)";

[[nodiscard]] inline std::string prependPolyTestPreludeIfMissing(
    const std::string& source)
{
    if (source.find("void putpoly") != std::string::npos) {
        return source;
    }
    return std::string(POLY_TEST_PRELUDE) + source;
}

} // namespace yesod::test_support::poly

#endif // _YESOD_TEST_POLY_POLY_PRELUDE_H_
