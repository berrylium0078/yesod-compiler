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

int lowbit(int x) {
    return x & (-x);
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
poly poly_exp_dc(poly f) {
    // Semi-online convolution via lowbit-based CDQ (分治优化半在线卷积)
    // From G' = G * F':  g[0]=1, g[i] = (1/i) * sum_{k< i} g[k] * (i-k) * f[i-k]
    // Let h[j] = j * f[j], then convolution contributions accumulate into g.
    // lowbit(i+1)-sized blocks are convolved with h as they complete.

    // Build h[j] = j * f[j]  (h[0] = 0 since f[0] = 0)
    poly h = derivative(f) << 1;
    poly g = poly(1);  // g[0] = 1

    int i = 0;
    while (i < !f) {
        // Finalize g[i]: divide by i
        if (i != 0) {
            mint gi = g[i] / mint(i);
            g =/* move */ set_coeff(g/* move */, i, gi);
        }
        i = i + 1;
        // lowbit(i) determines the block that just completed
        int len = lowbit(i);
        // accumulate contributions
        g = g + (g[i - len, i] * h)[i, i + len];
    }

    g = g[0, !f];
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
    poly result = poly_exp_dc(f);
    putpoly(result);
    return 0;
}
