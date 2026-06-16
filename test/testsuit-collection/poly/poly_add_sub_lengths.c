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
