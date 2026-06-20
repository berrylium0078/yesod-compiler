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

int main()
{
    poly a = poly(1) + (poly(2) << 1);
    poly b = poly(3) + (poly(4) << 1) + (poly(5) << 2);
    poly zero = (poly(9) >> 1);
    putpoly(a * b);
    putpoly(a * zero);
    putpoly(a * mint(-1));
    putpoly(998244354 * a);
    return 0;
}
