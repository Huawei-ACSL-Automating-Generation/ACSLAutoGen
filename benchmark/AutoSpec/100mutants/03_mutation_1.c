#include <assert.h>

/*
 * "nested4.c" from InvGen benchmark suite
 */

/*@
requires a > 0;
requires b > a;
*/
void foo(int b, int a) {
  int i,k;

  assume(a>0);

  // invariant (1 <= k && k <= b)
  for (k=1; k<b; k++){
  	// invariant (l <= i && i <= b)
    for (i=a; i<b; i++) {
    }
    // invariant (l <= i && i <= b)
    for (i=a; i<b; i++) {
      static_assert(1<=i);
    }
  }

}
