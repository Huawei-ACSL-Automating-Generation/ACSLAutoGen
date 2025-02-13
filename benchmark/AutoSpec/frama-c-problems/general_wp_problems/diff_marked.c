#include <limits.h>

/* 1. FUNC CONTRACT */
int diff (int x, int y) {
    return x-y;
}

/* 2. FUNC CONTRACT */
void main() {
    int t = diff(10, 5);
    //@ assert t == 5;
}