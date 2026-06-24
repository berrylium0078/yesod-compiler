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
    putpoly(poly(0), 1);
    putpoly(poly(1), 1);
    putpoly(poly(-1), 1);
    putpoly(poly(998244353), 1);
    putpoly(poly(998244354), 1);
    return 0;
}
