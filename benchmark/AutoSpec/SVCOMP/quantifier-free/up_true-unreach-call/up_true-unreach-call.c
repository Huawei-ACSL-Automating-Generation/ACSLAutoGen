#include "assert.h"

/*@
requires n > 0;
*/
void foo(int n) {
  //int n;
  int i = 0;
  int k = 0;
  n = __VERIFIER_nondet_int();
  // invariant (i == k && i <= n)
  while( i < n ) {
	  i++;
	  k++;
  }

  int j = 0;
  // invariant (j + k == n)
  while( j < n ) {
    __VERIFIER_assert (k > 0);
    j++;
    k--;
  }
}