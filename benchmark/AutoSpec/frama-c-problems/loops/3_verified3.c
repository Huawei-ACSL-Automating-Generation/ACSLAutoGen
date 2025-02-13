#include <stdio.h>
/*@
    ensures c > 0 ==> \result == c;
    ensures c == 0 ==> \result == 0;
    ensures c < 0 ==> \result == 0;
    ensures \result == 0 ==> c <= 0;
    assigns \nothing;
*/
int func(int c) {
    int x = c;
    int y = 0;
    /*@
        loop invariant c == x + y;
	loop invariant c < 0 ==> x < 0;
	loop invariant x >= 0 || x == c;
        loop assigns x, y;
    */
    while(x > 0) {
        x = x - 1;
        y = y + 1;
    }
    return y;
}

// write a test
void main() {
    int t = func(5);
    //@ assert t == 5;
    int b = func(-1);
    //@ assert b == 0;
}
