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
    poly p = poly(4) + (poly(5) << 2);
    putpoly(p >> 0, 3);
    putpoly(p >> 2, 1);
    putpoly(p >> 3, 0);
    putpoly(p >> -2, 5);
    putpoly(p << 0, 3);
    putpoly(p << -2, 1);
    putpoly(p << -3, 0);
    putpoly(p << 2, 5);
    return 0;
}
