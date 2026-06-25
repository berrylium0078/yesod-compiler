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
    poly empty = (poly(7) + (poly(8) << 2))[1, 1];
    poly a = poly(2) + (poly(3) << 2);
    poly b = poly(1) + (poly(2) << 1);
    poly c = poly(3) + (poly(4) << 2);
    putpoly(empty + a, 4);
    putpoly(a + empty, 4);
    putpoly(empty - a, 4);
    putpoly(a - empty, 4);
    putpoly(empty * a, 0);
    putpoly(a * empty, 0);
    putpoly(b * c, 5);
    return 0;
}
