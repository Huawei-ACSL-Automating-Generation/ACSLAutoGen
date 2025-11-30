#include <assert.h>
int unknown1();
int unknown2();
int unknown3();
int unknown4();

/*
 * from Invgen test suite
 */
/*@
requires n > 0;
requires k > n;
*/
int foo(int n, int k) {

  int i, j;

  j = 0;
  // invariant (j <= n && j + k > n)
  while( j < n ) {
    j++;
    k--;
  } 
  static_assert(k>=0);
  return 0;
}
