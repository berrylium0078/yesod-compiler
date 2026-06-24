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
    poly p = poly(1) + (poly(2) << 2) + (poly(3) << 4);
    putpoly(p[0, 5], 5);
    putpoly(p[1, 4], 4);
    putpoly(p[3, 3], 0);
    putpoly(p[6, 8], 0);
    putpoly(p[2, 100], 5);
    putpoly(p[1, 0], 0);
    return 0;
}
