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

mint globalMint;
mint mintArray[3];
poly globalPoly;
poly polyArray[2];

int main()
{
    globalMint = mint(-1);
    mintArray[0] = mint(2);
    mintArray[1] = mint(998244354);
    mintArray[2] = mint(-3);

    globalPoly = poly(mintArray[0]) + (poly(globalMint) << 1)
        + (poly(mintArray[2]) << 2);
    putpoly(globalPoly, 3);

    polyArray[0] = globalPoly * mintArray[1];
    polyArray[1] = mintArray[2] * (poly(4) + (poly(5) << 1));
    putpoly(polyArray[0], 3);
    putpoly(polyArray[1], 2);

    globalMint = globalMint + mint(2);
    polyArray[0] = polyArray[0] * globalMint;
    putpoly(polyArray[0], 3);
    return 0;
}
