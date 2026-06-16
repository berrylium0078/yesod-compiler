int main()
{
    poly p = poly(5) + (poly(7) << 2);
    putint(int(p[-1]));
    putch(10);
    putint(int(p[0]));
    putch(10);
    putint(int(p[1]));
    putch(10);
    putint(int(p[2]));
    putch(10);
    putint(int(p[3]));
    putch(10);
    putint(int(p[100000]));
    putch(10);
    return 0;
}
