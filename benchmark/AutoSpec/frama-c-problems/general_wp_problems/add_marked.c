#include <limits.h>

/* 1. FUNC CONTRACT */
int add(int x, int y) {
    return x+y;
}

/* 2. FUNC CONTRACT */
void foo() {
    int a = add(1, 43);
    //@ assert a == 44;
    int b = add(INT_MAX, INT_MAX);
}