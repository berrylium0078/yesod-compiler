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
    poly p = poly(4) + (poly(5) << 2);
    putpoly(p >> 0);
    putpoly(p >> 2);
    putpoly(p >> 3);
    putpoly(p >> -2);
    putpoly(p << 0);
    putpoly(p << -2);
    putpoly(p << -3);
    putpoly(p << 2);
    return 0;
}
