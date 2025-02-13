#include<assert.h>

int unknown1();
int unknown2();
int unknown3();
int unknown4();

/*@
requires (x + y) == k;
requires n > 0;
*/
void foo(int x, int y, int k, int j, int i, int n) {
  int m = 0;
  j = 0;
  /*@
  loop invariant m <= n-1;
  loop invariant 0 <= m;
  loop invariant 0 <= j;
  loop invariant (x + y) == k;
  loop assigns y;
  loop assigns x;
  loop assigns m;
  loop assigns j;
  loop assigns i;
  */
  while (j < n) {
    if (j == i) {
      x++;
      y--;
    } else {
      y++;
      x--;
    }
    if (unknown1())
      m = j;
    j++;
  }
  //@ assert (x + y) == k;
  
  if (n > 0) {
    //@ assert 0 <= m;
    //@ assert m < n;
  }

}