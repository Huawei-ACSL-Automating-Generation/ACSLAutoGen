#include <limits.h>
#include "assert.h"

int unknown1();

/*@
requires n < INT_MAX;
*/
int foo(int k, int n) {
    int i,j;
    //int k,n;
    k = __VERIFIER_nondet_int();
    n = __VERIFIER_nondet_int();
    __VERIFIER_assume(n < INT_MAX);
    if( k == n) {
    }
    else {
        goto END;
    }

    for (i = 0; i < n; i++) {
        // invariant (k == n)
        for (j= 2 * i; j < n; j++) {
            if(unknown1()) {
                for (k = j; k < n; k++) {
                    __VERIFIER_assert(k >= 2*i);
                }
            }
            else {
                __VERIFIER_assert( k >= n );
                __VERIFIER_assert( k <= n );
            }
        }
    }
END:
    return 0;
}