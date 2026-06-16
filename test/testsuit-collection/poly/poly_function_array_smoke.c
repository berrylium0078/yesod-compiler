poly make_poly(int a, int b)
{
    return poly(a) + (poly(b) << 1);
}

poly add_one(poly p)
{
    return p + poly(1);
}

int main()
{
    poly arr[2];
    arr[0] = make_poly(2, 3);
    arr[1] = add_one(arr[0]);
    putpoly(arr[0]);
    putpoly(arr[1]);
    return int(arr[1][0]);
}
