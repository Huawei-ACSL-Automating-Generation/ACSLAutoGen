#include <assert.h>

/*
 * "nested4.c" from InvGen benchmark suite
 */

/*@
requires a > 0;
requires b > a;
*/
void foo(int b, int a) {
  int x,y;

  assume(a>0);

  // invariant (1 <= y && y <= b)
  for (y=1; y<b; y++){
  	// invariant (l <= x && x <= b)
    for (x=a; x<b; x++) {
    }
    // invariant (l <= x && x <= b)
    for (x=a; x<b; x++) {
      static_assert(1<=x);
    }
  }

}

