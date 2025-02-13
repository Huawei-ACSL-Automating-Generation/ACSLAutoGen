
int main() {
    int x = 1;
    int y = 0;

    /*@
    loop invariant y <= 100;
    loop invariant 0 <= y;
    loop invariant -99 <= y;
    loop invariant x <= 101;
    loop invariant 1 <= x;
    loop invariant 0 <= x;
    loop assigns y;
    loop assigns x;
    */
    while (x <= 100) {
        y = 100 - x;
        x = x +1;
    }

    //post-condition
    //@ assert y >= 0;
    ////@ assert y < 100;
}
