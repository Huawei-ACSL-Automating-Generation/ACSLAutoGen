// Source: Michael Colon, Sriram Sankaranarayanan, Henny Sipma: "Linear
// Invariant Generation using Non-Linear Constraint Solving", CAV 2003.
#include "assert.h"
# define LARGE_INT 1000000

/*@ 
requires 0 <= a && a <= 1;
*/
int foo(int a) {
    int i,j;
    i = 1;
    j = 1;
    a = __VERIFIER_nondet_int();
    __VERIFIER_assume(0 <= a && a <= 1);
    // invariant (i + a <= 2 && i + a >=1 && i >=1)
    while (i < LARGE_INT) {
        i = i + 1;
        j = j + a;
        a = a - 1;
        __VERIFIER_assert(1 <= i + a && i + a <= 2 && i >= 1);
    }
    return 0;
}
