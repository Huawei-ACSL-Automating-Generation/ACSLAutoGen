/* 2. FUNC CONTRACT */
int fun(int x, int y , int *r) {
    *r = x;
    int d = 0;

    /* 1. LOOP INVARIANT */
    while (*r >= y) {
        *r = *r - y;
        d = d + 1;
    }
    return d;
}

/* 3. FUNC CONTRACT */
int main() {
    int a = 3;
    int num = fun(1, 2, &a);
    //@ assert a == 1;
    //@ assert num == 0;
    return 0;
}