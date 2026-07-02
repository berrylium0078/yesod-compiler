// https://www.luogu.com.cn/article/fwqkoyjx
#include <cstdio>
#include <algorithm>
const int N = 1e6 + 10, mod = 998244353; typedef long long ll;
inline int ksm(int a, int b)
{
    int ret = 1;
    while (b)
    {
        if (b & 1) ret = (ll)ret * a % mod;
        a = (ll)a * a % mod; b >>= 1;
    }
    return ret;
}
int F[N], G[N], A[N], B[N], rev[N], lim, m, n;
inline void init(int n)
{
    lim = 1; m = 0; while (lim <= n) lim <<= 1, ++m;
    for (int i = 0; i < lim; ++i) rev[i] = (rev[i >> 1] >> 1) | ((i & 1) << (m - 1));
}
inline void NTT(int* f, int len, int on)
{
    for (int i = 0; i < len; ++i) if (i < rev[i]) std::swap(f[i], f[rev[i]]);
    for (int h = 2; h <= len; h <<= 1)
    {
        int gn = ksm(3, (ll)(mod - 1) / h * on % (mod - 1));
        for (int j = 0; j < len; j += h)
            for (int k = j, g = 1; k < j + h / 2; ++k, g = (ll)g * gn % mod)
            {
                int u = f[k], t = (ll)g * f[k + h / 2] % mod;
                f[k] = (u + t) % mod; f[k + h / 2] = ((u - t) % mod + mod) % mod;
            }
    }
    if (on == mod - 2) for (int i = 0, inv = ksm(len, on); i < len; ++i) f[i] = (ll)f[i] * inv % mod;
}
void cdq(int l, int r)
{
    if (l + 1 == r) return F[l] = l ? (ll)F[l] * ksm(l, mod - 2) % mod : 1, void();
    int mid = (l + r) >> 1; cdq(l, mid); init(r - l);
    for (int i = 0; i < mid - l; ++i) A[i] = F[i + l];
    for (int i = 0; i < r - l - 1; ++i) B[i] = G[i];
    for (int i = mid - l; i < lim; ++i) A[i] = 0;
    for (int i = r - l; i < lim; ++i) B[i] = 0;
    NTT(A, lim, 1); NTT(B, lim, 1);
    for (int i = 0; i < lim; ++i) A[i] = (ll)A[i] * B[i] % mod;
    NTT(A, lim, mod - 2);
    for (int i = mid - l - 1; i < r - l - 1; ++i) (F[i + l + 1] += A[i]) %= mod;
    cdq(mid, r);
}
int main()
{
    scanf("%d", &n);
    for (int i = 0; i < n; ++i) scanf("%d", &G[i]);
    for (int i = 1; i < n; ++i) G[i - 1] = (ll)i * G[i] % mod;
    G[n - 1] = 0;
    cdq(0, n); for (int i = 0; i < n; ++i) printf("%d ", F[i]); 
    puts(""); return 0;
}
