/*@
requires n > 0;
*/
void foo(int n) {
    int x = 1;
    int y = 0;

    /*@
    loop invariant y <= n - 1;
    loop invariant 0 <= y;
    loop assigns n;
    loop invariant x <= n+1;
    loop invariant x <= n + 1;
    loop invariant 1 <= x;
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
      ////@ assert y < n;
    }
}
