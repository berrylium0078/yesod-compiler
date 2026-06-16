int main()
{
    poly p = poly(4) + (poly(5) << 2);
    putpoly(p >> 0);
    putpoly(p >> 2);
    putpoly(p >> 3);
    putpoly(p >> -2);
    putpoly(p << 0);
    putpoly(p << -2);
    putpoly(p << -3);
    putpoly(p << 2);
    return 0;
}
