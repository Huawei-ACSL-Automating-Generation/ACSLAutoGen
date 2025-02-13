// hhk2008_true-unreach-call.c

/*@
requires x < 1000000;
requires 0 <= y && y <= 1000000;
*/
int foo(int x, int y) {
    //int x;
    //int y;
    europa_make_symbolic(&x, sizeof(x), "x");
    europa_make_symbolic(&y, sizeof(y), "y");
    
    int res, cnt;
    europa_assume(x < 1000000);
    europa_assume(0 <= y && y <= 1000000);

    res = x;
    cnt = y;
    europa_make_symbolic(&res, sizeof(res), "res");
    europa_make_symbolic(&cnt, sizeof(cnt), "cnt");
    
    europa_invariant(cnt >= 0 && res + cnt == x + y);
    while (cnt > 0) {
    	cnt = cnt - 1;
        res = res + 1;
    }

    europa_assert(res == x + y);
    
    return 0;
}
