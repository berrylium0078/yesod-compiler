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

poly store[4];

poly make_poly3(int a, int b, int c)
{
    return poly(a) + (poly(b) << 1) + (poly(c) << 2);
}

void write_pair(poly a[], int base)
{
    int next = base + 1;
    a[base] = make_poly3(1, 2, 3);
    poly current = a[base];
    a[next] = current[1, 3] + poly(5);
}

void rotate_first_three(poly a[])
{
    poly tmp = a[0];
    a[0] = a[1];
    a[1] = a[2];
    a[2] = tmp;
}

int main()
{
    write_pair(store, 1);
    putpoly(store[1]);
    putpoly(store[2]);

    store[0] = poly(9);
    rotate_first_three(store);
    putpoly(store[0]);
    putpoly(store[1]);
    putpoly(store[2]);
    return 0;
}
