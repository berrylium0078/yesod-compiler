int main()
{
    poly p = poly(1) + (poly(2) << 2) + (poly(3) << 4);
    putpoly(p[0, 5]);
    putpoly(p[1, 4]);
    putpoly(p[3, 3]);
    putpoly(p[6, 8]);
    putpoly(p[2, 100]);
    putpoly(p[1, 0]);
    return 0;
}
