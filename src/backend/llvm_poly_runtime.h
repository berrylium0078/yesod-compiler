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

static int __yesod_rt_poly_coeff_raw(const YesodPoly *poly, int index) {
    if (!(index >= poly->l && index < poly->r)) {
        return 0;
    }
    return poly->coeffs[index];
}

static void __yesod_rt_poly_zero(YesodPoly *out) {
    out->coeffs = (int *)0;
    out->addr = (int *)0;
    out->n = 0;
    out->l = 0;
    out->r = 0;
}

void __yesod_poly_drop(YesodPoly *poly) {
    if (poly->addr != (int *)0) {
        free(poly->addr);
    }
    __yesod_rt_poly_zero(poly);
}

void __yesod_pv_drop(YesodPointValues *pv) {
    if (pv->values != (int *)0) {
        free(pv->values);
    }
    pv->values = (int *)0;
    pv->len = 0;
}

void __yesod_pv_alloc(YesodPointValues *out, int length) {
    out->len = length;
    out->values = __yesod_rt_alloc_ints(length);
}

static int __yesod_rt_max_int(int a, int b) { return a > b ? a : b; }
static int __yesod_rt_min_int(int a, int b) { return a < b ? a : b; }

void __yesod_poly_clone(YesodPoly *out, const YesodPoly *input) {
    out->l = input->l;
    out->r = input->r;
    out->n = input->n;
    if (input->addr == (int *)0 || input->n <= 0) {
        out->coeffs = (int *)0;
        out->addr = (int *)0;
        out->n = 0;
        return;
    }
    i64 offset = input->coeffs - input->addr;
    out->addr = __yesod_rt_alloc_ints(input->n);
    out->coeffs = out->addr + offset;
    for (int i = 0; i < input->n; ++i) {
        out->addr[i] = input->addr[i];
    }
}

void __yesod_poly_construct(YesodPoly *out, int *coeffs, int count) {
    out->l = 0;
    out->r = count;
    if (count <= 0) {
        out->coeffs = (int *)0;
        out->addr = (int *)0;
        out->n = 0;
        return;
    }
    out->n = __yesod_rt_next_pow2(count);
    out->addr = __yesod_rt_alloc_ints(out->n);
    out->coeffs = out->addr;
    for (int i = 0; i < out->n; ++i) {
        out->addr[i] = 0;
    }
    for (int i = 0; i < count; ++i) {
        out->coeffs[i] = coeffs[i];
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

int __yesod_next_pow2(int value) {
    return __yesod_rt_next_pow2(value);
}

void __yesod_poly_alloc_zero(YesodPoly *out, int l, int r) {
    if (l >= r) {
        __yesod_rt_poly_zero(out);
        return;
    }
    out->l = l;
    out->r = r;
    out->n = __yesod_rt_next_pow2(r - l);
    out->addr = __yesod_rt_alloc_ints(out->n);
    out->coeffs = out->addr - l;
    for (int i = 0; i < out->n; ++i) {
        out->addr[i] = 0;
    }
}

static void __yesod_rt_transform(int *out, const int *in, int length, int inverse) {
    if (length <= 0) {
        return;
    }
    int root = __yesod_rt_mint_pow(__yesod_rt_mint_from_int(3), (MOD - 1) / length);
    if (inverse != 0) {
        root = __yesod_rt_mint_inv(root);
    }
    for (int j = 0; j < length; ++j) {
        int power = MONT_R;
        int step = __yesod_rt_mint_pow(root, j);
        int sum = 0;
        for (int k = 0; k < length; ++k) {
            sum = __yesod_rt_mint_add(sum, __yesod_rt_mint_mul(in[k], power));
            power = __yesod_rt_mint_mul(power, step);
        }
        out[j] = sum;
    }
    if (inverse != 0) {
        int invLength = __yesod_rt_mint_inv(__yesod_rt_mint_from_int(length));
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
    for (int i = poly->l; i < poly->r; ++i) {
        int foldedIndex = __yesod_rt_mod_index(i, length);
        folded[foldedIndex] = __yesod_rt_mint_add(
            folded[foldedIndex], __yesod_rt_poly_coeff_raw(poly, i));
    }
    __yesod_rt_transform(out->values, folded, length, 0);
    free(folded);
}

void __yesod_poly_from_pointwise(
    YesodPoly *out, const YesodPointValues *pv, int activeL, int activeR) {
    if (activeL >= activeR) {
        __yesod_rt_poly_zero(out);
        return;
    }
    int *coeffs = __yesod_rt_alloc_ints(pv->len);
    __yesod_rt_transform(coeffs, pv->values, pv->len, 1);
    out->l = activeL;
    out->r = activeR;
    out->n = __yesod_rt_next_pow2(activeR - activeL);
    out->addr = __yesod_rt_alloc_ints(out->n);
    out->coeffs = out->addr - activeL;
    for (int i = 0; i < out->n; ++i) {
        out->addr[i] = 0;
    }
    for (int i = activeL; i < activeR; ++i) {
        out->coeffs[i] = coeffs[__yesod_rt_mod_index(i, pv->len)];
    }
    free(coeffs);
}

void __yesod_poly_take_pointwise(
    YesodPoly *out, YesodPointValues *pv, int activeL, int activeR) {
    if (activeL >= activeR) {
        __yesod_rt_poly_zero(out);
        __yesod_pv_drop(pv);
        return;
    }
    int *input = __yesod_rt_alloc_ints(pv->len);
    for (int i = 0; i < pv->len; ++i) {
        input[i] = pv->values[i];
    }
    __yesod_rt_transform(pv->values, input, pv->len, 1);
    free(input);

    int activeLen = activeR - activeL;
    int start = __yesod_rt_mod_index(activeL, pv->len);
    if (start + activeLen <= pv->len && activeLen * 4 >= pv->len) {
        out->l = activeL;
        out->r = activeR;
        out->n = pv->len;
        out->addr = pv->values;
        out->coeffs = pv->values + start - activeL;
        pv->values = (int *)0;
        pv->len = 0;
        return;
    }

    out->l = activeL;
    out->r = activeR;
    out->n = __yesod_rt_next_pow2(activeLen);
    out->addr = __yesod_rt_alloc_ints(out->n);
    out->coeffs = out->addr - activeL;
    for (int i = 0; i < out->n; ++i) {
        out->addr[i] = 0;
    }
    for (int i = activeL; i < activeR; ++i) {
        out->coeffs[i] = pv->values[__yesod_rt_mod_index(i, pv->len)];
    }
    __yesod_pv_drop(pv);
}
)";

} // namespace yesod::backend

#endif // _YESOD_BACKEND_LLVM_POLY_RUNTIME_H_
