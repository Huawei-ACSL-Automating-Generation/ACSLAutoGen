/*@
requires a <= m;
*/
void foo(int a, int m) {
    int j = 0;
    int k = 0;

    /*@
    loop invariant k <= 1;
    loop invariant a <= m;
    loop invariant 0 <= k;
    loop assigns m;
    loop assigns k;
    */
    while ( k < 1) {
        if(m < a) {
            m = a;
        }
        k = k + 1;
    }

    //post-condition
    //@ assert a <= m;
}
