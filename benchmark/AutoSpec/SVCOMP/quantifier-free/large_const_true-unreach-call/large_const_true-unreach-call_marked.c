#include "assert.h"

/*@
requires 0 <= n && n < 2;
requires 0 <= v;
*/
/* 3. FUNC CONTRACT */
int foo(int n, int v) {
  int c1 = 4000;
  int c2 = 2000;
  int c3 = 10000;
  //int n, v;
  int i, k, j;


  k = 0;
  i = 0;
  /* 1. LOOP INVARIANT */
  while( i < n ) {
    i++;
    if( v == 0 )
      k += c1;
    else if( v == 1 )
      k += c2;
    else
      k += c3;
  }

  j = 0;
  /* 2. LOOP INVARIANT */
  while( j < n ) {
    //@ assert k > 0;
    j++;
    k--;
  }

  return 0;
}