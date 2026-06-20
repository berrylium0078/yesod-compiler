void putpoly(poly f)
{
    putint(!f);
    putch(58);
    int i = 0;
    while (i < !f) {
        putch(32);
        putint(int(f[i]));
        i = i + 1;
    }
    putch(10);
}

poly set_coeff(poly g, int pos, mint val) {
    return g + (poly(val - g[pos]) << pos);
}
poly derivative(poly f) {
    int i = 1;
    while (i < !f) {
        f = set_coeff(f, i - 1, f[i] * mint(i));
        i = i + 1;
    }
    return f;
}
poly poly_inv(poly f) {
    poly g = poly(mint(1) / f[0]);
    int n = !f, k = 1;
    while (k < n) {
        // len(g) == k
        g = g - (f * g * g)[k, k * 2][0, n];
        k = k * 2;
    }
    return g;
}
poly integral(poly f) {
    int i = !f;
    while (i > 0) {
        f = set_coeff(f, i, f[i - 1] / mint(i));
        i = i - 1;
    }
    f = set_coeff(f, 0, mint(0));
    return f;
}

poly poly_ln(poly f) {
    poly g = derivative(f) * poly_inv(f);
    g = g[0, !f - 1];
    return (integral(g))[0, !f];
}

poly poly_exp_newton(poly f) {
    poly g = poly(1);
    int n = !f, k = 1;
    while (k < n) {
        int new_k = k * 2;
        if (new_k > n) {
            new_k = n;
        }
        poly pad_g = g + (poly(0) << new_k);
        poly ln_g = poly_ln(pad_g);
        g = g + (g * (f - ln_g))[k, new_k];
        k = new_k;
    }
    return g;
}

int main() {
    int n = getint();
    poly f = poly(0);
    int i = 0;
    while (i < n) {
        mint c = mint(getint());
        f = f + (poly(c) << i);
        i = i + 1;
    }
    poly result = poly_exp_newton(f);
    putpoly(result);
    return 0;
}
