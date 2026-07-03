#include <bits/stdc++.h>
using namespace std;
using ll = long long;
using uint = unsigned;
using ull = unsigned long long;

const int N = 1000100, M = 998244353;
const int PW_N = 1 << 21;
int n, m;
uint f[N], g[N], x[N], y[N];
uint power(uint x, int y)
{
    uint ans = 1;
    for (; y; y >>= 1, x = (ull)x * x % M)
        if (y & 1)
            ans = (ull)ans * x % M;
    return ans;
}

ull pw[PW_N];

inline void inc(uint& a, uint b)
{
    a += b;
    (a >= M) && (a -= M);
}

void DIF(uint* D, int lim)
{
    for (int mid = lim >> 1; mid >= 2; mid >>= 1) {
        for (int w = 0, j = 0; j < lim; j += mid << 1, w++) {
            for (int k = 0; k < mid; k += 2) {
                uint a = pw[w] * D[j + mid + k + 0] % M;
                uint b = pw[w] * D[j + mid + k + 1] % M;
                D[j + mid + k + 0] = D[j + k + 0];
                D[j + mid + k + 1] = D[j + k + 1];
                inc(D[j + k + 0], a), inc(D[j + mid + k + 0], M - a);
                inc(D[j + k + 1], b), inc(D[j + mid + k + 1], M - b);
            }
        }
    }
    for (int w = 0, j = 0; j < lim; j += 2, w++) {
        uint y = pw[w] * D[j + 1] % M;
        D[j + 1] = D[j];
        inc(D[j], y), inc(D[j + 1], M - y);
    }
}

void DIT(uint* D, int lim)
{
    for (int w = 0, j = 0; j < lim; j += 2, w++) {
        uint y = pw[w] * (M + D[j] - D[j + 1]) % M;
        inc(D[j], D[j + 1]), D[j + 1] = y;
    }
    for (int mid = 2; mid < lim; mid <<= 1) {
        for (int w = 0, j = 0; j < lim; j += mid << 1, w++) {
            for (int k = 0; k < mid; k += 2) {
                uint a = pw[w] * (M + D[j + k + 0] - D[j + mid + k + 0]) % M;
                uint b = pw[w] * (M + D[j + k + 1] - D[j + mid + k + 1]) % M;
                inc(D[j + k + 0], D[j + mid + k + 0]), D[j + mid + k + 0] = a;
                inc(D[j + k + 1], D[j + mid + k + 1]), D[j + mid + k + 1] = b;
            }
        }
    }
    reverse(D + 1, D + lim);
    uint local_inv = power(lim, M - 2);
    for (int i = 0; i < lim; i++)
        D[i] = (ull)D[i] * local_inv % M;
}

void init_pw(int max_n)
{
    pw[0] = 1;
    for (int lim = 1; lim < max_n; lim <<= 1) {
        uint w = power(3, (M - 1) / (lim << 2));
        for (int i = 0; i < lim; i++) {
            pw[lim + i] = pw[i] * w % M;
        }
    }
}

inline uint chkmod(int x) { return x + ((x >> 31) & M); }
inline int lowbit(int x) { return x & (-x); }
void polyexp(uint* g, uint* f, int n)
{
    for (int i = 1; i < n; i++)
        g[i] = 0;
    for (int i = 1; i < n; i++)
        f[i] = (ull)f[i] * i % M;
    for (int i = 0; i < n; i++) {
        g[i] = !i ? 1 : (ull)g[i] * power(i, M - 2) % M;
        int len = lowbit(i + 1), l = i - len + 1;
        fill(x, x + (len << 1), 0), fill(y, y + (len << 1), 0);
        copy(g + l, g + l + len, x), copy(f, f + (len << 1), y);
        DIF(x, len << 1), DIF(y, len << 1);
        for (int j = 0; j < (len << 1); j++)
            x[j] = (ull)x[j] * y[j] % M;
        DIT(x, len << 1);
        for (int k = len; k < (len << 1); k++)
            g[k + l] = chkmod(g[k + l] + x[k] - M);
    }
}
int main()
{
    ios::sync_with_stdio(0), cin.tie(0), cout.tie(0);
    cin >> n;
    int max_ntt = 2;
    while (max_ntt <= n)
        max_ntt <<= 1;
    init_pw(max_ntt);
    for (int i = 0; i < n; i++)
        cin >> f[i];
    polyexp(g, f, n);
    for (int i = 0; i < n; i++)
        cout << g[i] << " ";
}