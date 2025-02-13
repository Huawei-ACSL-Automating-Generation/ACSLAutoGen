/*@
requires n > 0;
*/
void foo(int n)
{
    int x = 0;
    int m = 0;

    /*@
    loop invariant x <= n;
    loop invariant m <= x;
    loop invariant m <= n;
    loop invariant 0 <= x;
    loop invariant 0 <= m;
    loop invariant (m < x) || (m >= x);
    loop assigns x;
    loop assigns m;
    */
    while (x < n) {
        if (unknown()) {
            m = x;
        }
        x = x + 1;
    }

    //post-condition
    if(n > 0) {
       ////@ assert m < n;
       //@ assert m >= 0;
    }
}
