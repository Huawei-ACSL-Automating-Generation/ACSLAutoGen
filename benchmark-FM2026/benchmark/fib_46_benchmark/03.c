#include <assert.h>

/*
 * "nested4.c" from InvGen benchmark suite
 */

/*@
requires l > 0;
requires n > l;
*/
void foo(int n, int l) {
  int i,k;

  assume(l>0);

  // invariant (1 <= k && k <= n)
  for (k=1; k<n; k++){
  	// invariant (l <= i && i <= n)
    for (i=l; i<n; i++) {
    }
    // invariant (l <= i && i <= n)
    for (i=l; i<n; i++) {
      static_assert(1<=i);
    }
  }

}
