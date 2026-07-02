#include <cstdio>
#include <iostream>
using namespace std;

#define debug(...) fprintf(stderr, __VA_ARGS__)

#define ll long long
const int N = 1 << 18 | 4399;
const int M = 998244353;
const int ROOT = 3;

int power(int x, int y, int m = M) {
	int ans = 1;
	for (; y; y >>= 1, x = (ll)x * x % m)
		if (y & 1)
			ans = (ll)ans * x % m;
	return ans;
}
int limit(int n) {
	int ans = 0;
	for (int i = 1; i < n; i <<= 1, ans++);
	return ans;
}
inline int mod(int x, int M) {
	return x + (M & (x >> 31));
}
inline void ntt(int *a, int lim, int flag) {
	const int n = 1 << lim;
	for (int i = 0, j = 0; i < n; i++) {
		if (i < j) swap(a[i], a[j]);
		for (int l = n >> 1; (j ^= l) < l; l >>= 1);
	}
	for (int m = 1, k = 2; m < n; m <<= 1, k <<= 1) {
		int omg = power(ROOT, (M - 1) + (M - 1) / k * flag, M);
		for (int j = 0; j < n; j += k)
			for (int *l = a + j, *r = l + m, *e = r, o = 1;
					l < e; l++, r++, o = (ll)o * omg % M) {
				const int tmp = (ll)o * *r % M;
				*r = mod(*l - tmp, M);
				*l = mod(*l + tmp - M, M);
			}
	}
	if (flag == -1) {
		const int invn = power(n, M - 2, M);
		for (int i = 0; i < n; i++)
			a[i] = (ll)a[i] * invn % M;
	}
}

struct poly {
	int *a, n, lim, sup;
	poly(): a(), n(), lim(), sup() {}
	void resize(int m) {
		int tmp = 1 << (lim = limit(n = m));
		a = (int *)realloc(a, tmp << 2);
		if (tmp > sup) fill(a + sup, a + tmp, 0);
		sup = tmp;
	}
	poly(int m) {
		sup = 1 << (lim = limit(n = m));
		a = (int *)malloc(sup << 2);
		fill(a, a + sup, 0);
	}
	poly(int *l, int *r) {
		sup = 1 << (lim = limit(n = r - l));
		a = (int *)malloc(sup << 2);
		copy(l, r, a), fill(a + n, a + sup, 0);
	}
	operator int *() const { return a; }
};

inline void inv(poly &f, poly &g) {
	int n = g.sup;
	poly tf(1); tf[0] = power(g[0], M - 2);
	for (int m = 1, k = 2, l = 4, lim = 2; m < n; m <<= 1, k <<= 1, l <<= 1, lim++) {
		poly tg(g + 0, g + k);
		tg.resize(l), tf.resize(l);
		ntt(tg, lim, 1), ntt(tf, lim, 1);
		for (int i = 0; i < l; i++)
			tf[i] = (ll)tf[i] * (M + 2 - (ll)tf[i] * tg[i] % M) % M;
		ntt(tf, lim, -1);
		tf.resize(k);
	}
	f.resize(g.n);
	copy(tf + 0, tf + g.n, f + 0);
}
inline void deriv(poly &f, poly &g) {
	int n = g.n; f.resize(n - 1);
	for (int i = 1; i < n; i++) f[i - 1] = (ll)i * g[i] % M;
}
inline void integ(poly &f, poly &g, int C = 0) {
	int n = g.n; f.resize(n + 1);
	f[0] = C; for (int i = n; i; i--) f[i] = (ll)power(i, M - 2) * g[i - 1] % M;
}
inline void ln(poly &f, poly &g) {
	int sup = g.sup << 1, lim = g.lim + 1;
	poly t1, t2;
	deriv(t1, g), inv(t2, g);
	t1.resize(sup), t2.resize(sup);
	ntt(t1, lim, 1), ntt(t2, lim, 1);
	for (int i = 0; i < sup; i++) t1[i] = (ll)t1[i] * t2[i] % M;
	ntt(t1, lim, -1);
	t1.resize(g.n - 1);
	integ(f, t1);
}
inline void exp(poly &f, poly &g) {
	int n = g.sup;
	poly tg, tf(1);
	tf[0] = 1;
	for (int m = 1, k = 2, l = 4, lim = 2; m < n; m <<= 1, k <<= 1, l <<= 1, lim++) {
		tf.resize(k), ln(tg, tf);
		for (int i = 0; i < k; i++) tg[i] = mod(g[i] - tg[i], M);
		tg[0] = mod(tg[0] - M + 1, M);
		tf.resize(l), tg.resize(l);
		ntt(tf, lim, 1), ntt(tg, lim, 1);
		for (int i = 0; i < l; i++) tf[i] = (ll)tf[i] * tg[i] % M;
		ntt(tf, lim, -1);
		tf.resize(k);
	}
	copy(tf + 0, tf + n, f + 0);
}

int main() {
	int n;
	scanf("%d", &n);
	poly f(n);
	for (int i = 0; i < n; i++)
		scanf("%d", f + i);
	exp(f, f);
	for (int i = 0; i < n; i++)
		printf("%d ", f[i]);
}