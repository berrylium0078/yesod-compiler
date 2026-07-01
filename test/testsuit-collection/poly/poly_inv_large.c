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

int main()
{
    int n = getint();
    poly f = poly(0);
    int i = 0;
    while (i < n) {
        mint c = mint(getint());
        f = f + (poly(c) << i);
        i = i + 1;
    }
    poly g = poly_inv(f, n);
    putpoly(g, n);
    return 0;
}
