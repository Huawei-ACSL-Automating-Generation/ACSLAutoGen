/*@
requires a <= m;
*/
void foo(int a, int m, int c) {
    int j = 0;
    int k = 0;

    /*@
    loop invariant a <= m;
    loop invariant 0 <= k;
    loop invariant (m < a ==> m == a);
    loop assigns m;
    loop assigns k;
    loop assigns a;
    */
    while (k < c) {
        if(m < a) {
            m = a;
        }
        k = k + 1;
    }

    //post-condition
    //@ assert a <= m;
}
