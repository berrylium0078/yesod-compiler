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

poly g;
poly h;
mint scale;

void bump_global() { g = g + poly(1) + (poly(2) << 1); }

int main()
{
    g = poly(5) + (poly(6) << 1);
    putpoly(g);

    h = g;
    g = g + (poly(7) << 2);
    putpoly(h);
    putpoly(g);

    bump_global();
    putpoly(g);

    scale = mint(-2);
    g = g * scale;
    putpoly(g);
    return 0;
}
