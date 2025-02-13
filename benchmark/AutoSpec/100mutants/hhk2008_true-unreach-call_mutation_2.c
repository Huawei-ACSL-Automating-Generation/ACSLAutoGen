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
    
    int cnt, res;
    europa_assume(x < 1000000);
    europa_assume(0 <= y && y <= 1000000);

    cnt = x;
    res = y;
    europa_make_symbolic(&cnt, sizeof(cnt), "cnt");
    europa_make_symbolic(&res, sizeof(res), "res");
    
    europa_invariant(res >= 0 && cnt + res == x + y);
    while (res > 0) {
    	res = res - 1;
        cnt = cnt + 1;
    }

    europa_assert(cnt == x + y);
    
    return 0;
}

