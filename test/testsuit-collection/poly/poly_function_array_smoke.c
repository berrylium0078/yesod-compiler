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

poly make_poly(int a, int b) { return poly(a) + (poly(b) << 1); }

poly add_one(poly p) { return p + poly(1); }

int main()
{
    poly arr[2];
    arr[0] = make_poly(2, 3);
    arr[1] = add_one(arr[0]);
    putpoly(arr[0], 2);
    putpoly(arr[1], 2);
    return int(arr[1][0]);
}
