#include <stdio.h>

/* 2. FUNC CONTRACT */
int fun(int n) {
    int i = 7;
    int x = 1;

    /* 1. LOOP INVARIANT */
    while(i <= n) {
        x += 1;
        i += 3;
    }
    return x;
}

/* 3. FUNC CONTRACT */
int main() {
    int a = fun(10);
    //@ assert a == 3;
    printf("%d\n", a);
}