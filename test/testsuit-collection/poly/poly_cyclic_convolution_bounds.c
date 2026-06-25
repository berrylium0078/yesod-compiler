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
    poly a = poly(0) + (poly(1) << 4);
    poly b = poly(0) + (poly(1) << 4);
    poly c = a * b;
    putpoly(c, 9);
    return 0;
}
