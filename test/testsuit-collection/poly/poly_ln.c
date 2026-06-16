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
    return (integral((derivative(f) * poly_inv(f))[0, !f - 1]))[0, !f];
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
    poly result = poly_ln(f);
    putpoly(result);
    return 0;
}
