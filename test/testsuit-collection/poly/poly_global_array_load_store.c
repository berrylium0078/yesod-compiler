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

poly globalArray[3];

poly make_poly(int a, int b) { return poly(a) + (poly(b) << 1); }

void swap_wrong(poly a[], int i, int j)
{
    a[i] = a[i] + a[j];
    a[j] = a[i] - a[j];
    a[i] = a[i] - a[j];
}

void bump_slot(poly a[], int idx, int value) { a[idx] = a[idx] + poly(value); }

int main()
{
    globalArray[0] = make_poly(1, 10);
    globalArray[1] = make_poly(2, 20);
    globalArray[2] = make_poly(3, 30);
    putpoly(globalArray[0], 2);
    putpoly(globalArray[1], 2);
    putpoly(globalArray[2], 2);

    swap_wrong(globalArray, 0, 2);
    putpoly(globalArray[0], 2);
    putpoly(globalArray[1], 2);
    putpoly(globalArray[2], 2);

    swap_wrong(globalArray, 1, 1);
    putpoly(globalArray[1], 2);

    bump_slot(globalArray, 1, 7);
    putpoly(globalArray[1], 2);
    return 0;
}
