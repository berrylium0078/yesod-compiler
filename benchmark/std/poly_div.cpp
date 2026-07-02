// https://www.luogu.com.cn/record/276499114
#include <bits/stdc++.h>
#include <cassert>
using namespace std;
class Polynomial {
public:
typedef long long i64;
typedef vector<int> poly;
enum class MulBackend { AUTO, NTT, CRT };
private:
enum { MOD1 = 998244353, MOD2 = 1004535809, MOD3 = 469762049 };
typedef unsigned int u32;
typedef unsigned long long u64;
typedef __uint128_t u128;
struct Barrett {
u32 m;
u64 im;
Barrett(u32 mod = MOD1) : m(mod), im((u64)(-1) / mod + 1) {}
inline int mod() const { return (int)m; }
inline int reduce(u64 z) const {
u64 x = (u64)((u128)z * im >> 64);
u64 y = x * m;
u64 r = z - y;
if (z < y) r += m;
if (r >= m) r -= m;
if (r >= m) r -= m;
return (int)r;
}
inline int mul(int a, int b) const {
return reduce((u64)(u32)a * (u32)b);
}
};
template<int MOD>
struct StaticMod {
static inline u64 im() {
static const u64 v = (u64)(-1) / (u32)MOD + 1;
return v;
}
static inline int reduce(u64 z) {
u64 x = (u64)((u128)z * im() >> 64);
u64 y = x * (u32)MOD;
u64 r = z - y;
if (z < y) r += (u32)MOD;
if (r >= (u32)MOD) r -= (u32)MOD;
if (r >= (u32)MOD) r -= (u32)MOD;
return (int)r;
}
static inline int add(int x, int y) {
x += y;
return x >= MOD ? x - MOD : x;
}
static inline int sub(int x, int y) {
x -= y;
return x < 0 ? x + MOD : x;
}
static inline int mul(int x, int y) {
return reduce((u64)(u32)x * (u32)y);
}
static inline int norm(long long x) {
x %= MOD;
if (x < 0) x += MOD;
return (int)x;
}
};
struct TempPool {
static inline int lg_cap(int n) {
int c = 1, lg = 0;
while (c < n) c <<= 1, ++lg;
return lg;
}
static vector<poly>* buckets() {
static thread_local vector<poly> pool[31];
return pool;
}
static poly acquire(int n, bool zero = true) {
if (n <= 0) return poly();
int lg = lg_cap(n);
vector<poly>* pool = buckets();
poly v;
if (lg < 31 && !pool[lg].empty()) {
v.swap(pool[lg].back());
pool[lg].pop_back();
v.resize(n);
if (zero) memset(&v[0], 0, sizeof(int) * n);
} else {
v.assign(n, 0);
}
return v;
}
static void release(poly& v) {
if (v.empty() && v.capacity() == 0) return;
int cap = (int)v.capacity();
if (cap <= 0) return;
int lg = lg_cap(cap);
if (lg >= 31) { poly().swap(v); return; }
vector<poly>* pool = buckets();
v.clear();
if ((int)pool[lg].size() < 8) pool[lg].push_back(poly()), pool[lg].back().swap(v);
else poly().swap(v);
}
};
struct TempVec {
poly v;
bool owned;
explicit TempVec(int n = 0, bool zero = true) : v(TempPool::acquire(n, zero)), owned(true) {}
~TempVec() { if (owned) TempPool::release(v); }
poly& get() { return v; }
const poly& get() const { return v; }
poly release() { owned = false; return std::move(v); }
};
int mod_;
Barrett bt_;
vector<int> invs_;
mutable bool info_ready_;
mutable bool prime_cache_;
mutable int max_ntt_len_cache_;
mutable int primitive_root_cache_;
mutable int rev_cache_n_;
mutable vector<int> rev_cache_;
static inline void trim(poly& a) {
while (!a.empty() && a.back() == 0) a.pop_back();
}
inline int norm(long long x) const {
x %= mod_;
if (x < 0) x += mod_;
return (int)x;
}
inline int addmod(int x, int y) const {
x += y;
return x >= mod_ ? x - mod_ : x;
}
inline int submod(int x, int y) const {
x -= y;
return x < 0 ? x + mod_ : x;
}
inline int mulmod(int x, int y) const {
return bt_.mul(x, y);
}
static int qpow_mod(int a, long long b, int mod) {
Barrett bt((u32)mod);
int r = 1;
int x = a % mod;
if (x < 0) x += mod;
while (b) {
if (b & 1) r = bt.mul(r, x);
x = bt.mul(x, x);
b >>= 1;
}
return r;
}
template<int MOD>
static int qpow_static(int a, long long b) {
int r = 1;
int x = StaticMod<MOD>::norm(a);
while (b) {
if (b & 1) r = StaticMod<MOD>::mul(r, x);
x = StaticMod<MOD>::mul(x, x);
b >>= 1;
}
return r;
}
static inline int ceil_pow2_int(int n) {
int x = 0;
while ((1 << x) < n) ++x;
return x;
}
static inline int countr_zero_u32(unsigned int x) {
return __builtin_ctz(x);
}
int qpow(int a, long long b) const {
int r = 1;
int x = norm(a);
while (b) {
if (b & 1) r = mulmod(r, x);
x = mulmod(x, x);
b >>= 1;
}
return r;
}
static bool is_prime_u64(unsigned long long n) {
if (n < 2) return false;
static const unsigned long long small_primes[] = {
2ULL,3ULL,5ULL,7ULL,11ULL,13ULL,17ULL,19ULL,23ULL,29ULL,31ULL,37ULL
};
for (int i = 0; i < 12; ++i) {
unsigned long long p = small_primes[i];
if (n % p == 0) return n == p;
}
struct Local {
static inline unsigned long long mul(unsigned long long a, unsigned long long b, unsigned long long mod) {
return (unsigned long long)((__uint128_t)a * b % mod);
}
static unsigned long long pow(unsigned long long a, unsigned long long e, unsigned long long mod) {
unsigned long long r = 1;
while (e) {
if (e & 1) r = mul(r, a, mod);
a = mul(a, a, mod);
e >>= 1;
}
return r;
}
};
unsigned long long d = n - 1, s = 0;
while ((d & 1) == 0) d >>= 1, ++s;
static const unsigned long long bases[] = {
2ULL, 325ULL, 9375ULL, 28178ULL, 450775ULL, 9780504ULL, 1795265022ULL
};
for (int i = 0; i < 7; ++i) {
unsigned long long a = bases[i] % n;
if (a == 0) continue;
unsigned long long x = Local::pow(a, d, n);
if (x == 1 || x == n - 1) continue;
bool composite = true;
for (unsigned long long r = 1; r < s; ++r) {
x = Local::mul(x, x, n);
if (x == n - 1) { composite = false; break; }
}
if (composite) return false;
}
return true;
}
int calc_primitive_root() const {
if (mod_ == 2) return 1;
if (mod_ == MOD1 || mod_ == MOD2 || mod_ == MOD3) return 3;
int phi = mod_ - 1, x = phi;
vector<int> fac;
for (int i = 2; 1LL * i * i <= x; ++i) {
if (x % i == 0) {
fac.push_back(i);
while (x % i == 0) x /= i;
}
}
if (x > 1) fac.push_back(x);
for (int g = 2; ; ++g) {
bool ok = true;
for (int i = 0; i < (int)fac.size(); ++i) {
if (qpow(g, phi / fac[i]) == 1) { ok = false; break; }
}
if (ok) return g;
}
}
void prepare_mod_info() const {
if (info_ready_) return;
prime_cache_ = is_prime_u64((unsigned long long)mod_);
max_ntt_len_cache_ = 0;
primitive_root_cache_ = 0;
if (prime_cache_) {
int x = mod_ - 1, pw2 = 0;
while ((x & 1) == 0) x >>= 1, ++pw2;
max_ntt_len_cache_ = 1 << pw2;
primitive_root_cache_ = calc_primitive_root();
}
info_ready_ = true;
}
bool ntt_legal(int need_len) const {
prepare_mod_info();
return prime_cache_ && max_ntt_len_cache_ >= need_len;
}
int primitive_root() const {
prepare_mod_info();
return primitive_root_cache_;
}
const vector<int>& get_rev(int n) const {
if (rev_cache_n_ == n) return rev_cache_;
rev_cache_n_ = n;
rev_cache_.assign(n, 0);
for (int i = 1; i < n; ++i) {
rev_cache_[i] = (rev_cache_[i >> 1] >> 1) | ((i & 1) ? (n >> 1) : 0);
}
return rev_cache_;
}
void ntt(poly& a, bool inv) const {
const int n = (int)a.size();
const vector<int>& rev = get_rev(n);
for (int i = 1; i < n; ++i) {
int j = rev[i];
if (i < j) swap(a[i], a[j]);
}
const int root = primitive_root();
const int root_inv = inv ? qpow(root, mod_ - 2) : 0;
const int base = inv ? root_inv : root;
for (int len = 2; len <= n; len <<= 1) {
const int wn = qpow(base, (mod_ - 1) / len);
const int half = len >> 1;
for (int i = 0; i < n; i += len) {
int w = 1;
int* p = &a[i];
for (int j = 0; j < half; ++j) {
int u = p[j];
int v = mulmod(p[j + half], w);
p[j] = addmod(u, v);
p[j + half] = submod(u, v);
w = mulmod(w, wn);
}
}
}
if (inv) {
int invn = qpow(n, mod_ - 2);
for (int i = 0; i < n; ++i) a[i] = mulmod(a[i], invn);
}
}
template<int MOD, int ROOT>
static void prepare_radix2_rates(int* sum_e, int* sum_ie) {
int cnt2 = countr_zero_u32((unsigned int)(MOD - 1));
int e = qpow_static<MOD>(ROOT, (MOD - 1) >> cnt2);
int ie = qpow_static<MOD>(e, MOD - 2);
int es[30], ies[30];
for (int i = cnt2; i >= 2; --i) {
es[i - 2] = e;
ies[i - 2] = ie;
e = StaticMod<MOD>::mul(e, e);
ie = StaticMod<MOD>::mul(ie, ie);
}
int now = 1, inow = 1;
for (int i = 0; i <= cnt2 - 2; ++i) {
sum_e[i] = StaticMod<MOD>::mul(es[i], now);
now = StaticMod<MOD>::mul(now, ies[i]);
sum_ie[i] = StaticMod<MOD>::mul(ies[i], inow);
inow = StaticMod<MOD>::mul(inow, es[i]);
}
}
template<int MOD, int ROOT>
static void butterfly_fixed_radix2(poly& a) {
const int n = (int)a.size();
if (n <= 1) return;
const int h = ceil_pow2_int(n);
static bool inited = false;
static int sum_e[30], sum_ie_dummy[30];
if (!inited) {
prepare_radix2_rates<MOD, ROOT>(sum_e, sum_ie_dummy);
inited = true;
}
for (int ph = 1; ph <= h; ++ph) {
const int w = 1 << (ph - 1);
const int p = 1 << (h - ph);
int now = 1;
for (int s = 0; s < w; ++s) {
const int offset = s << (h - ph + 1);
int* left = &a[offset];
int* right = left + p;
for (int i = 0; i < p; ++i) {
int l = left[i];
int r = StaticMod<MOD>::mul(right[i], now);
left[i] = StaticMod<MOD>::add(l, r);
right[i] = StaticMod<MOD>::sub(l, r);
}
now = StaticMod<MOD>::mul(now, sum_e[countr_zero_u32(~(unsigned int)s)]);
}
}
}
template<int MOD, int ROOT>
static void butterfly_inv_fixed_radix2(poly& a) {
const int n = (int)a.size();
if (n <= 1) return;
const int h = ceil_pow2_int(n);
static bool inited = false;
static int sum_e_dummy[30], sum_ie[30];
if (!inited) {
prepare_radix2_rates<MOD, ROOT>(sum_e_dummy, sum_ie);
inited = true;
}
for (int ph = h; ph >= 1; --ph) {
const int w = 1 << (ph - 1);
const int p = 1 << (h - ph);
int inow = 1;
for (int s = 0; s < w; ++s) {
const int offset = s << (h - ph + 1);
int* left = &a[offset];
int* right = left + p;
for (int i = 0; i < p; ++i) {
int l = left[i];
int r = right[i];
left[i] = StaticMod<MOD>::add(l, r);
right[i] = StaticMod<MOD>::mul(StaticMod<MOD>::sub(l, r), inow);
}
inow = StaticMod<MOD>::mul(inow, sum_ie[countr_zero_u32(~(unsigned int)s)]);
}
}
}
template<int MOD, int ROOT>
struct Radix4Info {
int rate2[30], irate2[30], rate3[30], irate3[30];
int imag, iimag;
Radix4Info() {
const int rank2 = countr_zero_u32((unsigned int)(MOD - 1));
int root[31], iroot[31];
root[rank2] = qpow_static<MOD>(ROOT, (MOD - 1) >> rank2);
iroot[rank2] = qpow_static<MOD>(root[rank2], MOD - 2);
for (int i = rank2 - 1; i >= 0; --i) {
root[i] = StaticMod<MOD>::mul(root[i + 1], root[i + 1]);
iroot[i] = StaticMod<MOD>::mul(iroot[i + 1], iroot[i + 1]);
}
imag = root[2];
iimag = iroot[2];
int prod = 1, iprod = 1;
for (int i = 0; i <= rank2 - 2; ++i) {
rate2[i] = StaticMod<MOD>::mul(root[i + 2], prod);
irate2[i] = StaticMod<MOD>::mul(iroot[i + 2], iprod);
prod = StaticMod<MOD>::mul(prod, iroot[i + 2]);
iprod = StaticMod<MOD>::mul(iprod, root[i + 2]);
}
prod = 1; iprod = 1;
for (int i = 0; i <= rank2 - 3; ++i) {
rate3[i] = StaticMod<MOD>::mul(root[i + 3], prod);
irate3[i] = StaticMod<MOD>::mul(iroot[i + 3], iprod);
prod = StaticMod<MOD>::mul(prod, iroot[i + 3]);
iprod = StaticMod<MOD>::mul(iprod, root[i + 3]);
}
}
};
template<int MOD, int ROOT>
static const Radix4Info<MOD, ROOT>& radix4_info() {
static Radix4Info<MOD, ROOT> info;
return info;
}
template<int MOD, int ROOT>
static void butterfly_fixed(poly& a) {
const int n = (int)a.size();
if (n <= 1) return;
const int h = ceil_pow2_int(n);
const Radix4Info<MOD, ROOT>& info = radix4_info<MOD, ROOT>();
for (int ph = 1; ph <= h; ) {
if (h - ph <= 1) {
const int p = 1 << (h - ph);
const int w = 1 << (ph - 1);
int now = 1;
for (int s = 0; s < w; ++s) {
const int offset = s << (h - ph + 1);
int* x = &a[offset];
int* y = x + p;
for (int i = 0; i < p; ++i) {
int l = x[i];
int r = StaticMod<MOD>::mul(y[i], now);
x[i] = StaticMod<MOD>::add(l, r);
y[i] = StaticMod<MOD>::sub(l, r);
}
now = StaticMod<MOD>::mul(now, info.rate2[countr_zero_u32(~(unsigned int)s)]);
}
++ph;
} else {
const int p = 1 << (h - ph - 1);
const int w = 1 << (ph - 1);
int now = 1;
const int imag = info.imag;
for (int s = 0; s < w; ++s) {
const int offset = s << (h - ph + 1);
const int now2 = StaticMod<MOD>::mul(now, now);
const int now3 = StaticMod<MOD>::mul(now2, now);
int* a0p = &a[offset];
int* a1p = a0p + p;
int* a2p = a1p + p;
int* a3p = a2p + p;
for (int i = 0; i < p; ++i) {
const int a0 = a0p[i];
const int a1 = StaticMod<MOD>::mul(a1p[i], now);
const int a2 = StaticMod<MOD>::mul(a2p[i], now2);
const int a3 = StaticMod<MOD>::mul(a3p[i], now3);
const int a0pa2 = StaticMod<MOD>::add(a0, a2);
const int a0ma2 = StaticMod<MOD>::sub(a0, a2);
const int a1pa3 = StaticMod<MOD>::add(a1, a3);
const int a1ma3imag = StaticMod<MOD>::mul(StaticMod<MOD>::sub(a1, a3), imag);
a0p[i] = StaticMod<MOD>::add(a0pa2, a1pa3);
a1p[i] = StaticMod<MOD>::sub(a0pa2, a1pa3);
a2p[i] = StaticMod<MOD>::add(a0ma2, a1ma3imag);
a3p[i] = StaticMod<MOD>::sub(a0ma2, a1ma3imag);
}
now = StaticMod<MOD>::mul(now, info.rate3[countr_zero_u32(~(unsigned int)s)]);
}
ph += 2;
}
}
}
template<int MOD, int ROOT>
static void butterfly_inv_fixed(poly& a) {
const int n = (int)a.size();
if (n <= 1) return;
const int h = ceil_pow2_int(n);
const Radix4Info<MOD, ROOT>& info = radix4_info<MOD, ROOT>();
for (int ph = h; ph >= 1; ) {
if (ph == 1) {
const int p = 1 << (h - ph);
const int w = 1 << (ph - 1);
int inow = 1;
for (int s = 0; s < w; ++s) {
const int offset = s << (h - ph + 1);
int* x = &a[offset];
int* y = x + p;
for (int i = 0; i < p; ++i) {
int l = x[i];
int r = y[i];
x[i] = StaticMod<MOD>::add(l, r);
y[i] = StaticMod<MOD>::mul(StaticMod<MOD>::sub(l, r), inow);
}
inow = StaticMod<MOD>::mul(inow, info.irate2[countr_zero_u32(~(unsigned int)s)]);
}
--ph;
} else {
const int p = 1 << (h - ph);
const int w = 1 << (ph - 2);
int inow = 1;
const int iimag = info.iimag;
for (int s = 0; s < w; ++s) {
const int offset = s << (h - ph + 2);
const int inow2 = StaticMod<MOD>::mul(inow, inow);
const int inow3 = StaticMod<MOD>::mul(inow2, inow);
int* a0p = &a[offset];
int* a1p = a0p + p;
int* a2p = a1p + p;
int* a3p = a2p + p;
for (int i = 0; i < p; ++i) {
const int a0 = a0p[i];
const int a1 = a1p[i];
const int a2 = a2p[i];
const int a3 = a3p[i];
const int a0pa1 = StaticMod<MOD>::add(a0, a1);
const int a0ma1 = StaticMod<MOD>::sub(a0, a1);
const int a2pa3 = StaticMod<MOD>::add(a2, a3);
const int a2ma3iimag = StaticMod<MOD>::mul(StaticMod<MOD>::sub(a2, a3), iimag);
a0p[i] = StaticMod<MOD>::add(a0pa1, a2pa3);
a1p[i] = StaticMod<MOD>::mul(StaticMod<MOD>::add(a0ma1, a2ma3iimag), inow);
a2p[i] = StaticMod<MOD>::mul(StaticMod<MOD>::sub(a0pa1, a2pa3), inow2);
a3p[i] = StaticMod<MOD>::mul(StaticMod<MOD>::sub(a0ma1, a2ma3iimag), inow3);
}
inow = StaticMod<MOD>::mul(inow, info.irate3[countr_zero_u32(~(unsigned int)s)]);
}
ph -= 2;
}
}
}
template<int MOD, int ROOT>
static void ntt_fixed(poly& a, bool inv) {
#ifndef POLY_FORCE_RADIX4_NTT
const bool use_radix4 = ((int)a.size() >= 8192);
#else
const bool use_radix4 = true;
#endif
if (!inv) {
if (use_radix4) butterfly_fixed<MOD, ROOT>(a);
else butterfly_fixed_radix2<MOD, ROOT>(a);
} else {
if (use_radix4) butterfly_inv_fixed<MOD, ROOT>(a);
else butterfly_inv_fixed_radix2<MOD, ROOT>(a);
const int invn = qpow_static<MOD>((int)a.size(), MOD - 2);
for (int i = 0; i < (int)a.size(); ++i) a[i] = StaticMod<MOD>::mul(a[i], invn);
}
}
poly mul_bruteforce(const poly& A, const poly& B) const {
if (A.empty() || B.empty()) return poly();
if (A.size() == 1) return scalar_mul(B, A[0]);
if (B.size() == 1) return scalar_mul(A, B[0]);
const poly *X = &A, *Y = &B;
if (X->size() > Y->size()) swap(X, Y);
const int n = (int)X->size(), m = (int)Y->size();
poly C(n + m - 1, 0);
if (n <= 12) {
for (int i = 0; i < n; ++i) {
int xi = (*X)[i];
if (!xi) continue;
int* c = &C[i];
for (int j = 0; j < m; ++j) {
int prod = mulmod(xi, (*Y)[j]);
int v = c[j] + prod;
c[j] = v >= mod_ ? v - mod_ : v;
}
}
} else {
for (int k = 0; k < n + m - 1; ++k) {
int l = max(0, k - m + 1);
int r = min(n - 1, k);
u128 acc = 0;
for (int i = l; i <= r; ++i) {
acc += (u64)(u32)(*X)[i] * (u32)(*Y)[k - i];
}
C[k] = (int)(acc % (u32)mod_);
}
}
trim(C);
return C;
}
poly mul_bruteforce_prefix(const poly& A, const poly& B, int need_limit) const {
if (A.empty() || B.empty() || need_limit <= 0) return poly();
int full = (int)A.size() + (int)B.size() - 1;
int need = min(need_limit, full);
if (A.size() == 1) { poly R = scalar_mul(B, A[0]); if ((int)R.size() > need) R.resize(need); trim(R); return R; }
if (B.size() == 1) { poly R = scalar_mul(A, B[0]); if ((int)R.size() > need) R.resize(need); trim(R); return R; }
const poly *X = &A, *Y = &B;
if (X->size() > Y->size()) swap(X, Y);
const int n = min((int)X->size(), need), m = min((int)Y->size(), need);
poly C(need, 0);
if (n <= 12) {
for (int i = 0; i < n; ++i) {
int xi = (*X)[i];
if (!xi) continue;
int* c = &C[i];
int up = min(m, need - i);
for (int j = 0; j < up; ++j) {
int prod = mulmod(xi, (*Y)[j]);
int v = c[j] + prod;
c[j] = v >= mod_ ? v - mod_ : v;
}
}
} else {
for (int k = 0; k < need; ++k) {
int l = max(0, k - m + 1);
int r = min(n - 1, k);
u128 acc = 0;
for (int i = l; i <= r; ++i) acc += (u64)(u32)(*X)[i] * (u32)(*Y)[k - i];
C[k] = (int)(acc % (u32)mod_);
}
}
trim(C);
return C;
}
poly mul_ntt(const poly& A, const poly& B) const {
if (A.empty() || B.empty()) return poly();
if (A.size() == 1) return scalar_mul(B, A[0]);
if (B.size() == 1) return scalar_mul(A, B[0]);
const int need = (int)A.size() + (int)B.size() - 1;
int lim = 1;
while (lim < need) lim <<= 1;
TempVec ta(lim), tb(lim);
poly& a = ta.get();
poly& b = tb.get();
if (!A.empty()) memcpy(&a[0], &A[0], sizeof(int) * A.size());
if (&A == &B) {
ntt(a, false);
for (int i = 0; i < lim; ++i) a[i] = mulmod(a[i], a[i]);
ntt(a, true);
a.resize(need);
trim(a);
return ta.release();
}
if (!B.empty()) memcpy(&b[0], &B[0], sizeof(int) * B.size());
ntt(a, false);
ntt(b, false);
for (int i = 0; i < lim; ++i) a[i] = mulmod(a[i], b[i]);
ntt(a, true);
a.resize(need);
trim(a);
return ta.release();
}
template<int MOD, int ROOT>
static poly mul_mod_fixed(const poly& A, const poly& B) {
if (A.empty() || B.empty()) return poly();
if (A.size() == 1) {
int k = StaticMod<MOD>::norm(A[0]);
if (k == 0) return poly();
poly R(B.size());
for (int i = 0; i < (int)B.size(); ++i) {
int x = B[i];
x = ((unsigned)x < (unsigned)MOD) ? x : StaticMod<MOD>::norm(x);
R[i] = StaticMod<MOD>::mul(x, k);
}
trim(R);
return R;
}
if (B.size() == 1) return mul_mod_fixed<MOD, ROOT>(B, A);
const int need = (int)A.size() + (int)B.size() - 1;
int lim = 1;
while (lim < need) lim <<= 1;
TempVec ta(lim), tb(lim);
poly& a = ta.get();
poly& b = tb.get();
for (int i = 0; i < (int)A.size(); ++i) {
int x = A[i];
a[i] = ((unsigned)x < (unsigned)MOD) ? x : StaticMod<MOD>::norm(x);
}
if (&A == &B) {
ntt_fixed<MOD, ROOT>(a, false);
for (int i = 0; i < lim; ++i) a[i] = StaticMod<MOD>::mul(a[i], a[i]);
ntt_fixed<MOD, ROOT>(a, true);
a.resize(need);
return ta.release();
}
for (int i = 0; i < (int)B.size(); ++i) {
int x = B[i];
b[i] = ((unsigned)x < (unsigned)MOD) ? x : StaticMod<MOD>::norm(x);
}
ntt_fixed<MOD, ROOT>(a, false);
ntt_fixed<MOD, ROOT>(b, false);
for (int i = 0; i < lim; ++i) a[i] = StaticMod<MOD>::mul(a[i], b[i]);
ntt_fixed<MOD, ROOT>(a, true);
a.resize(need);
return ta.release();
}
template<int MOD, int ROOT>
static poly mul_mod_same_fixed(const poly& A, const poly& B) {
if (A.empty() || B.empty()) return poly();
if (A.size() == 1) {
int k = StaticMod<MOD>::norm(A[0]);
if (k == 0) return poly();
poly R(B.size());
for (int i = 0; i < (int)B.size(); ++i) R[i] = StaticMod<MOD>::mul(StaticMod<MOD>::norm(B[i]), k);
trim(R);
return R;
}
if (B.size() == 1) return mul_mod_same_fixed<MOD, ROOT>(B, A);
const int need = (int)A.size() + (int)B.size() - 1;
int lim = 1;
while (lim < need) lim <<= 1;
TempVec ta(lim), tb(lim);
poly& a = ta.get();
poly& b = tb.get();
for (int i = 0; i < (int)A.size(); ++i) {
int x = A[i];
a[i] = ((unsigned)x < (unsigned)MOD) ? x : StaticMod<MOD>::norm(x);
}
if (&A == &B) {
ntt_fixed<MOD, ROOT>(a, false);
for (int i = 0; i < lim; ++i) a[i] = StaticMod<MOD>::mul(a[i], a[i]);
ntt_fixed<MOD, ROOT>(a, true);
a.resize(need);
trim(a);
return ta.release();
}
for (int i = 0; i < (int)B.size(); ++i) {
int x = B[i];
b[i] = ((unsigned)x < (unsigned)MOD) ? x : StaticMod<MOD>::norm(x);
}
ntt_fixed<MOD, ROOT>(a, false);
ntt_fixed<MOD, ROOT>(b, false);
for (int i = 0; i < lim; ++i) a[i] = StaticMod<MOD>::mul(a[i], b[i]);
ntt_fixed<MOD, ROOT>(a, true);
a.resize(need);
trim(a);
return ta.release();
}
template<int MOD, int ROOT>
static poly inv_same_fixed_impl(const poly& A, int n) {
if (n == 0) return poly();
poly B(1, qpow_static<MOD>(A[0], MOD - 2));
for (int len = 1; len < n; len <<= 1) {
const int m = min(len << 1, n);
const int lim = len << 1;
TempVec tf(lim), tg(lim);
poly& f = tf.get();
poly& g = tg.get();
const int upto = min((int)A.size(), m);
for (int i = 0; i < upto; ++i) {
int x = A[i];
f[i] = ((unsigned)x < (unsigned)MOD) ? x : StaticMod<MOD>::norm(x);
}
for (int i = 0; i < len; ++i) g[i] = B[i];
ntt_fixed<MOD, ROOT>(f, false);
ntt_fixed<MOD, ROOT>(g, false);
for (int i = 0; i < lim; ++i) f[i] = StaticMod<MOD>::mul(f[i], g[i]);
ntt_fixed<MOD, ROOT>(f, true);
const int keep = min(m, (int)f.size());
for (int i = 0; i < len && i < keep; ++i) f[i] = 0;
for (int i = keep; i < lim; ++i) f[i] = 0;
ntt_fixed<MOD, ROOT>(f, false);
for (int i = 0; i < lim; ++i) f[i] = StaticMod<MOD>::mul(f[i], g[i]);
ntt_fixed<MOD, ROOT>(f, true);
B.resize(m, 0);
for (int i = len; i < m; ++i) B[i] = f[i] ? MOD - f[i] : 0;
}
B.resize(n);
trim(B);
return B;
}
template<int MOD>
static vector<int>& static_inv_table() {
static vector<int> inv(2, 1);
inv[0] = 0;
return inv;
}
template<int MOD>
static void ensure_static_inv(int n) {
vector<int>& inv = static_inv_table<MOD>();
if ((int)inv.size() > n) return;
int old = (int)inv.size();
inv.resize(n + 1);
for (int i = old; i <= n; ++i) {
inv[i] = StaticMod<MOD>::mul(MOD - MOD / i, inv[MOD % i]);
}
}
template<int MOD>
static inline int get_fixed_coeff(const poly& A, int i) {
if (i >= (int)A.size()) return 0;
int x = A[i];
return ((unsigned)x < (unsigned)MOD) ? x : StaticMod<MOD>::norm(x);
}
template<int MOD>
static void copy_fixed_prefix(const poly& src, int cnt, poly& dst, int len) {
dst.assign(len, 0);
if (cnt > (int)src.size()) cnt = (int)src.size();
if (cnt > len) cnt = len;
for (int i = 0; i < cnt; ++i) {
int x = src[i];
dst[i] = ((unsigned)x < (unsigned)MOD) ? x : StaticMod<MOD>::norm(x);
}
}
static void copy_raw_prefix(const poly& src, int cnt, poly& dst, int len) {
dst.assign(len, 0);
if (cnt > (int)src.size()) cnt = (int)src.size();
if (cnt > len) cnt = len;
if (cnt > 0) memcpy(&dst[0], &src[0], sizeof(int) * cnt);
}
template<int MOD>
static void deriv_fixed_to(const poly& f, int n, poly& out, int len) {
out.assign(len, 0);
int upto = min(n, (int)f.size());
for (int i = 1; i < upto && i <= len; ++i) {
int x = f[i];
x = ((unsigned)x < (unsigned)MOD) ? x : StaticMod<MOD>::norm(x);
out[i - 1] = StaticMod<MOD>::mul(x, i % MOD);
}
}
template<int MOD>
static void integral_fixed_inplace(poly& a, int len) {
ensure_static_inv<MOD>(len);
vector<int>& inv = static_inv_table<MOD>();
if ((int)a.size() < len) a.resize(len, 0);
for (int i = len - 1; i >= 1; --i) a[i] = StaticMod<MOD>::mul(a[i - 1], inv[i]);
a[0] = 0;
}
template<int MOD, int ROOT>
static poly ln_fixed_fast_impl(const poly& A, int n) {
if (n == 0) return poly();
int len = 1;
while (len < n) len <<= 1;
poly f(len, 0), d(len, 0), g(len, 0), out(len, 0);
copy_fixed_prefix<MOD>(A, n, f, len);
deriv_fixed_to<MOD>(f, n, d, len);
g[0] = 1;
out[0] = d[0];
poly t1, t2, t3;
for (int k = 1, k2 = 2; k < len; k = k2, k2 <<= 1) {
copy_raw_prefix(g, k, t1, k2);
copy_raw_prefix(f, min(k2, n), t2, k2);
ntt_fixed<MOD, ROOT>(t1, false);
ntt_fixed<MOD, ROOT>(t2, false);
for (int i = 0; i < k2; ++i) t2[i] = StaticMod<MOD>::mul(t1[i], t2[i]);
ntt_fixed<MOD, ROOT>(t2, true);
for (int i = 0; i < k; ++i) t2[i] = 0;
ntt_fixed<MOD, ROOT>(t2, false);
copy_raw_prefix(g, k, t3, k2);
ntt_fixed<MOD, ROOT>(t3, false);
for (int i = 0; i < k2; ++i) t3[i] = StaticMod<MOD>::mul(t2[i], t3[i]);
ntt_fixed<MOD, ROOT>(t3, true);
for (int i = k; i < k2 && i < len; ++i) g[i] = t3[i] ? MOD - t3[i] : 0;
copy_raw_prefix(d, k2, t3, k2);
ntt_fixed<MOD, ROOT>(t3, false);
for (int i = 0; i < k2; ++i) t1[i] = StaticMod<MOD>::mul(t3[i], t1[i]);
copy_raw_prefix(out, k, t3, k2);
ntt_fixed<MOD, ROOT>(t3, false);
for (int i = 0; i < k2; ++i) t2[i] = StaticMod<MOD>::mul(t3[i], t2[i]);
t3.assign(k2, 0);
for (int i = 0; i < k2; ++i) t3[i] = StaticMod<MOD>::sub(t1[i], t2[i]);
ntt_fixed<MOD, ROOT>(t3, true);
for (int i = k; i < k2 && i < len; ++i) out[i] = t3[i];
}
integral_fixed_inplace<MOD>(out, len);
out.resize(n);
trim(out);
return out;
}
template<int MOD, int ROOT>
static poly exp_fixed_fast_impl(const poly& A, int n) {
if (n == 0) return poly();
int len = 1;
while (len < n) len <<= 1;
poly f(len, 0), out(len, 0), g(len, 0);
copy_fixed_prefix<MOD>(A, n, f, len);
out[0] = 1;
g[0] = 1;
poly t1, t2, t3, t4;
for (int k = 1, k2 = 2; k < len; k = k2, k2 <<= 1) {
copy_raw_prefix(out, k, t1, k2);
ntt_fixed<MOD, ROOT>(t1, false);
copy_raw_prefix(g, k, t2, k2);
ntt_fixed<MOD, ROOT>(t2, false);
t3.assign(k2, 0);
for (int i = 0; i < k2; ++i) {
int v = StaticMod<MOD>::mul(t1[i], StaticMod<MOD>::mul(t2[i], t2[i]));
t3[i] = v ? MOD - v : 0;
}
ntt_fixed<MOD, ROOT>(t3, true);
for (int i = 0; i < k; ++i) t3[i] = g[i];
ntt_fixed<MOD, ROOT>(t3, false);
t4.assign(k2, 0);
for (int i = 1; i < k; ++i) t4[i - 1] = StaticMod<MOD>::mul(out[i], i % MOD);
ntt_fixed<MOD, ROOT>(t4, false);
for (int i = 0; i < k2; ++i) t4[i] = StaticMod<MOD>::mul(t4[i], t3[i]);
ntt_fixed<MOD, ROOT>(t4, true);
integral_fixed_inplace<MOD>(t4, k2);
const int upto = min(n, k2);
for (int i = k; i < upto; ++i) t4[i] = StaticMod<MOD>::sub(t4[i], f[i]);
for (int i = 0; i < k; ++i) t4[i] = 0;
ntt_fixed<MOD, ROOT>(t4, false);
for (int i = 0; i < k2; ++i) {
int delta = t4[i];
int one_minus_delta = delta ? MOD + 1 - delta : 1;
if (one_minus_delta >= MOD) one_minus_delta -= MOD;
t1[i] = StaticMod<MOD>::mul(t1[i], one_minus_delta);
t2[i] = StaticMod<MOD>::add(t3[i], StaticMod<MOD>::mul(t2[i], delta));
}
ntt_fixed<MOD, ROOT>(t1, true);
for (int i = k; i < k2 && i < len; ++i) out[i] = t1[i];
ntt_fixed<MOD, ROOT>(t2, true);
for (int i = k; i < k2 && i < len; ++i) g[i] = t2[i];
}
out.resize(n);
trim(out);
return out;
}
template<int MOD, int ROOT>
static poly sqrt_fixed_fast_impl(const poly& A, int n, int s0) {
if (n == 0) return poly();
int len = 1;
while (len < n) len <<= 1;
poly f(len, 0), out(len, 0), h(len, 0);
copy_fixed_prefix<MOD>(A, n, f, len);
s0 = StaticMod<MOD>::norm(s0);
out[0] = s0;
h[0] = qpow_static<MOD>(s0, MOD - 2);
const int neg_inv2 = MOD - ((MOD + 1) >> 1);
poly t1, t2, t3;
for (int k = 1, k2 = 2; k < len; k = k2, k2 <<= 1) {
copy_raw_prefix(f, min(k2, n), t1, k2);
ntt_fixed<MOD, ROOT>(t1, false);
copy_raw_prefix(out, k, t2, k2);
ntt_fixed<MOD, ROOT>(t2, false);
copy_raw_prefix(h, k, t3, k2);
ntt_fixed<MOD, ROOT>(t3, false);
for (int i = 0; i < k2; ++i) {
int diff = StaticMod<MOD>::sub(StaticMod<MOD>::mul(t2[i], t2[i]), t1[i]);
int coeff = StaticMod<MOD>::mul(t3[i], neg_inv2);
t1[i] = StaticMod<MOD>::mul(diff, coeff);
}
ntt_fixed<MOD, ROOT>(t1, true);
for (int i = 0; i < k; ++i) t1[i] = out[i];
for (int i = k; i < k2 && i < len; ++i) out[i] = t1[i];
ntt_fixed<MOD, ROOT>(t1, false);
for (int i = 0; i < k2; ++i) t1[i] = StaticMod<MOD>::mul(t1[i], t3[i]);
ntt_fixed<MOD, ROOT>(t1, true);
for (int i = 0; i < k; ++i) t1[i] = 0;
ntt_fixed<MOD, ROOT>(t1, false);
for (int i = 0; i < k2; ++i) t1[i] = StaticMod<MOD>::mul(t1[i], t3[i]);
ntt_fixed<MOD, ROOT>(t1, true);
for (int i = k; i < k2 && i < len; ++i) h[i] = t1[i] ? MOD - t1[i] : 0;
}
out.resize(n);
trim(out);
return out;
}
poly mul_crt(const poly& A, const poly& B) const {
if (A.empty() || B.empty()) return poly();
if (A.size() == 1) return scalar_mul(B, A[0]);
if (B.size() == 1) return scalar_mul(A, B[0]);
poly x = mul_mod_fixed<MOD1, 3>(A, B);
poly y = mul_mod_fixed<MOD2, 3>(A, B);
poly z = mul_mod_fixed<MOD3, 3>(A, B);
static const int INV_M1_MOD_M2 = qpow_static<MOD2>(MOD1 % MOD2, MOD2 - 2);
static const int M1M2_MOD_M3 = (int)((long long)(MOD1 % MOD3) * (MOD2 % MOD3) % MOD3);
static const int INV_M1M2_MOD_M3 = qpow_static<MOD3>(M1M2_MOD_M3, MOD3 - 2);
const long long M12 = 1LL * MOD1 * MOD2;
const int m1_mod = MOD1 % mod_;
const int m12_mod = (int)(M12 % mod_);
Barrett b2(MOD2), b3(MOD3);
poly res(x.size());
for (int i = 0; i < (int)x.size(); ++i) {
int c1 = x[i];
int t2 = y[i] - c1;
if (t2 < 0) t2 += MOD2;
int c2 = b2.mul(t2, INV_M1_MOD_M2);
int t13 = (int)(((long long)(MOD1 % MOD3) * c2 + c1) % MOD3);
int t3 = z[i] - t13;
if (t3 < 0) t3 += MOD3;
int c3 = b3.mul(t3, INV_M1M2_MOD_M3);
u64 val = (u64)(c1 % mod_) + (u64)m1_mod * (u32)c2 + (u64)m12_mod * (u32)c3;
res[i] = bt_.reduce(val);
}
trim(res);
return res;
}
poly multiply_impl(const poly& A, const poly& B, MulBackend backend) const {
if (A.empty() || B.empty()) return poly();
if (A.size() == 1) return scalar_mul(B, A[0]);
if (B.size() == 1) return scalar_mul(A, B[0]);
const long long work = 1LL * A.size() * B.size();
const int mn = (int)min(A.size(), B.size());
int need = (int)A.size() + (int)B.size() - 1;
int lim = 1;
while (lim < need) lim <<= 1;
const bool fixed_common_mod = (mod_ == MOD1 || mod_ == MOD2 || mod_ == MOD3);
bool use_brute = false;
if (backend == MulBackend::CRT || (!fixed_common_mod && backend != MulBackend::NTT)) {
use_brute = (work <= 200000LL) || (mn <= 192 && work <= 2000000LL);
} else if (fixed_common_mod) {
use_brute = (work <= 12000LL)
|| (lim == 512 && work <= 30000LL)
|| (mn <= 112 && work <= 1000000LL);
} else {
use_brute = (work <= 20000LL)
|| (lim == 512 && work <= 45000LL)
|| (mn <= 128 && work <= 1200000LL);
}
if (use_brute) return mul_bruteforce(A, B);
if (backend == MulBackend::CRT) return mul_crt(A, B);
if (mod_ == MOD1) return mul_mod_same_fixed<MOD1, 3>(A, B);
if (mod_ == MOD2) return mul_mod_same_fixed<MOD2, 3>(A, B);
if (mod_ == MOD3) return mul_mod_same_fixed<MOD3, 3>(A, B);
if (backend == MulBackend::NTT) {
assert(ntt_legal(lim));
return mul_ntt(A, B);
}
if (ntt_legal(lim)) return mul_ntt(A, B);
return mul_crt(A, B);
}
poly multiply_prefix_impl(const poly& A, const poly& B, int need_limit, MulBackend backend) const {
if (A.empty() || B.empty() || need_limit <= 0) return poly();
int full = (int)A.size() + (int)B.size() - 1;
if (need_limit >= full) return multiply_impl(A, B, backend);
int need = need_limit;
int la = min((int)A.size(), need), lb = min((int)B.size(), need);
long long ops = 0;
if (la <= lb) {
for (int i = 0; i < la; ++i) ops += min(lb, need - i);
} else {
for (int j = 0; j < lb; ++j) ops += min(la, need - j);
}
const int mn = min(la, lb);
const bool fixed_common_mod = (mod_ == MOD1 || mod_ == MOD2 || mod_ == MOD3);
bool use_brute = false;
if (backend == MulBackend::CRT || (!fixed_common_mod && backend != MulBackend::NTT)) {
use_brute = (ops <= 260000LL) || (mn <= 224 && ops <= 2600000LL);
} else if (fixed_common_mod) {
use_brute = (ops <= 18000LL) || (mn <= 128 && ops <= 1200000LL);
} else {
use_brute = (ops <= 26000LL) || (mn <= 144 && ops <= 1400000LL);
}
if (use_brute) return mul_bruteforce_prefix(A, B, need);
if (la == (int)A.size() && lb == (int)B.size()) {
poly R = multiply_impl(A, B, backend);
if ((int)R.size() > need) R.resize(need);
trim(R);
return R;
}
poly AA, BB;
const poly *PA = &A, *PB = &B;
if (la != (int)A.size()) { AA.assign(A.begin(), A.begin() + la); PA = &AA; }
if (lb != (int)B.size()) { BB.assign(B.begin(), B.begin() + lb); PB = &BB; }
poly R = multiply_impl(*PA, *PB, backend);
if ((int)R.size() > need) R.resize(need);
trim(R);
return R;
}
void ensure_inv(int n) {
if ((int)invs_.size() > n) return;
int old = (int)invs_.size();
invs_.resize(n + 1);
for (int i = old; i <= n; ++i) {
invs_[i] = mulmod(mod_ - mod_ / i, invs_[mod_ % i]);
}
}
static int cipolla_w;
struct ComplexMod { int x, y; };
ComplexMod cmul(ComplexMod a, ComplexMod b) const {
int real = addmod(mulmod(a.x, b.x), mulmod(mulmod(a.y, b.y), cipolla_w));
int imag = addmod(mulmod(a.x, b.y), mulmod(a.y, b.x));
ComplexMod r = { real, imag };
return r;
}
ComplexMod cpow(ComplexMod a, long long e) const {
ComplexMod r = {1, 0};
while (e) {
if (e & 1) r = cmul(r, a);
a = cmul(a, a);
e >>= 1;
}
return r;
}
int modsqrt(int n) const {
if (n == 0) return 0;
if (mod_ == 2) return n;
if (qpow(n, (mod_ - 1) / 2) != 1) return -1;
static mt19937 rng((unsigned)chrono::steady_clock::now().time_since_epoch().count());
while (true) {
int a = (int)(rng() % mod_);
cipolla_w = norm((long long)a * a - n);
if (qpow(cipolla_w, (mod_ - 1) / 2) == mod_ - 1) {
ComplexMod x = {a, 1};
int r = cpow(x, (mod_ + 1) / 2).x;
if (r > mod_ - r) r = mod_ - r;
return r;
}
}
}
public:
explicit Polynomial(int mod = MOD1)
: mod_(mod), bt_((u32)mod), invs_(2),
info_ready_(false), prime_cache_(false), max_ntt_len_cache_(0), primitive_root_cache_(0),
rev_cache_n_(0) {
invs_[0] = 0;
invs_[1] = 1;
}
int mod() const { return mod_; }
poly normalize(poly a) const {
for (int i = 0; i < (int)a.size(); ++i) a[i] = norm(a[i]);
trim(a);
return a;
}
poly add(poly A, const poly& B) const {
if (B.size() > A.size()) A.resize(B.size(), 0);
for (int i = 0; i < (int)B.size(); ++i) A[i] = addmod(A[i], B[i]);
trim(A);
return A;
}
poly sub(poly A, const poly& B) const {
if (B.size() > A.size()) A.resize(B.size(), 0);
for (int i = 0; i < (int)B.size(); ++i) A[i] = submod(A[i], B[i]);
trim(A);
return A;
}
poly prefix(const poly& A, int n) const {
if (n <= 0) return poly();
if ((int)A.size() <= n) return A;
return poly(A.begin(), A.begin() + n);
}
poly scalar_mul(poly A, int k) const {
k = norm(k);
if (k == 0) return poly();
if (k == 1) { trim(A); return A; }
for (int i = 0; i < (int)A.size(); ++i) A[i] = mulmod(A[i], k);
trim(A);
return A;
}
poly mul(const poly& A, const poly& B, MulBackend backend = MulBackend::AUTO) const {
return multiply_impl(A, B, backend);
}
poly mul_prefix(const poly& A, const poly& B, int n, MulBackend backend = MulBackend::AUTO) const {
return multiply_prefix_impl(A, B, n, backend);
}
poly deriv(const poly& A) const {
if ((int)A.size() <= 1) return poly();
poly B((int)A.size() - 1);
for (int i = 1; i < (int)A.size(); ++i) B[i - 1] = mulmod(A[i], i % mod_);
trim(B);
return B;
}
poly integral(const poly& A) {
ensure_inv((int)A.size());
poly B((int)A.size() + 1, 0);
for (int i = 0; i < (int)A.size(); ++i) B[i + 1] = mulmod(A[i], invs_[i + 1]);
return B;
}
poly inv(const poly& A, int n, MulBackend backend = MulBackend::AUTO) const {
assert(n >= 0);
if (n == 0) return poly();
assert(!A.empty() && A[0] != 0);
if (n >= 256 && backend != MulBackend::CRT) {
if (mod_ == MOD1) return inv_same_fixed_impl<MOD1, 3>(A, n);
if (mod_ == MOD2) return inv_same_fixed_impl<MOD2, 3>(A, n);
if (mod_ == MOD3) return inv_same_fixed_impl<MOD3, 3>(A, n);
}
poly B(1, qpow(A[0], mod_ - 2));
for (int len = 1; len < n; len <<= 1) {
int m = min(len << 1, n);
poly F = prefix(A, m);
poly T = multiply_prefix_impl(F, B, m, backend);
T.resize(m, 0);
for (int i = 0; i < m; ++i) if (T[i]) T[i] = mod_ - T[i];
T[0] = addmod(T[0], 2);
B = multiply_prefix_impl(B, T, m, backend);
B.resize(m);
}
B.resize(n);
trim(B);
return B;
}
poly ln(const poly& A, int n, MulBackend backend = MulBackend::AUTO) {
assert(n >= 0);
if (n == 0) return poly();
assert(!A.empty() && A[0] == 1);
#ifdef POLY_ENABLE_FUSED_LN
if (backend != MulBackend::CRT) {
if (mod_ == MOD1) return ln_fixed_fast_impl<MOD1, 3>(A, n);
if (mod_ == MOD2) return ln_fixed_fast_impl<MOD2, 3>(A, n);
if (mod_ == MOD3) return ln_fixed_fast_impl<MOD3, 3>(A, n);
}
#endif
poly D = deriv(A);
poly I = inv(A, n, backend);
poly R = multiply_prefix_impl(D, I, max(0, n - 1), backend);
R.resize(max(0, n - 1));
R = integral(R);
R.resize(n);
trim(R);
return R;
}
poly exp(const poly& A, int n, MulBackend backend = MulBackend::AUTO) {
assert(n >= 0);
if (n == 0) return poly();
assert(A.empty() || A[0] == 0);
if (backend != MulBackend::CRT) {
if (mod_ == MOD1) return exp_fixed_fast_impl<MOD1, 3>(A, n);
if (mod_ == MOD2) return exp_fixed_fast_impl<MOD2, 3>(A, n);
if (mod_ == MOD3) return exp_fixed_fast_impl<MOD3, 3>(A, n);
}
poly B(1, 1);
for (int len = 1; len < n; len <<= 1) {
int m = min(len << 1, n);
poly LnB = ln(B, m, backend);
poly C(m, 0);
const int asz = min((int)A.size(), m);
for (int i = 0; i < asz; ++i) C[i] = A[i];
for (int i = 0; i < (int)LnB.size(); ++i) C[i] = submod(C[i], LnB[i]);
C[0] = addmod(C[0], 1);
B = multiply_prefix_impl(B, C, m, backend);
B.resize(m);
}
B.resize(n);
trim(B);
return B;
}
poly sqrt(const poly& A, int n, MulBackend backend = MulBackend::AUTO) {
assert(n >= 0);
if (n == 0) return poly();
if (A.empty()) return poly(n, 0);
if (A[0] == 0) {
int k = 0;
while (k < (int)A.size() && A[k] == 0) ++k;
if (k == (int)A.size()) return poly(n, 0);
assert((k & 1) == 0);
poly B(A.begin() + k, A.end());
poly C = sqrt(B, n - k / 2, backend);
poly res(k / 2, 0);
res.insert(res.end(), C.begin(), C.end());
res.resize(n);
trim(res);
return res;
}
int s0 = modsqrt(A[0]);
assert(s0 != -1);
if (backend != MulBackend::CRT) {
if (mod_ == MOD1) return sqrt_fixed_fast_impl<MOD1, 3>(A, n, s0);
if (mod_ == MOD2) return sqrt_fixed_fast_impl<MOD2, 3>(A, n, s0);
if (mod_ == MOD3) return sqrt_fixed_fast_impl<MOD3, 3>(A, n, s0);
}
poly B(1, s0);
const int inv2 = (mod_ + 1) >> 1;
for (int len = 1; len < n; len <<= 1) {
int m = min(len << 1, n);
poly F = prefix(A, m);
poly IB = inv(B, m, backend);
poly T = multiply_prefix_impl(F, IB, m, backend);
T.resize(m, 0);
B.resize(m, 0);
for (int i = 0; i < m; ++i) B[i] = mulmod(addmod(B[i], T[i]), inv2);
}
B.resize(n);
trim(B);
return B;
}
pair<poly, poly> divmod(const poly& A, const poly& B, MulBackend backend = MulBackend::AUTO) const {
assert(!B.empty());
if (A.size() < B.size()) return make_pair(poly(), A);
int n = (int)A.size(), m = (int)B.size(), k = n - m + 1;
poly RA = A, RB = B;
reverse(RA.begin(), RA.end());
reverse(RB.begin(), RB.end());
poly IQ = inv(prefix(RB, k), k, backend);
poly Q = multiply_prefix_impl(prefix(RA, k), IQ, k, backend);
Q.resize(k);
reverse(Q.begin(), Q.end());
trim(Q);
poly BQ = multiply_prefix_impl(B, Q, m - 1, backend);
poly R = prefix(A, m - 1);
R = sub(R, BQ);
if ((int)R.size() >= m) R.resize(m - 1);
trim(R);
return make_pair(Q, R);
}
};
int Polynomial::cipolla_w = 0;//Author:kevinZ99
#include <bits/stdc++.h>
#define up(a,b,c) for(int (a)=(b);(a)<=(c);(a)=-~(a))
#define dn(a,b,c) for(int (a)=(b);(a)>=(c);(a)=~-(a))
#define fst first
#define sed second
#define pref static inline __attribute__((hot,always_inline,flatten))
#define noe noexcept
#define gc() p1==p2&&(p2=(p1=buf)+fread(buf,1,1<<12,stdin),p1==p2)?EOF:*p1++
using namespace std; using hint = __int128;using pii = pair< int , int > ;
using us = unsigned short ;using ldb  = long double ;using ll = long long;
using ull= unsigned long long;using ui=unsigned int;using pll=pair<ll,ll>;
using pil= pair<int,ll> ;using vpil   = vector<pil>;using vl = vector<ll>;
using pli= pair<ll,int>;using vpli    = vector<pli>;using vi =vector<int>;
using vpi= vector< pii > ;using vpl   = vector<pll> ; using db =  double ;
namespace mystl{
	char buf[1<<20], *p1=buf, *p2=buf, sr[1<<23], z[23], nc;int C=-1 ,Z=0;
	template <typename T>pref void read ( T & x )noe { bool flag = false ;
		while( nc = gc() ,(nc<48||nc>57) && nc!=-1)flag|=(nc==45);x=nc-48;
		while(nc=gc(),47<nc&&nc<58)x=(x<<3)+(x<<1)+(nc^48); if(flag)x=-x;}
	template <  typename T , typename ... Args_Arrays_Typename_KevinZ99  >
	void read(T&x,Args_Arrays_Typename_KevinZ99&...a){read(x);read(a...);}
	pref void ot(  )noe {  fwrite( sr , 1 , C + 1, stdout ) ;  C = - 1 ; }
	pref void flush( )noe{ if ( C > 1<<22 ) ot() ; } template <typename T>
	pref void write(T x,char t)noe{ int y = 0 ; if ( x < 0 ) y = 1, x = -x;
		while( z [ ++ Z ] = x % 10 + 48 , x /= 10) ; if( y ) z[ ++Z ]='-';
		while( sr[ ++ C ] = z[ Z ] , -- Z ) ; sr [ ++C ] = t ; flush() ; }
	pref void write(char x)noe{sr[C=-~C]=x;}pref void write(string s){for(
	char t:s)write(t);}pref ll qpow(ll a , ll b,ll p)noe{if(a==0)return 0;
	ll c=1ll; while(b) { if(b & 1) c=a*c%p; a=a*a%p; b>>=1; } return c ; }
	pref ll lcm ( ll x , ll y )noe{return x / std :: __gcd( x , y ) * y ;}
};
using namespace mystl;
namespace my{
	int P=static_cast<int>(998244353);
	pref void madd(int & x , int y)noe { x = ( x + y >= P )?(x+y-P):(x+y);}
	pref int fmadd(int x , int y)noe { return ( x + y >=P )?(x+y-P):(x+y);}
	pref void msub(int & x , int y)noe { x = ( x < y ) ? (x-y+P) : (x-y); }
	pref int fmsub(int x , int y)noe { return ( x < y ) ? (x-y+P) : (x-y);}
	pref void mmul ( int & x , int y )noe{ x = (int)( 1ll * x * y % P ) ; }
	pref int fmmul ( int x,int y )noe { return (int)( 1ll * x * y % P ) ; }
	template<typename T>pref T Min(T x,T y)noe{return (x<y)?(x):(y);}
	template<typename T>pref T Max(T x,T y)noe{return (x>y)?(x):(y);}
	template<typename T>pref T Abs(T x) noe  {return (x<0)?(-x):(x);}
	constexpr int N=static_cast<int>(0),inf=static_cast<int>(1e9);
	int n,m;
	Polynomial poly(P);
	void SOLVE()noe{
		read(n,m);
		vi a(n+1),b(m+1);
		for(int&v:a)read(v);
		for(int&v:b)read(v);
		auto q=poly.divmod(a,b);
		for(int v:q.fst)write(v,' ');write('\n');
		for(int v:q.sed)write(v,' ');
		ot();
	}
}
int main(){
	my::SOLVE();return 0;
}
/*

*/
