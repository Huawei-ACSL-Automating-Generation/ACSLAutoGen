#include <assert.h>
int unknown1();

/*
 * "nest-if8" from InvGen benchmark suite
 */

void main() {
  int i, j, k, n, m;
  if (m + 1 < n);
  else return;
  /*@
  loop invariant m + 1 < n;
  loop invariant i % 4 == 0;
  loop invariant 0 <= i;
  loop assigns n;
  loop assigns m;
  loop assigns k;
  loop assigns j;
  loop assigns i;
  */
  for (i = 0; i < n; i += 4) {
    /*@
    loop invariant i <= j;
    loop assigns k;
    loop assigns j;
    */
    for (j = i; j < m;) {
      if (unknown1()) {
        //@ assert j >= 0;
        j++;
        k = 0;
        /*@
        loop assigns k;
        */
        while (k < j) {
          k++;
        }
      } else {
        //@ assert n + j + 5 > i;
        j += 2;
      }
    }
  }
}