#include <algorithm>
#include <cstdio>
#include <iostream>
using namespace std;

#define debug(...) fprintf(stderr, __VA_ARGS__)

#define ll long long
const int N = 1 << 18 | 4399;
const int M = 998244353;
const int ROOT = 3;
const int PW_N = 1 << 20;

unsigned long long pw[PW_N];

int power(int x, int y, int m = M)
{
    int ans = 1;
    for (; y; y >>= 1, x = (ll)x * x % m)
        if (y & 1)
            ans = (ll)ans * x % m;
    return ans;
}

inline void inc(int& a, int b)
{
    a += b;
    (a >= M) && (a -= M);
}

void DIF(int* D, int lim)
{
    for (int mid = lim >> 1; mid >= 2; mid >>= 1) {
        for (int w = 0, j = 0; j < lim; j += mid << 1, w++) {
            for (int k = 0, a, b; k < mid; k += 2) {
                a = pw[w] * D[j + mid + k + 0] % M,
                D[j + mid + k + 0] = D[j + k + 0];
                inc(D[j + k + 0], a), inc(D[j + mid + k + 0], M - a);
                b = pw[w] * D[j + mid + k + 1] % M,
                D[j + mid + k + 1] = D[j + k + 1];
                inc(D[j + k + 1], b), inc(D[j + mid + k + 1], M - b);
            }
        }
    }
    for (int w = 0, j = 0, y; j < lim; j += 2, w++) {
        y = pw[w] * D[j + 1] % M, D[j + 1] = D[j], inc(D[j], y),
        inc(D[j + 1], M - y);
    }
}

void DIT(int* D, int lim)
{
    for (int w = 0, j = 0, y; j < lim; j += 2, w++) {
        y = pw[w] * (M + D[j] - D[j + 1]) % M, inc(D[j], D[j + 1]),
        D[j + 1] = y;
    }
    for (int mid = 2; mid < lim; mid <<= 1) {
        for (int w = 0, j = 0; j < lim; j += mid << 1, w++) {
            for (int k = 0, a, b; k < mid; k += 2) {
                a = pw[w] * (M + D[j + k + 0] - D[j + mid + k + 0]) % M;
                inc(D[j + k + 0], D[j + mid + k + 0]), D[j + mid + k + 0] = a;
                b = pw[w] * (M + D[j + k + 1] - D[j + mid + k + 1]) % M;
                inc(D[j + k + 1], D[j + mid + k + 1]), D[j + mid + k + 1] = b;
            }
        }
    }
    reverse(D + 1, D + lim);
    int local_inv = power(lim, M - 2);
    for (int i = 0; i < lim; i++)
        D[i] = 1ull * D[i] * local_inv % M;
}

void init_pw(int max_n)
{
    pw[0] = 1;
    for (int lim = 1; lim < max_n; lim <<= 1) {
        int w = power(ROOT, (M - 1) / (lim << 2));
        for (int i = 0; i < lim; i++) {
            pw[lim + i] = pw[i] * w % M;
        }
    }
}

inline int mod(int x, int M) { return x + (M & (x >> 31)); }

int limit(int n)
{
    int ans = 0;
    for (int i = 1; i < n; i <<= 1, ans++)
        ;
    return ans;
}

struct poly {
    int *a, n, lim, sup;
    poly()
        : a()
        , n()
        , lim()
        , sup()
    {
    }
    void resize(int m)
    {
        int tmp = 1 << (lim = limit(n = m));
        a = (int*)realloc(a, tmp << 2);
        if (tmp > sup)
            fill(a + sup, a + tmp, 0);
        sup = tmp;
    }
    poly(int m)
    {
        sup = 1 << (lim = limit(n = m));
        a = (int*)malloc(sup << 2);
        fill(a, a + sup, 0);
    }
    poly(int* l, int* r)
    {
        sup = 1 << (lim = limit(n = r - l));
        a = (int*)malloc(sup << 2);
        copy(l, r, a), fill(a + n, a + sup, 0);
    }
    operator int*() const { return a; }
};

inline void inv(poly& f, poly& g)
{
    int n = g.sup;
    poly tf(1);
    tf[0] = power(g[0], M - 2);
    for (int m = 1, k = 2, l = 4, lim = 2; m < n;
        m <<= 1, k <<= 1, l <<= 1, lim++) {
        poly tg(g + 0, g + k);
        tg.resize(l), tf.resize(l);
        DIF(tg, l), DIF(tf, l);
        for (int i = 0; i < l; i++)
            tf[i] = (ll)tf[i] * (M + 2 - (ll)tf[i] * tg[i] % M) % M;
        DIT(tf, l);
        tf.resize(k);
    }
    f.resize(g.n);
    copy(tf + 0, tf + g.n, f + 0);
}
inline void deriv(poly& f, poly& g)
{
    int n = g.n;
    f.resize(n - 1);
    for (int i = 1; i < n; i++)
        f[i - 1] = (ll)i * g[i] % M;
}
inline void integ(poly& f, poly& g, int C = 0)
{
    int n = g.n;
    f.resize(n + 1);
    f[0] = C;
    for (int i = n; i; i--)
        f[i] = (ll)power(i, M - 2) * g[i - 1] % M;
}
inline void ln(poly& f, poly& g)
{
    int sup = g.sup << 1, lim = g.lim + 1;
    poly t1, t2;
    deriv(t1, g), inv(t2, g);
    t1.resize(sup), t2.resize(sup);
    DIF(t1, sup), DIF(t2, sup);
    for (int i = 0; i < sup; i++)
        t1[i] = (ll)t1[i] * t2[i] % M;
    DIT(t1, sup);
    t1.resize(g.n - 1);
    integ(f, t1);
}
inline void exp(poly& f, poly& g)
{
    int n = g.sup;
    poly tg, tf(1);
    tf[0] = 1;
    for (int m = 1, k = 2, l = 4, lim = 2; m < n;
        m <<= 1, k <<= 1, l <<= 1, lim++) {
        tf.resize(k), ln(tg, tf);
        for (int i = 0; i < k; i++)
            tg[i] = mod(g[i] - tg[i], M);
        tg[0] = mod(tg[0] - M + 1, M);
        tf.resize(l), tg.resize(l);
        DIF(tf, l), DIF(tg, l);
        for (int i = 0; i < l; i++)
            tf[i] = (ll)tf[i] * tg[i] % M;
        DIT(tf, l);
        tf.resize(k);
    }
    copy(tf + 0, tf + n, f + 0);
}

int main()
{
    init_pw(PW_N);
    int n;
    scanf("%d", &n);
    poly f(n);
    for (int i = 0; i < n; i++)
        scanf("%d", f + i);
    exp(f, f);
    for (int i = 0; i < n; i++)
        printf("%d ", f[i]);
}