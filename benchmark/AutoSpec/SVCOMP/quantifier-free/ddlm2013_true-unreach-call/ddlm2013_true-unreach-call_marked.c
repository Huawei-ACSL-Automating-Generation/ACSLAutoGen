// Source: Isil Dillig, Thomas Dillig, Boyang Li, Ken McMillan: "Inductive
// Invariant Generation via Abductive Inference", OOPSLA 2013.
#include "assert.h"
int unknown1();

/* 2. FUNC CONTRACT */
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

    /* 1. LOOP INVARIANT */
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
        //@ assert a == b;
    }
    return 0;
}