/*@
requires n > 0;
*/
void foo(int n) {
    int y = 0;
    int x = 1;

    /*@
    loop invariant y <= n;
    loop invariant x <= n+1;
    loop invariant x <= n + 1;
    loop invariant 1 <= x;
    loop invariant 0 <= y;
    loop invariant 0 <= x;
    loop assigns y;
    loop assigns x;
    */
    while (x <= n) {
        y = n - x;
        x = x +1;
    }

    //post-condition
    if (n > 0) {
        //@ assert y >= 0;
        ////@ assert y <= n;
    }
}
