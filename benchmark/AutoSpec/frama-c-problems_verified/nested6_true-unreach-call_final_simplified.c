#include <limits.h>
#include "assert.h"

int unknown1();

/*@
requires n < INT_MAX;
*/
int foo(int k, int n) {
    int i,j;
    //int k,n;
    if( k == n) {
    }
    else {
        goto END;
    }

    /*@
    loop invariant n <= INT_MAX;
    loop invariant n < INT_MAX;
    loop invariant k >= n || k < n;
    loop invariant k == n;
    loop invariant k <= n;
    loop invariant k < n || k >= n;
    loop invariant \forall integer x; j <= x < n ==> k <= n;
    loop invariant \forall integer x; i <= x < n ==> k >= x;
    loop invariant \forall integer x; i <= x < n ==> k <= n;
    loop invariant \forall integer x, y; 0 <= x < i && 2*x <= y < n ==> k >= 2*x;
    loop invariant \forall integer x, y; 0 <= x < i && 2*x <= y < n ==> k <= n;
    loop invariant \forall integer m; 2*i <= m < j ==> k <= n;
    loop invariant 0 <= i;
    loop assigns n;
    loop assigns k;
    loop assigns j;
    loop assigns i;
    */
    for (i = 0; i < n; i++) {
        /*@
        loop invariant n <= k;
        loop invariant n - i;
        loop invariant k <= n;
        loop invariant k < n || k >= n;
        loop invariant i <= n;
        loop invariant i <= k;
        loop invariant i <= j;
        loop invariant i < n;
        loop invariant \forall integer x; j <= x < n ==> k >= x;
        loop invariant \forall integer x; i <= x < n ==> k <= n;
        loop invariant \forall integer x, y; 0 <= x < i && 2*x <= y < n ==> k >= 2*x;
        loop invariant \forall integer x, y; 0 <= x < i && 2*x <= y < n ==> k <= n;
        loop invariant \forall integer m; j <= m < n ==> k >= m;
        loop invariant \forall integer m; i <= m < n ==> k >= m;
        loop invariant \forall integer m; 2*i <= m < j ==> k >= m;
        loop invariant \forall integer m; 2*i <= m < j ==> k <= n;
        loop invariant 2*i <= j;
        loop invariant 2 * i <= j;
        loop invariant 0 <= k;
        loop invariant 0 <= j;
        loop invariant 0 <= i;
        loop assigns k;
        loop assigns j;
        loop assigns i;
        */
        for (j= 2 * i; j < n; j++) {
            if(unknown1()) {
                /*@
                loop invariant n <= k;
                loop invariant n < INT_MAX;
                loop invariant n - j;
                loop invariant n - i;
                loop invariant k <= n;
                loop invariant j <= n;
                loop invariant j <= k;
                loop invariant j < n;
                loop invariant i <= n;
                loop invariant i <= k;
                loop invariant i <= j;
                loop invariant i < n;
                loop invariant \forall integer x; j <= x < n ==> k >= x;
                loop invariant \forall integer x; i <= x < n ==> k >= x;
                loop invariant \forall integer x; i <= x < n ==> k <= n;
                loop invariant \forall integer x, y; 0 <= x < i && 2*x <= y < n ==> k >= 2*x;
                loop invariant \forall integer x, y; 0 <= x < i && 2*x <= y < n ==> k <= n;
                loop invariant \forall integer m; j <= m < n ==> k >= m;
                loop invariant \forall integer m; i <= m < n ==> k >= m;
                loop invariant \forall integer m; 2*i <= m < j ==> k >= m;
                loop invariant \forall integer m; 2*i <= m < j ==> k <= n;
                loop invariant 2*i <= k;
                loop invariant 2*i <= j;
                loop invariant 2 * i <= k;
                loop invariant 2 * i <= j;
                loop invariant 0 <= k;
                loop invariant 0 <= j;
                loop invariant 0 <= i;
                loop assigns k;
                */
                for (k = j; k < n; k++) {
                    //@ assert k >= 2*i;
                }
            }
            else {
                //@ assert  k >= n ;
                //@ assert  k <= n ;
            }
        }
    }
END:
    return 0;
}