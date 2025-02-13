// Source: Isil Dillig, Thomas Dillig, Boyang Li, Ken McMillan: "Inductive
// Invariant Generation via Abductive Inference", OOPSLA 2013.
#include "assert.h"
int unknown1();

int foo(int flag) {
    unsigned int i,j,a,b;
    a = 0;
    b = 0;
    j = 1;
    if (flag) {
        i = 0;
    } else {
        i = 1;
    }

    /*@
    >>> INFILL <<<
    loop invariant j%2 <= 1;
    loop invariant j%2 <= 1 || j <= 3;
    loop invariant j%2 <= 1 || i%2 <= 1;
    loop invariant j%2 <= 1 || i <= 2;
    loop invariant j%2 <= 1 || a <= b;
    loop invariant j%2 <= 1 || 0 <= j%2;
    loop invariant j%2 <= 1 || 0 <= i%2;
    loop invariant j <= 3 || 0 <= j;
    loop invariant j % 2 == 0 || j % 2 == 1;
    loop invariant i%2 == 0 || i%2 == 1;
    loop invariant i%2 <= 1;
    loop invariant i%2 <= 1 || j <= 3;
    loop invariant i%2 <= 1 || 0 <= j%2;
    loop invariant i <= 2 || i%2 <= 1;
    loop invariant i <= 2 || 0 <= j%2;
    loop invariant i <= 2 || 0 <= i;
    loop invariant i <= 2 || 0 <= i%2;
    loop invariant i % 2 == 1 ==> j % 2 == 0 || j % 2 == 1;
    loop invariant i % 2 == 0 || i % 2 == 1;
    loop invariant a <= b || i%2 <= 1;
    loop invariant a <= b || 0 <= j%2;
    loop invariant a <= b || 0 <= i%2;
    loop invariant 0 <= j;
    loop invariant 0 <= j%2;
    loop invariant 0 <= j%2 || j <= 3;
    loop invariant 0 <= j || j <= 3;
    loop invariant 0 <= j || i <= 2;
    loop invariant 0 <= j || 0 <= i;
    loop invariant 0 <= j <= 3 || 0 <= j;
    loop invariant 0 <= j <= 3 || 0 <= i;
    loop invariant 0 <= i|| j <= 3;
    loop invariant 0 <= i;
    loop invariant 0 <= i%2;
    loop invariant 0 <= i%2 || j <= 3;
    loop invariant 0 <= i%2 || i%2 <= 1;
    loop invariant 0 <= i%2 || 0 <= j%2;
    loop invariant 0 <= i || j <= 3;
    loop invariant 0 <= i <= 2 || 0 <= i;
    loop invariant 0 <= b;
    loop invariant 0 <= a;
    loop assigns j;
    loop assigns i;
    loop assigns b;
    loop assigns a;
    */
    while (unknown1()) {
        a++;
        b += (j - i);
        i += 2;
        if (i%2 == 0) {
            j += 2;
        } else {
            j++;
        }
    }
    if (flag) {
        // @ assert a == b;
    }
    return 0;
}