#ifndef _YESOD_BACKEND_LLVM_POLY_RUNTIME_H_
#define _YESOD_BACKEND_LLVM_POLY_RUNTIME_H_

#include <string_view>

namespace yesod::backend {

inline constexpr std::string_view LLVM_POLY_RUNTIME_SOURCE = R"(
typedef long long i64;
typedef unsigned long long u64;
typedef unsigned long usize;

extern void *malloc(usize size);
extern void free(void *ptr);

typedef struct YesodPoly {
    int *coeffs;
    int *addr;
    int n;
    int l;
    int r;
} YesodPoly;

typedef struct YesodPointValues {
    int *values;
    int len;
} YesodPointValues;

static const int MOD = 998244353;
static const int MONT_R = 301989884;
static const int MONT_R2 = 932051910;
static const int MONT_U = 998244351;
static const int YESOD_MAX_LOGN = 23;
static const int YESOD_NTT_ROOTS[24] = { 301989884, -301989884, -306948983,
    -109640866, 54518517, -429836493, 232329203, 34051923, 5152499, -252999103,
    -173318384, -181472998, -224654934, 272471756, -326751991, 269053879,
    50676799, 280400066, -343413670, 441361737, 219694500, 173964571,
    260083352, -216872702 };
static const int YESOD_NTT_IROOTS[24] = { 301989884, -301989884, 306948983,
    307583142, -49870511, -83114827, 188046825, 307336038, -336865748,
    -138951703, 446928399, 99752712, -405289730, 154089990, -249951332,
    -137071299, 266982935, -150170224, -312576529, 255387832, 323328994,
    -335667732, -447228759, -41915020 };

static int __yesod_rt_mont_reduce(i64 value) {
    int q = (int)value * MONT_U;
    return (int)((value + (i64)MOD * q) >> 32);
}

static int __yesod_rt_mint_from_int(int value) {
    return __yesod_rt_mont_reduce((i64)value * MONT_R2);
}

static int __yesod_rt_mint_to_int(int value) {
    int result = __yesod_rt_mont_reduce(value);
    return result < 0 ? result + MOD : result;
}

static int __yesod_rt_mint_fold(int value) {
    if (value < 0) {
        value += MOD * 2;
    }
    if (value >= MOD) {
        value -= MOD;
    }
    return value;
}

static int __yesod_rt_mint_add(int a, int b) {
    return __yesod_rt_mint_fold(a + b);
}

static int __yesod_rt_mint_sub(int a, int b) {
    return __yesod_rt_mint_fold(a - b);
}

static int __yesod_rt_mint_mul(int a, int b) {
    return __yesod_rt_mont_reduce((i64)a * b);
}

static int __yesod_rt_mint_pow(int base, int exp) {
    int result = MONT_R;
    while (exp > 0) {
        if ((exp & 1) != 0) {
            result = __yesod_rt_mint_mul(result, base);
        }
        base = __yesod_rt_mint_mul(base, base);
        exp >>= 1;
    }
    return result;
}

static int __yesod_rt_mint_inv(int value) {
    if (value == 0) {
        return 0;
    }
    return __yesod_rt_mint_pow(value, MOD - 2);
}

static int *__yesod_rt_alloc_ints(int count) {
    if (count <= 0) {
        return (int *)0;
    }
    return (int *)malloc((usize)count * sizeof(int));
}

void __yesod_rt_free_ints(int *ptr) {
    if (ptr != (int *)0) {
        free(ptr);
    }
}

static int __yesod_rt_next_pow2(int value) {
    if (value <= 1) {
        return 1;
    }
    int result = 1;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

static int __yesod_rt_mod_index(int value, int length) {
    int index = value % length;
    if (index < 0) {
        index += length;
    }
    return index;
}

int __yesod_poly_getcoeff_data(int *coeffs, int l, int r, int index) {
    if (!(index >= l && index < r)) {
        return 0;
    }
    return coeffs[index];
}

static int __yesod_rt_max_int(int a, int b) { return a > b ? a : b; }
static int __yesod_rt_min_int(int a, int b) { return a < b ? a : b; }

int *__yesod_poly_clone_data(int *addr, int n) {
    if (addr == (int *)0 || n <= 0) {
        return (int *)0;
    }
    int *out = __yesod_rt_alloc_ints(n);
    for (int i = 0; i < n; ++i) {
        out[i] = addr[i];
    }
    return out;
}

int __yesod_field_add(int lhs, int rhs) {
    return __yesod_rt_mint_add(lhs, rhs);
}

int __yesod_field_sub(int lhs, int rhs) {
    return __yesod_rt_mint_sub(lhs, rhs);
}

int __yesod_field_mul(int lhs, int rhs) {
    return __yesod_rt_mint_mul(lhs, rhs);
}

int __yesod_field_div(int lhs, int rhs) {
    return __yesod_rt_mint_mul(lhs, __yesod_rt_mint_inv(rhs));
}

int __yesod_next_pow2(int value) {
    return __yesod_rt_next_pow2(value);
}

int *__yesod_poly_alloc_zero_data(int l, int r) {
    if (l >= r) {
        return (int *)0;
    }
    int n = __yesod_rt_next_pow2(r - l);
    int *addr = __yesod_rt_alloc_ints(n);
    for (int i = 0; i < n; ++i) {
        addr[i] = 0;
    }
    return addr;
}

static void __yesod_rt_transform(int *out, const int *in, int length, int inverse) {
    if (length <= 0) {
        return;
    }
    for (int i = 0; i < length; ++i) {
        out[i] = in[i];
    }

    int logn = 0;
    int power = 1;
    while (power < length && logn < YESOD_MAX_LOGN) {
        ++logn;
        power <<= 1;
    }
    if (power != length) {
        return;
    }

    if (inverse == 0) {
        int k = logn;
        int len = length;
        int half = length / 2;
        while (k >= 1) {
            int wlen = YESOD_NTT_ROOTS[k];
            int i = 0;
            while (i < length) {
                int w = MONT_R;
                int j = 0;
                while (j < half) {
                    int u = out[i + j];
                    int v = out[i + j + half];
                    out[i + j] = __yesod_rt_mint_add(u, v);
                    out[i + j + half]
                        = __yesod_rt_mint_mul(__yesod_rt_mint_sub(u, v), w);
                    w = __yesod_rt_mint_mul(w, wlen);
                    ++j;
                }
                i += len;
            }
            len = half;
            half /= 2;
            --k;
        }
        return;
    }

    int k = 1;
    int len = 2;
    int half = 1;
    while (k <= logn) {
        int wlen = YESOD_NTT_IROOTS[k];
        int i = 0;
        while (i < length) {
            int w = MONT_R;
            int j = 0;
            while (j < half) {
                int u = out[i + j];
                int v = __yesod_rt_mint_mul(out[i + j + half], w);
                out[i + j] = __yesod_rt_mint_add(u, v);
                out[i + j + half] = __yesod_rt_mint_sub(u, v);
                w = __yesod_rt_mint_mul(w, wlen);
                ++j;
            }
            i += len;
        }
        ++k;
        half = len;
        len += len;
    }

    int invLength = __yesod_rt_mint_inv(__yesod_rt_mint_from_int(length));
    for (int i = 0; i < length; ++i) {
        out[i] = __yesod_rt_mint_mul(out[i], invLength);
    }
}

int *__yesod_poly_ntt_data(int *coeffs, int l, int r, int length) {
    int *values = __yesod_rt_alloc_ints(length);
    if (length <= 0) {
        return values;
    }
    int *folded = __yesod_rt_alloc_ints(length);
    for (int i = 0; i < length; ++i) {
        folded[i] = 0;
    }
    for (int i = l; i < r; ++i) {
        int foldedIndex = __yesod_rt_mod_index(i, length);
        folded[foldedIndex] = __yesod_rt_mint_add(
            folded[foldedIndex], __yesod_poly_getcoeff_data(coeffs, l, r, i));
    }
    __yesod_rt_transform(values, folded, length, 0);
    free(folded);
    return values;
}

int *__yesod_poly_from_pointwise_data(
    int *values, int len, int activeL, int activeR) {
    if (activeL >= activeR) {
        return (int *)0;
    }
    int *coeffs = __yesod_rt_alloc_ints(len);
    __yesod_rt_transform(coeffs, values, len, 1);
    int n = __yesod_rt_next_pow2(activeR - activeL);
    int *addr = __yesod_rt_alloc_ints(n);
    int *outCoeffs = addr - activeL;
    for (int i = 0; i < n; ++i) {
        addr[i] = 0;
    }
    for (int i = activeL; i < activeR; ++i) {
        outCoeffs[i] = coeffs[__yesod_rt_mod_index(i, len)];
    }
    free(coeffs);
    return addr;
}
)";

} // namespace yesod::backend

#endif // _YESOD_BACKEND_LLVM_POLY_RUNTIME_H_
