#include <limits.h>
#include "assert.h"

int unknown1();

/*@
requires l > 0;
requires l < INT_MAX;
requires n < INT_MAX;
*/
void foo(int n, int l) {
  int i,k;
  //int n,l;
  n = __VERIFIER_nondet_int();
  l = __VERIFIER_nondet_int();
  __VERIFIER_assume(l > 0);
  __VERIFIER_assume(l < INT_MAX);
  __VERIFIER_assume(n < INT_MAX);
  for (k = 1; k < n; k++){
    // invariant (l > 0)
    for (i = l; i < n; i++){  
      __VERIFIER_assert(1 <= i);
    }
    if(unknown1()) {
      l = l + 1;
    }
  }
 }