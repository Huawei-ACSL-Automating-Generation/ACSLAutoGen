// sum01_true-unreach-call_true-termination.c

/*@
requires 0 <= x;
requires x < 1000;
*/
int foo(int x) {
    //int x;
    europa_make_symbolic(&x, sizeof(x), "x");
    int i=0;
    int sn=0;
    
    europa_assume(0 <= x && x < 1000);
    europa_make_symbolic(&i, sizeof(i), "i");
    europa_make_symbolic(&sn, sizeof(sn), "sn");
    
    europa_invariant(i <= x+1 && sn == 2 * (i-1));
    for (i = 1; i <= x; i++) {
        sn = sn + (2);
    }
    
    europa_assert(sn == x * (2) || sn == 0);
    
    return 0;
}
