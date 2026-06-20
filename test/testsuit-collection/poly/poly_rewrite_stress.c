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

int g_seed;

int next_rand()
{
    g_seed = (g_seed * 73 + 19) % 1000003;
    return g_seed;
}

int norm_mint(int x)
{
    if (x < 0) {
        return -x + 998;
    }
    return x + 17;
}

poly random_poly(int n)
{
    poly p = poly(0) >> 1;
    int i = 0;
    while (i < n) {
        int v = norm_mint(next_rand());
        p = p + (poly(mint(v)) << i);
        i = i + 1;
    }
    return p;
}

int coeff_xor(poly p)
{
    int n = !p;
    int i = 0;
    int x = 0;
    while (i < n) {
        x = x ^ int(p[i]);
        i = i + 1;
    }
    return x;
}

void emit_expr(poly p, int dynamic_len)
{
    putint(dynamic_len ^ coeff_xor(p));
    putch(10);
}

int main()
{
    g_seed = getint();
    int na = getint();
    int nb = getint();
    int nc = getint();
    int nd = getint();
    int ne = getint();
    int nf = getint();

    int l1 = getint();
    int r1 = getint();
    int k1 = getint();
    int l2 = getint();
    int r2 = getint();
    int k2 = getint();
    int l3 = getint();
    int r3 = getint();
    int k3 = getint();
    int l4 = getint();
    int r4 = getint();
    int k4 = getint();
    int l5 = getint();
    int r5 = getint();
    int k5 = getint();
    int l6 = getint();
    int r6 = getint();
    int k6 = getint();
    int l7 = getint();
    int r7 = getint();
    int k7 = getint();
    int l8 = getint();
    int r8 = getint();
    int k8 = getint();

    int s1 = getint();
    int s2 = getint();
    int s3 = getint();
    int s4 = getint();
    int s5 = getint();
    int s6 = getint();
    int rounds = getint();

    poly a = random_poly(na);
    poly b = random_poly(nb);
    poly c = random_poly(nc);
    poly d = random_poly(nd);
    poly e = random_poly(ne);
    poly f = random_poly(nf);

    emit_expr(
        ((((a * b + c * d)[l1, r1] >> k1)
             + (((e - f)[l2, r2] << k2) * mint(s1))
             - ((((a + c * mint(s2))[l3, r3] >> k3)[l4, r4] << k4))
                * mint(s3))
             [l5, r5]
             >> k5),
        !(((((a * b + c * d)[l1, r1] >> k1)
               + (((e - f)[l2, r2] << k2) * mint(s1))
               - ((((a + c * mint(s2))[l3, r3] >> k3)[l4, r4] << k4))
                  * mint(s3))
               [l5, r5]
               >> k5)));

    emit_expr(
        ((((a * b) * (c + d))[l1, r6]
             + ((e * (a - b))[l2, r2] >> k6)
             - ((((c * d)[l3, r3] + (a * e)[l4, r4]) << k7)[l6, r7]))
             [l8, r8]
             << k8),
        !(((((a * b) * (c + d))[l1, r6]
               + ((e * (a - b))[l2, r2] >> k6)
               - ((((c * d)[l3, r3] + (a * e)[l4, r4]) << k7)[l6, r7]))
               [l8, r8]
               << k8)));

    poly expr3a1 = (a + b * mint(s4))[l1, r1] >> k1;
    poly expr3a2 = (expr3a1[l2, r2] << k2);
    poly expr3a = expr3a2[l3, r3];
    poly expr3b
        = ((((c - d * mint(s5))[l4, r4] << k3)[l5, r5] >> k4) * mint(s6));
    poly expr3_base = expr3a - expr3b;
    poly expr3 = expr3_base[l6, r6];
    int len3 = !expr3_base[l6, r6];
    emit_expr(expr3, len3);

    poly expr4_base = (a[l1, r1] >> k1) * mint(s1)
        - (((b << k2)[l2, r2]) * mint(s2))
        + (((c + d)[l3, r3] >> k3) * mint(s3))
        - (((e - f)[l4, r4] << k4) * mint(s4))
        + ((((a - c)[l5, r5] >> k5)[l6, r6] << k6) * mint(s5));
    poly expr4 = expr4_base[l7, r7];
    int len4 = !expr4_base[l7, r7];
    emit_expr(expr4, len4);

    poly scalar_poly = poly(s1);
    poly left5a = (a * (b + c))[l1, r1] >> k1;
    poly left5b = (d * e)[l2, r2] - f;
    poly left5c = ((((left5b)[l3, r3]) << k2) * mint(s6));
    poly left5 = left5a + left5c;
    poly right5 = (a + scalar_poly)[l4, r4];

    emit_expr(((left5 * right5)[l5, r5] >> k5),
        !(((left5 * right5)[l5, r5] >> k5)));

    poly acc = (a + b)[l1, r1] >> k1;
    int i = 0;
    int loop_xor = 0;
    while (i < rounds) {
        int dl = l1 + i;
        int dr = r8 - i;
        int dk = k1 - i;
        poly term1 = (((a * b + acc * c)[dl, dr] >> dk) * mint(s1 + i));
        poly term2
            = (((((acc + d)[l2, r2] << k2)[l3, r3] >> k3) * mint(s2 + i)));
        poly term3
            = (((((e * f)[l4, r4] + (a - b)[l5, r5]) << k4)[l6, r6] >> k5)
                * mint(s3 + i));
        acc = acc + term1 - term2 + term3;
        loop_xor = loop_xor ^ (!acc) ^ coeff_xor(acc);
        i = i + 1;
    }
    putint(loop_xor);
    putch(10);

    return 0;
}
