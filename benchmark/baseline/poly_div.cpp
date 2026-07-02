#include <cstdio>
#include <iostream>
#include <cassert>
#include <cstring>
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
	~poly() { free(a); }
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

#define print(f) ({ printf(#f": "); for (int i = 0; i < f.n; i++) printf("%d ", f[i]); puts(""); })

inline void mult(poly &prod, poly &f, poly &g) {
	int n = f.n + g.n - 1;
	poly F(f + 0, f + f.n), G(g + 0, g + g.n);
	F.resize(n), G.resize(n);
	ntt(F, F.lim, 1), ntt(G, G.lim, 1);
	for (int i = 0; i < F.sup; i++) F[i] = (ll)F[i] * G[i] % M;
	ntt(F, F.lim, -1);
	prod.resize(n), copy(F + 0, F + n, prod + 0);
}

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
inline void power(poly &f, poly &g, int m) {
	assert(g[0] == 1);
	ln(f, g);
	for (int i = 0; i < f.n; i++) f[i] = (ll)f[i] * m % M;
	exp(f, f);
}
inline void power_s(poly &f, poly &g, int m1, int m2, int tm) {
	int n = g.n, t = 0; for (; t < n && !g[t]; t++);
	int sft = t * tm, tn = n - sft;
	if ((ll)t * tm >= n) {
		f.resize(n);
		fill(f + 0, f + n, 0);
		return;
	}
	int inv = power(g[t], M - 2), mult = power(g[t], m2);
	for (int i = 0; i < tn; i++) f[i] = (ll)inv * g[i + t] % M;
	f.resize(tn), power(f, f, m1), f.resize(n);
	for (int i = n - 1; i >= sft; i--) f[i] = (ll)mult * f[i - sft] % M;
	fill(f + 0, f + sft, 0);
}
inline void reverse(poly &f, poly &g) {
	if (&f == &g)
		for (int i = 0, j = g.n - 1, t; i < j; i++, j--)
			swap(f[i], f[j]);
	else {
		f.resize(g.n);
		for (int i = 0, j = g.n - 1; i < g.n; i++, j--)
			f[i] = g[j];
	}
}
inline void div(poly &q, poly &f, poly &g) {
	poly F, G;
	reverse(F, f), reverse(G, g), G.resize(F.n);
	inv(G, G);
	mult(q, F, G);
	q.resize(f.n - g.n + 1);
	reverse(q, q);
}

int main() {
	int n, m;
	scanf("%d %d", &n, &m);
	poly f(n + 1), g(m + 1), q, r;
	for (int i = 0; i <= n; i++) scanf("%d", f + i);
	for (int i = 0; i <= m; i++) scanf("%d", g + i);
	div(q, f, g);
	mult(r, q, g), r.resize(m);
	for (int i = 0; i < r.n; i++)
		r.a[i] = mod(f[i] - r.a[i], M);
	for (int i = 0; i < q.n; i++)
		printf("%d ", q[i]);
	puts("");
	for (int i = 0; i < r.n; i++)
		printf("%d ", r[i]);
	puts("");
}