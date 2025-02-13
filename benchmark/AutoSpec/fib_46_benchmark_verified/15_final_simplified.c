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
  /*@
  loop invariant k == \at(k, LoopEntry) - j;
  loop invariant 0 <= k;
  loop assigns k;
  loop assigns j;
  */
  while( j < n ) {
    j++;
    k--;
  } 
  //@ assert k>=0;
  return 0;
}
