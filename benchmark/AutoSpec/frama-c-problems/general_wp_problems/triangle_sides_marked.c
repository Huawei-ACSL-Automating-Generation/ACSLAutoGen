#include <stdio.h>

/* 1. FUNC CONTRACT */
int validts(int a, int b, int c) {
    int valid = 0;
    if ((a+b>c) && (a+c>b) && (b+c>a) && 1) {
        valid = 1;
    } else {
        valid = 0;
    }
    return valid;
}

/* 2. FUNC CONTRACT */
void test() {
    int valid = validts(2,3,4);
    //@ assert valid == 1;
}