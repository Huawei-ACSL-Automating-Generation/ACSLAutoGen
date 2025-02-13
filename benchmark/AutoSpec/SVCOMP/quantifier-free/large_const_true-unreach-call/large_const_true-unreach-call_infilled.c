#include "assert.h"

/*@
requires 0 <= n && n < 2;
requires 0 <= v;
*/
int foo(int n, int v) {
  int c1 = 4000;
  int c2 = 2000;
  int c3 = 10000;
  //int n, v;
  int i, k, j;


  k = 0;
  i = 0;
  /*@
  >>> INFILL <<<
  loop invariant v == 0 && k == i * c1 || v == 1 && k == i * c2 || v != 0 && v != 1 && k == i * c3;
  loop invariant k >= i * (((v == 0) ? c1 : (v == 1) ? c2 : c3));
  loop invariant k == i * c1 || k == i * c2 || k == i * c3;
  loop invariant k == c1*i || k == c2*i || k == c3*i;
  loop invariant k == c1 * i || k == c2 * i || k == c3 * i;
  loop invariant j <= n || c2 * i <= k;
  loop invariant i*c2 <= k;
  loop invariant i <= n;
  loop invariant i <= n ==> k == i * c1 || k == i * c2 || k == i * c3;
  loop invariant i <= n ==> k == (i * c1) || k == (i * c2) || k == (i * c3);
  loop invariant i * c2 <= k;
  loop invariant c3 * i <= k || c2 * i <= k;
  loop invariant c3 * i <= k || 0 < k;
  loop invariant c2 * i <= k;
  loop invariant c1 * i <= k || c2 * i <= k;
  loop invariant 0 <= k;
  loop invariant 0 <= j || j <= n;
  loop invariant 0 <= j || c2 * i <= k;
  loop invariant 0 <= i;
  loop invariant 0 < k || c2 * i <= k;
  loop invariant 0 < k || c1 * i <= k;
  loop invariant (v == 0 && k == i * c1) || (v == 1 && k == i * c2) || (v != 0 && v != 1 && k == i * c3);
  loop invariant (k == i * c1 || k == i * c2 || k == i * c3);
  loop invariant ((v == 0 && k == i * c1) || (v == 1 && k == i * c2) || (v != 0 && v != 1 && k == i * c3));
  loop invariant ((v == 0 && k == c1 * i) || (v == 1 && k == c2 * i) || (v != 0 && v != 1 && k == c3 * i));
  loop assigns k;
  loop assigns j;
  loop assigns i;
  */
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
  /*@
  loop invariant j <= n;
  loop invariant j <= n || c2 * i <= k;
  loop invariant i >= n ==> 0 <= j;
  loop invariant i <= n;
  loop invariant i <= n || 0 <= j;
  loop invariant 0 <= j;
  loop invariant 0 <= j || j <= n;
  loop invariant 0 <= j || c2 * i <= k;
  loop invariant 0 <= i;
  loop assigns k;
  loop assigns j;
  loop assigns i;
  */
  while( j < n ) {
    // @ assert k > 0;
    j++;
    k--;
  }

  return 0;
}