int main()
{
    int x = 1;
    int m = 1;
    int n;

    /*@
    loop invariant n > 1 ==> m >= 1;
    loop invariant n > 1 ==> m < n;
    loop invariant m <= x;
    loop invariant 1 <= x;
    loop invariant 1 <= m;
    loop assigns x;
    loop assigns n;
    loop assigns m;
    */
    while (x < n) {
        if (unknown()) {
            m = x;
        }
        x = x + 1;
    }

    //post-condition
    if(n > 1) {
       ////@ assert m < n;
       //@ assert m >= 1;
    }
}
