void putpoly(poly f, int n)
{
    putint(n);
    putch(58);
    int i = 0;
    while (i < n) {
        putch(32);
        putint(int(f[i]));
        i = i + 1;
    }
    putch(10);
}

poly set_coeff(poly g, int pos, mint val)
{
    return g + (poly(val - g[pos]) << pos);
}

poly poly_inv(poly f, int n)
{
    poly g = poly(mint(1) / f[0]);
    int k = 1;
    while (k < n) {
        // len(g) == k
        g = g - (f * g * g)[k, k * 2][0, n];
        k = k * 2;
    }
    return g;
}

poly build_rev(poly p, int n)
{
    int i = 0, j = n - 1;
    while (i < j) {
        mint a = p[i], b = p[j];
        p = set_coeff(p, i, b);
        p = set_coeff(p, j, a);
        i = i + 1;
        j = j - 1;
    }
    return p;
}

poly poly_div(poly f, int fLen, poly g, int gLen)
{
    int L = fLen - gLen + 1;
    poly revF = build_rev(f, fLen);
    poly revG = (build_rev(g, gLen))[0, L] + (poly(0) << L);
    poly inv_revG = poly_inv(revG, L);
    poly revQ = revF * inv_revG;
    return build_rev(revQ[0, L], L);
}

int main()
{
    int n = getint();
    int m = getint();
    poly f = poly(0);
    poly g = poly(0);
    int i = 0;
    while (i <= n) {
        f = f + (poly(getint()) << i);
        i = i + 1;
    }
    i = 0;
    while (i <= m) {
        g = g + (poly(getint()) << i);
        i = i + 1;
    }
    int fLen = n + 1;
    int gLen = m + 1;
    int qLen = fLen - gLen + 1;
    int rLen = gLen - 1;
    poly q = poly_div(f, fLen, g, gLen);
    poly r = (f - q * g)[0, rLen];
    putpoly(q, qLen);
    putpoly(r, rLen);
    return 0;
}
