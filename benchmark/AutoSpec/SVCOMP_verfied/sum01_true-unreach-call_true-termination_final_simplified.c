// sum01_true-unreach-call_true-termination.c

/*@
requires 0 <= n;
requires n < 1000;
*/
int foo(int n) {
    //int n;
    int i=0;
    int sn=0;
    
    
    /*@
    loop invariant sn == (i-1) * 2;
    loop invariant i <= n + 1;
    loop assigns sn;
    loop assigns i;
    */
    for (i = 1; i <= n; i++) {
        sn = sn + (2);
    }
    
    //@ assert sn == n * (2) || sn == 0;
    
    return 0;
}
