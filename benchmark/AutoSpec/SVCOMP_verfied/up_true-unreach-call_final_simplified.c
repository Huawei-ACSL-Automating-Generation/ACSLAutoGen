#include "assert.h"

/*@
requires n > 0;
*/
void foo(int n) {
  //int n;
  int i = 0;
  int k = 0;
  /*@
  loop invariant k == i;
  loop invariant i <= n;
  loop assigns k;
  loop assigns i;
  */
  while( i < n ) {
	  i++;
	  k++;
  }

  int j = 0;
  /*@
  loop invariant k == i - j;
  loop invariant i - j == n - j;
  loop assigns k;
  loop assigns j;
  loop assigns i;
  */
  while( j < n ) {
    //@ assert k > 0;
    j++;
    k--;
  }
}