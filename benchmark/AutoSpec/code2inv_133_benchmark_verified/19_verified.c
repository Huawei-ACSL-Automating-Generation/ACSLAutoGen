/*@
requires n > 0;
*/
void foo(int n)
{
    int x = 0;
    int m = 0;

    /*@
    loop invariant x >= 0 && x <= n;
    loop invariant x == n ==> m >= 0;
    loop invariant x == n ==> m < n;
    loop invariant x <= n;
    loop invariant m >= 0 && m <= x;
    loop invariant m <= x;
    loop invariant 0 <= x;
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
       //@ assert m < n;
       ////@ assert m >= 0;
    }
}
