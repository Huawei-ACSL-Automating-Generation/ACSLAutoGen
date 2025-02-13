#include <assert.h>

int unknown1();
int unknown2();
int unknown3();
int unknown4();

void foo(int flag) {
  int x = 0;
  int y = 0;
  int j = 0;
  int i = 0;

  /*@
  loop invariant z <= x+y;
  loop invariant z <= x + y;
  loop invariant z % 2 == 0 ==> x == y;
  loop invariant y <= x;
  loop invariant x == y;
  loop invariant x <= y;
  loop invariant x <= y + 1;
  loop invariant w == z + 1;
  loop invariant w <= z+1;
  loop invariant w <= z + 1;
  loop invariant j >= i ==> x == y;
  loop invariant j < i ==> x == y + 1;
  loop invariant i == x*(x+1)/2;
  loop invariant 1 <= w;
  loop invariant 0 <= z;
  loop invariant 0 <= y;
  loop invariant 0 <= x;
  loop invariant 0 <= w;
  loop invariant 0 <= j;
  loop invariant 0 <= i;
  loop assigns z;
  loop assigns y;
  loop assigns x;
  loop assigns w;
  loop assigns j;
  loop assigns i;
  */
  while (unknown1()) {
    x++;
    y++;
    i += x;
    j += y;
    if (flag) {
      j += 1;
    }
  }
  if (j >= i)
    x = y;
  else
    x = y + 1;

  int w = 1;
  int z = 0;
  /*@
  loop invariant y <= x;
  loop invariant x == y;
  loop invariant x <= y;
  loop invariant x <= y + 1;
  loop invariant j >= i ==> x == y;
  loop invariant j < i ==> x == y + 1;
  loop invariant 0 <= y;
  loop invariant 0 <= x;
  loop invariant 0 <= j;
  loop invariant 0 <= i;
  loop assigns y;
  loop assigns x;
  loop assigns j;
  loop assigns i;
  */
  while (unknown2()) {
    /*@
    loop invariant z <= x+y;
    loop invariant z <= x + y;
    loop invariant z % 2 == 0 ==> x == y;
    loop invariant y <= x;
    loop invariant x == y;
    loop invariant x <= y;
    loop invariant x <= y + 1;
    loop invariant w == z + 1;
    loop invariant w <= z+1;
    loop invariant w <= z + 1;
    loop invariant j >= i ==> x == y;
    loop invariant j < i ==> x == y + 1;
    loop invariant 1 <= w;
    loop invariant 0 <= z;
    loop invariant 0 <= y;
    loop invariant 0 <= x;
    loop invariant 0 <= w;
    loop invariant 0 <= j;
    loop invariant 0 <= i;
    loop assigns z;
    loop assigns y;
    loop assigns x;
    loop assigns w;
    loop assigns j;
    loop assigns i;
    */
    while (unknown3()) {
      if (w % 2 == 1)
        x++;
      if (z % 2 == 0)
        y++;
    }
    z = x + y;
    w = z + 1;
  }
  //@ assert x == y;
}