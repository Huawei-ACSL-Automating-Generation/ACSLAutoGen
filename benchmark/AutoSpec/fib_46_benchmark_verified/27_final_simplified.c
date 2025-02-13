#include <assert.h>

/*
  * "nested2.c" from InvGen benchmark suite
  */
/*@
requires l > 0;
*/
void foo(int l) {
  int i, k, n;

  /*@
  loop invariant 1 <= k;
  loop assigns k;
  loop assigns i;
  */
  for (k = 1; k < n; k++) {
    /*@
    loop assigns i;
    */
    for (i = l; i < n; i++) {

    }
    /*@
    loop invariant 1 <= k;
    loop assigns k;
    loop assigns i;
    */
    for (i = l; i < n; i++) {
      //@ assert 1 <= k;
    }
  }

}