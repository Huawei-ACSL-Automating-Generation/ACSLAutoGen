// hhk2008_true-unreach-call.c

/*@
requires a < 1000000;
requires 0 <= b && b <= 1000000;
*/
int foo(int a, int b) {
    //int a;
    //int b;
    europa_make_symbolic(&b, sizeof(b), "b");
    europa_make_symbolic(&a, sizeof(a), "a");
    
    int res, cnt;
    europa_assume(0 <= b && b <= 1000000);
    europa_assume(a < 1000000);

    cnt = b;
    res = a;
    europa_make_symbolic(&cnt, sizeof(cnt), "cnt");
    europa_make_symbolic(&res, sizeof(res), "res");
    
    europa_invariant(cnt >= 0 && res + cnt == a + b);
    while (cnt > 0) {
        res = res + 1;
    	cnt = cnt - 1;
    }

    europa_assert(res == a + b);
    
    return 0;
}
