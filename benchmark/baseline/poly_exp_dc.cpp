#include <bits/stdc++.h>
using namespace std;
using ll = long long;
using uint = unsigned;
using ull = unsigned long long;

const int N = 1000100, M = 998244353;
int n, m;
uint f[N], g[N], x[N], y[N];
uint power(uint x, int y) {
	uint ans = 1;
	for (; y; y >>= 1, x = (ull)x * x % M) if (y & 1)
		ans = (ull)ans * x % M;
	return ans;
}
inline uint chkmod(int x) { return x + ((x >> 31) & M); }
void ntt(uint *a, int n, int flag) {
	for (int i = 0, j = 0; i < n; i++) {
		if (i < j) swap(a[i], a[j]);
		for (int l = n >> 1; (j ^= l) < l; l >>= 1);
	}
	for (uint m = 1, k = 2; m < n; m <<= 1, k <<= 1) {
		const uint omg = power(3, (M - 1) + (M - 1) / k * flag);
		for (uint j = 0; j < n; j += k)
			for (uint i = 0, o = 1; i < m; i++, o = (ull)o * omg % M) {
				uint x = a[i + j], y = (ull)a[i + j + m] * o % M;
				a[i + j] = chkmod(x + y - M);
				a[i + j + m] = chkmod(x - y);
			}
	}
	if (flag == -1) {
		const int inv = power(n, M - 2);
		for (int i = 0; i < n; i++) a[i] = (ull)a[i] * inv % M;
	}

}
inline int lowbit(int x) { return x & (-x); }
void polyexp(uint *g, uint *f, int n) {
	for (int i = 1; i < n; i++) g[i] = 0;
	for (int i = 1; i < n; i++) f[i] = (ull)f[i] * i % M;
	for (int i = 0; i < n; i++) {
		g[i] = !i ? 1 : (ull)g[i] * power(i, M - 2) % M;
		int len = lowbit(i + 1), l = i - len + 1;
		fill(x, x + (len << 2), 0), fill(y, y + (len << 2), 0);
		copy(g + l, g + l + len, x), copy(f, f + (len << 1), y);
		ntt(x, len << 2, 1), ntt(y, len << 2, 1);
		for (int j = 0; j < (len << 2); j++) x[j] = (ull)x[j] * y[j] % M;
		ntt(x, len << 2, -1);
		for (int k = len; k < (len << 1); k++)
			g[k + l] = chkmod(g[k + l] + x[k] - M);
	}
}
int main() {
	ios::sync_with_stdio(0), cin.tie(0), cout.tie(0);
	cin >> n;
	for (int i = 0; i < n; i++) cin >> f[i];
	polyexp(g, f, n);
	for (int i = 0; i < n; i++) cout << g[i] << " ";
}