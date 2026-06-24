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

poly globalValue;
poly globalArray[2];

poly echo(poly p) { return p; }

void copy_global_to_array() { globalArray[0] = globalValue; }

int main()
{
    globalValue = poly(4) + (poly(5) << 1);
    copy_global_to_array();

    poly local = globalArray[0];
    globalValue = globalValue + poly(10);
    globalArray[0] = globalArray[0] + (poly(20) << 1);

    putpoly(local, 2);
    putpoly(globalValue, 2);
    putpoly(globalArray[0], 2);

    globalArray[1] = echo(globalValue);
    globalValue = poly(0);
    putpoly(globalArray[1], 2);
    putpoly(globalValue, 1);
    return 0;
}
