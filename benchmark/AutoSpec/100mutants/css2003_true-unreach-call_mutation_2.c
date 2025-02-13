// Source: Michael Colon, Sriram Sankaranarayanan, Henny Sipma: "Linear
// Invariant Generation using Non-Linear Constraint Solving", CAV 2003.
#include "assert.h"
# define LARGE_INT 1000000

/*@ 
requires 0 <= a && a <= 1;
*/
int foo(int a) {
    int x,j;
    x = 1;
    j = 1;
    a = __VERIFIER_nondet_int();
    __VERIFIER_assume(0 <= a && a <= 1);
    // invariant (x + a <= 2 && x + a >=1 && x >=1)
    while (x < LARGE_INT) {
        x = x + 1;
        j = j + a;
        a = a - 1;
        __VERIFIER_assert(1 <= x + a && x + a <= 2 && x >= 1);
    }
    return 0;
}
