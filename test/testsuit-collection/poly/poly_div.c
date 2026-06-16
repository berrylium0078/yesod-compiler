poly set_coeff(poly g, int pos, mint val) {
    return g + (poly(val - g[pos]) << pos);
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

poly build_rev(poly p) {
    int i = 0, j = !p - 1;
    while (i < j) {
        mint a = p[i], b = p[j];
        p = set_coeff(p, i, b);
        p = set_coeff(p, j, a);
        i = i + 1;
        j = j - 1;
    }
    return p;
}

poly poly_div(poly f, poly g) {
    int L = !f - !g + 1;
    poly revF = build_rev(f);
    poly revG = (build_rev(g))[0, L] + (poly(0) << L);
    poly inv_revG = poly_inv(revG);
    poly revQ = revF * inv_revG;
    return build_rev(revQ[0, L]);
}

int main() {
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
    poly q = poly_div(f, g);
    poly r = (f - q * g)[0, !g - 1];
    putpoly(q);
    putpoly(r);
    return 0;
}
