#include <limits.h>
#include "assert.h"

int __BLAST_NONDET;

/*@
requires INT_MIN < n && n < INT_MAX;
requires INT_MIN < m && m < INT_MAX;
requires INT_MIN < l && l < INT_MAX;
*/
int foo(int n, int m , int l) {
    int i,j,k;
    //int n,l,m;
    n = __VERIFIER_nondet_int();
    m = __VERIFIER_nondet_int();
    l = __VERIFIER_nondet_int();
    __VERIFIER_assume(INT_MIN < n && n < INT_MAX);
    __VERIFIER_assume(INT_MIN < m && m < INT_MAX);
    __VERIFIER_assume(INT_MIN < l && l < INT_MAX);
    if(3*n<=m+l); else goto END;
    // invariant ( i <= n)
    for (i=0;i<n;i++)
        for (j = 2*i;j<3*i;j++)
            // invariant (i <= k <= 3*i)
            for (k = i; k< j; k++) {
                __VERIFIER_assert( k-i <= 2*n );
            }
END:
    return 0;
}