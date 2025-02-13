#include<limits.h>

/* @ >>> INFILL <<< */
int test(int x) {
    int a = x;
    int y = 0;

    /*@
    loop invariant y == x - a;
    loop invariant y <= INT_MAX;
    loop invariant x >= INT_MIN && x <= INT_MAX;
    loop invariant a == x - y;
    loop invariant 0 <= y;
    loop assigns y;
    loop assigns a;
    */
    while(a != 0) {
        y = y + 1;
        a = a - 1;
    }
    return y;
}
    
int main() {
    int num = test(3);
    // @ assert num == 3;
    return 0;
}