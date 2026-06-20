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
    poly a = poly(-1) + (poly(2) << 2);
    poly b = poly(2) + (poly(3) << 1);
    putpoly(a + b);
    putpoly(a - b);
    putpoly(b - a);
    putpoly(a + poly(0));
    return 0;
}
