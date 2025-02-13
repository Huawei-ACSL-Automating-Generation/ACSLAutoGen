#include <assert.h>

/*
  * "nested2.c" from InvGen benchmark suite
  */
/*@
requires l > 0;
*/
void foo(int l) {
  int i, k, n;
  assume(l > 0);

  // invariant (1 <= k && k <= n)
  for (k = 1; k < n; k++) {
    for (i = l; i < n; i++) {

    }
    // invariant (l <= i && i <= n)
    for (i = l; i < n; i++) {
      static_assert(1 <= k);
    }
  }

}