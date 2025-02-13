#include<limits.h>

/* 2. FUNC CONTRACT */
int test(int x) {
    int a = x;
    int y = 0;

    /* 1. LOOP INVARIANT */
    while(a != 0) {
        y = y + 1;
        a = a - 1;
    }
    return y;
}
    
/* 3. FUNC CONTRACT */
int main() {
    int num = test(3);
    //@ assert num == 3;
    return 0;
}