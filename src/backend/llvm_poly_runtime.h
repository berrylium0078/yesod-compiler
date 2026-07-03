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
static const int YESOD_PRIMITIVE_ROOT = 3;
static int *yesodNttRoots = (int *)0;
static int yesodNttRootCapacity = 0;
static int yesodNttNextRootLim = 1;

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

static void __yesod_rt_reserve_ntt_roots(int count) {
    if (yesodNttRootCapacity >= count) {
        return;
    }
    int newCapacity = 1;
    while (newCapacity < count) {
        newCapacity <<= 1;
    }
    int *newRoots = (int *)malloc((usize)newCapacity * sizeof(int));
    for (int i = 0; i < newCapacity; ++i) {
        newRoots[i] = 0;
    }
    for (int i = 0; i < yesodNttRootCapacity; ++i) {
        newRoots[i] = yesodNttRoots[i];
    }
    if (yesodNttRoots != (int *)0) {
        free(yesodNttRoots);
    }
    yesodNttRoots = newRoots;
    yesodNttRootCapacity = newCapacity;
    yesodNttRoots[0] = MONT_R;
}

static void __yesod_rt_prepare_ntt_roots(int targetLim) {
    __yesod_rt_reserve_ntt_roots(1);
    if (targetLim < 1 || yesodNttNextRootLim > targetLim) {
        return;
    }
    __yesod_rt_reserve_ntt_roots(targetLim << 1);
    int root = __yesod_rt_mint_from_int(YESOD_PRIMITIVE_ROOT);
    while (yesodNttNextRootLim <= targetLim) {
        int step = __yesod_rt_mint_pow(
            root, (MOD >> 2) / yesodNttNextRootLim);
        for (int i = 0; i < yesodNttNextRootLim; ++i) {
            yesodNttRoots[yesodNttNextRootLim + i]
                = __yesod_rt_mint_mul(yesodNttRoots[i], step);
        }
        yesodNttNextRootLim <<= 1;
    }
}

__attribute__((used)) int *__yesod_rt_alloc_ints(int count) {
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

    __yesod_rt_prepare_ntt_roots(length >> 2);

    if (inverse == 0) {
        int mid = length >> 1;
        while (mid > 0) {
            int w = 0;
            int i = 0;
            while (i < length) {
                int root = yesodNttRoots[w];
                int j = 0;
                while (j < mid) {
                    int left = out[i + j];
                    int right = __yesod_rt_mint_mul(root, out[i + mid + j]);
                    out[i + j] = __yesod_rt_mint_add(left, right);
                    out[i + mid + j] = __yesod_rt_mint_sub(left, right);
                    ++j;
                }
                i += mid << 1;
                ++w;
            }
            mid >>= 1;
        }
        return;
    }

    int mid = 1;
    while (mid < length) {
        int w = 0;
        int i = 0;
        while (i < length) {
            int j = 0;
            int root = yesodNttRoots[w];
            while (j < mid) {
                int left = out[i + j];
                int right = out[i + mid + j];
                out[i + j] = __yesod_rt_mint_add(left, right);
                out[i + mid + j]
                    = __yesod_rt_mint_mul(root, __yesod_rt_mint_sub(left, right));
                ++j;
            }
            i += mid << 1;
            ++w;
        }
        mid <<= 1;
    }

    for (int left = 1, right = length - 1; left < right; ++left, --right) {
        int tmp = out[left];
        out[left] = out[right];
        out[right] = tmp;
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
