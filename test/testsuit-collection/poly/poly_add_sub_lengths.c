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

int main()
{
    poly a = poly(-1) + (poly(2) << 2);
    poly b = poly(2) + (poly(3) << 1);
    putpoly(a + b, 3);
    putpoly(a - b, 3);
    putpoly(b - a, 3);
    putpoly(a + poly(0), 3);
    return 0;
}
