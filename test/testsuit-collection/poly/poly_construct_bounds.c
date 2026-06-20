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
    putpoly(poly(0));
    putpoly(poly(1));
    putpoly(poly(-1));
    putpoly(poly(998244353));
    putpoly(poly(998244354));
    return 0;
}
