#ifndef _YESOD_BACKEND_LLVM_POLY_RUNTIME_H_
#define _YESOD_BACKEND_LLVM_POLY_RUNTIME_H_

#include <string_view>

namespace yesod::backend {

inline constexpr std::string_view LLVM_POLY_RUNTIME_SOURCE = R"(
typedef long long i64;
typedef unsigned long long u64;
typedef unsigned long usize;

extern void *malloc(usize size);

typedef struct YesodPoly {
    int *coeffs;
    int len;
    int l;
    int r;
} YesodPoly;

typedef struct YesodPointValues {
    int *values;
    int len;
} YesodPointValues;

static const int MOD = 998244353;

static int __yesod_rt_mint_norm(i64 value) {
    value %= MOD;
    if (value < 0) {
        value += MOD;
    }
    return (int)value;
}

static int __yesod_rt_mint_add(int a, int b) { return __yesod_rt_mint_norm((i64)a + b); }
static int __yesod_rt_mint_sub(int a, int b) { return __yesod_rt_mint_norm((i64)a - b); }
static int __yesod_rt_mint_mul(int a, int b) { return __yesod_rt_mint_norm((i64)a * b); }

static int __yesod_rt_mint_pow(int base, int exp) {
    int result = 1;
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

static int __yesod_rt_poly_coeff_raw(const YesodPoly *poly, int index) {
    if (poly->coeffs == (int *)0 || index < 0 || index >= poly->len) {
        return 0;
    }
    if (poly->l >= poly->r || index < poly->l || index >= poly->r) {
        return 0;
    }
    return poly->coeffs[index];
}

static void __yesod_rt_poly_zero(YesodPoly *out) {
    out->coeffs = (int *)0;
    out->len = 0;
    out->l = 0;
    out->r = 0;
}

static int __yesod_rt_max_int(int a, int b) { return a > b ? a : b; }
static int __yesod_rt_min_int(int a, int b) { return a < b ? a : b; }

void __yesod_poly_clone(YesodPoly *out, const YesodPoly *input) {
    out->len = input->len;
    out->l = input->l;
    out->r = input->r;
    out->coeffs = __yesod_rt_alloc_ints(input->len);
    for (int i = 0; i < input->len; ++i) {
        out->coeffs[i] = __yesod_rt_poly_coeff_raw(input, i);
    }
}

void __yesod_poly_construct(YesodPoly *out, int *coeffs, int count) {
    out->len = count;
    out->l = 0;
    out->r = count;
    out->coeffs = __yesod_rt_alloc_ints(count);
    for (int i = 0; i < count; ++i) {
        out->coeffs[i] = __yesod_rt_mint_norm(coeffs[i]);
    }
}

int __yesod_poly_getcoeff(const YesodPoly *poly, int index) {
    return __yesod_rt_poly_coeff_raw(poly, index);
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

void __yesod_poly_set_l(YesodPoly *out, const YesodPoly *input, int value) {
    __yesod_poly_clone(out, input);
    out->l = value;
}

void __yesod_poly_set_r(YesodPoly *out, const YesodPoly *input, int value) {
    __yesod_poly_clone(out, input);
    out->r = value;
}

void __yesod_poly_set_ptr(YesodPoly *out, const YesodPoly *input, int *ptr) {
    out->coeffs = ptr;
    out->len = input->len;
    out->l = input->l;
    out->r = input->r;
}

int __yesod_next_pow2(int value) {
    if (value <= 1) {
        return 1;
    }
    int result = 1;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

void __yesod_poly_combine_term(YesodPoly *out, const YesodPoly *acc,
    const YesodPoly *src, int start, int end, int shift, int scale) {
    int srcEnd = src->r;
    if (end < srcEnd) {
        srcEnd = end;
    }
    int lower = __yesod_rt_max_int(start, src->l);
    int upper = srcEnd;
    int termLen = upper - shift;
    if (upper <= lower) {
        termLen = 0;
    }
    int resultLen = __yesod_rt_max_int(acc->len, termLen);
    if (resultLen <= 0) {
        __yesod_rt_poly_zero(out);
        return;
    }
    out->coeffs = __yesod_rt_alloc_ints(resultLen);
    out->len = resultLen;
    out->l = 0;
    out->r = resultLen;
    for (int i = 0; i < resultLen; ++i) {
        out->coeffs[i] = __yesod_rt_poly_coeff_raw(acc, i);
    }
    if (upper <= lower) {
        return;
    }
    for (int j = lower; j < upper; ++j) {
        int dst = j - shift;
        if (dst >= 0 && dst < resultLen) {
            out->coeffs[dst]
                = __yesod_rt_mint_add(out->coeffs[dst],
                    __yesod_rt_mint_mul(__yesod_rt_poly_coeff_raw(src, j), scale));
        }
    }
}

static void __yesod_rt_transform(int *out, const int *in, int length, int inverse) {
    if (length <= 0) {
        return;
    }
    int root = __yesod_rt_mint_pow(3, (MOD - 1) / length);
    if (inverse != 0) {
        root = __yesod_rt_mint_inv(root);
    }
    for (int j = 0; j < length; ++j) {
        int power = 1;
        int step = __yesod_rt_mint_pow(root, j);
        int sum = 0;
        for (int k = 0; k < length; ++k) {
            sum = __yesod_rt_mint_add(sum, __yesod_rt_mint_mul(in[k], power));
            power = __yesod_rt_mint_mul(power, step);
        }
        out[j] = sum;
    }
    if (inverse != 0) {
        int invLength = __yesod_rt_mint_inv(length);
        for (int i = 0; i < length; ++i) {
            out[i] = __yesod_rt_mint_mul(out[i], invLength);
        }
    }
}

void __yesod_poly_ntt(
    YesodPointValues *out, const YesodPoly *poly, int length) {
    out->len = length;
    out->values = __yesod_rt_alloc_ints(length);
    if (length <= 0) {
        return;
    }
    int *folded = __yesod_rt_alloc_ints(length);
    for (int i = 0; i < length; ++i) {
        folded[i] = 0;
    }
    for (int i = 0; i < poly->len; ++i) {
        folded[i % length] = __yesod_rt_mint_add(
            folded[i % length], __yesod_rt_poly_coeff_raw(poly, i));
    }
    __yesod_rt_transform(out->values, folded, length, 0);
}

void __yesod_pv_add(YesodPointValues *out, const YesodPointValues *lhs,
    const YesodPointValues *rhs) {
    out->len = lhs->len;
    out->values = __yesod_rt_alloc_ints(out->len);
    for (int i = 0; i < out->len; ++i) {
        out->values[i] = __yesod_rt_mint_add(lhs->values[i], rhs->values[i]);
    }
}

void __yesod_pv_sub(YesodPointValues *out, const YesodPointValues *lhs,
    const YesodPointValues *rhs) {
    out->len = lhs->len;
    out->values = __yesod_rt_alloc_ints(out->len);
    for (int i = 0; i < out->len; ++i) {
        out->values[i] = __yesod_rt_mint_sub(lhs->values[i], rhs->values[i]);
    }
}

void __yesod_pv_mul(YesodPointValues *out, const YesodPointValues *lhs,
    const YesodPointValues *rhs) {
    out->len = lhs->len;
    out->values = __yesod_rt_alloc_ints(out->len);
    for (int i = 0; i < out->len; ++i) {
        out->values[i] = __yesod_rt_mint_mul(lhs->values[i], rhs->values[i]);
    }
}

void __yesod_pv_times(YesodPointValues *out, const YesodPointValues *lhs,
    int scale) {
    out->len = lhs->len;
    out->values = __yesod_rt_alloc_ints(out->len);
    for (int i = 0; i < out->len; ++i) {
        out->values[i] = __yesod_rt_mint_mul(lhs->values[i], scale);
    }
}

void __yesod_poly_from_pointwise(
    YesodPoly *out, const YesodPointValues *pv, int activeL, int activeR) {
    if (activeL >= activeR) {
        __yesod_rt_poly_zero(out);
        return;
    }
    int *coeffs = __yesod_rt_alloc_ints(pv->len);
    __yesod_rt_transform(coeffs, pv->values, pv->len, 1);
    out->len = activeR;
    out->l = activeL;
    out->r = activeR;
    out->coeffs = __yesod_rt_alloc_ints(activeR);
    for (int i = 0; i < activeR; ++i) {
        out->coeffs[i] = 0;
    }
    for (int i = activeL; i < activeR; ++i) {
        out->coeffs[i] = coeffs[i % pv->len];
    }
}
)";

} // namespace yesod::backend

#endif // _YESOD_BACKEND_LLVM_POLY_RUNTIME_H_
