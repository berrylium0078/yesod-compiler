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
poly derivative(poly f, int n)
{
    int i = 1;
    while (i < n) {
        f = set_coeff(f, i - 1, f[i] * mint(i));
        i = i + 1;
    }
    return f;
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
poly integral(poly f, int n)
{
    int i = n;
    while (i > 0) {
        f = set_coeff(f, i, f[i - 1] / mint(i));
        i = i - 1;
    }
    f = set_coeff(f, 0, mint(0));
    return f;
}

poly poly_ln(poly f, int n)
{
    poly g = derivative(f, n) * poly_inv(f, n);
    g = g[0, n - 1];
    return (integral(g, n - 1))[0, n];
}

poly poly_exp_newton(poly f, int n)
{
    poly g = poly(1);
    int k = 1;
    while (k < n) {
        int new_k = k * 2;
        if (new_k > n) {
            new_k = n;
        }
        poly pad_g = g + (poly(0) << new_k);
        poly ln_g = poly_ln(pad_g, new_k);
        g = g + (g * (f - ln_g))[k, new_k];
        k = new_k;
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
    poly result = poly_exp_newton(f, n);
    putpoly(result, n);
    return 0;
}
