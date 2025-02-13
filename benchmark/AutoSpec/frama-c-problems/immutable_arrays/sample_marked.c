/* 2. FUNC CONTRACT */
int fun(int x, int y) {
    int r = x;
    int d = 0;

    /* 1. LOOP INVARIANT */
    while (r >= y) {
        printf("r = %d   d = %d   y = %d   x = %d\n", r, d, y, x);
        // Beginning
        r = r - y;
        d = d + 1;
        // ENd
        //@ assert r + d*y == x;
    }
    return d;
}