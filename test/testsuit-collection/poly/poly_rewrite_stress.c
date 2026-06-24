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

int max_int(int a, int b)
{
    if (a > b) {
        return a;
    }
    return b;
}

int min_int(int a, int b)
{
    if (a < b) {
        return a;
    }
    return b;
}

int len_add(int a, int b) { return max_int(a, b); }

int len_mul(int a, int b)
{
    if (a == 0 || b == 0) {
        return 0;
    }
    return a + b - 1;
}

int len_slice(int n, int l, int r)
{
    if (r <= 0 || l >= r || l >= n) {
        return 0;
    }
    return min_int(r, n);
}

int len_shr(int n, int k)
{
    if (k >= n) {
        return 0;
    }
    return n - k;
}

int len_shl(int n, int k) { return len_shr(n, -k); }

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

int coeff_xor(poly p, int n)
{
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
    putint(dynamic_len ^ coeff_xor(p, dynamic_len));
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

    int expr1_left_len = len_shr(
        len_slice(len_add(len_mul(na, nb), len_mul(nc, nd)), l1, r1), k1);
    int expr1_mid_len = len_shl(len_slice(len_add(ne, nf), l2, r2), k2);
    int expr1_right_len = len_shl(
        len_slice(len_shr(len_slice(len_add(na, nc), l3, r3), k3), l4, r4), k4);
    int expr1_len = len_shr(
        len_slice(
            len_add(len_add(expr1_left_len, expr1_mid_len), expr1_right_len),
            l5, r5),
        k5);

    emit_expr(
        ((((a * b + c * d)[l1, r1] >> k1) + (((e - f)[l2, r2] << k2) * mint(s1))
             - ((((a + c * mint(s2))[l3, r3] >> k3)[l4, r4] << k4))
                 * mint(s3))[l5, r5]
            >> k5),
        expr1_len);

    int expr2_left_len
        = len_slice(len_mul(len_mul(na, nb), len_add(nc, nd)), l1, r6);
    int expr2_mid_len
        = len_shr(len_slice(len_mul(ne, len_add(na, nb)), l2, r2), k6);
    int expr2_right_len
        = len_slice(len_shl(len_add(len_slice(len_mul(nc, nd), l3, r3),
                                len_slice(len_mul(na, ne), l4, r4)),
                        k7),
            l6, r7);
    int expr2_len = len_shl(
        len_slice(
            len_add(len_add(expr2_left_len, expr2_mid_len), expr2_right_len),
            l8, r8),
        k8);

    emit_expr(
        ((((a * b) * (c + d))[l1, r6] + ((e * (a - b))[l2, r2] >> k6)
             - ((((c * d)[l3, r3] + (a * e)[l4, r4]) << k7)[l6, r7]))[l8, r8]
            << k8),
        expr2_len);

    poly expr3a1 = (a + b * mint(s4))[l1, r1] >> k1;
    poly expr3a2 = (expr3a1[l2, r2] << k2);
    poly expr3a = expr3a2[l3, r3];
    poly expr3b
        = ((((c - d * mint(s5))[l4, r4] << k3)[l5, r5] >> k4) * mint(s6));
    poly expr3_base = expr3a - expr3b;
    poly expr3 = expr3_base[l6, r6];
    int expr3a1_len = len_shr(len_slice(len_add(na, nb), l1, r1), k1);
    int expr3a2_len = len_shl(len_slice(expr3a1_len, l2, r2), k2);
    int expr3a_len = len_slice(expr3a2_len, l3, r3);
    int expr3b_len = len_shr(
        len_slice(len_shl(len_slice(len_add(nc, nd), l4, r4), k3), l5, r5), k4);
    int len3 = len_slice(len_add(expr3a_len, expr3b_len), l6, r6);
    emit_expr(expr3, len3);

    poly expr4_base = (a[l1, r1] >> k1) * mint(s1)
        - (((b << k2)[l2, r2]) * mint(s2))
        + (((c + d)[l3, r3] >> k3) * mint(s3))
        - (((e - f)[l4, r4] << k4) * mint(s4))
        + ((((a - c)[l5, r5] >> k5)[l6, r6] << k6) * mint(s5));
    poly expr4 = expr4_base[l7, r7];
    int expr4_len = len_add(len_add(len_shr(len_slice(na, l1, r1), k1),
                                len_slice(len_shl(nb, k2), l2, r2)),
        len_add(len_shr(len_slice(len_add(nc, nd), l3, r3), k3),
            len_add(len_shl(len_slice(len_add(ne, nf), l4, r4), k4),
                len_shl(
                    len_slice(len_shr(len_slice(len_add(na, nc), l5, r5), k5),
                        l6, r6),
                    k6))));
    int len4 = len_slice(expr4_len, l7, r7);
    emit_expr(expr4, len4);

    poly scalar_poly = poly(s1);
    poly left5a = (a * (b + c))[l1, r1] >> k1;
    poly left5b = (d * e)[l2, r2] - f;
    poly left5c = ((((left5b)[l3, r3]) << k2) * mint(s6));
    poly left5 = left5a + left5c;
    poly right5 = (a + scalar_poly)[l4, r4];
    int left5a_len
        = len_shr(len_slice(len_mul(na, len_add(nb, nc)), l1, r1), k1);
    int left5b_len = len_add(len_mul(nd, ne), nf);
    int left5c_len = len_shl(len_slice(left5b_len, l3, r3), k2);
    int left5_len = len_add(left5a_len, left5c_len);
    int right5_len = len_slice(len_add(na, 1), l4, r4);
    int expr5_len
        = len_shr(len_slice(len_mul(left5_len, right5_len), l5, r5), k5);

    emit_expr(((left5 * right5)[l5, r5] >> k5), expr5_len);

    poly acc = (a + b)[l1, r1] >> k1;
    int acc_len = len_shr(len_slice(len_add(na, nb), l1, r1), k1);
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
        int term1_len = len_shr(
            len_slice(len_add(len_mul(na, nb), len_mul(acc_len, nc)), dl, dr),
            dk);
        int term2_len = len_shr(
            len_slice(
                len_shl(len_slice(len_add(acc_len, nd), l2, r2), k2), l3, r3),
            k3);
        int term3_len = len_shr(
            len_slice(len_shl(len_add(len_slice(len_mul(ne, nf), l4, r4),
                                  len_slice(len_add(na, nb), l5, r5)),
                          k4),
                l6, r6),
            k5);
        acc_len = len_add(
            len_add(acc_len, term1_len), len_add(term2_len, term3_len));
        loop_xor = loop_xor ^ acc_len ^ coeff_xor(acc, acc_len);
        i = i + 1;
    }
    putint(loop_xor);
    putch(10);

    return 0;
}
