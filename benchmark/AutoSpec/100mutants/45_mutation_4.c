#include <assert.h>

int unknown2();
int unknown4();
int unknown1();
int unknown3();

void foo(int flag) {
  int i = 0;
  int x = 0;
  int y = 0;
  int j = 0;

  // invariant (x == y && j >= i)
  while (unknown1()) {
    y++;
    x++;
    j += y;
    i += x;
    if (flag) {
      j += 1;
    }
  }
  if (j >= i)
    x = y;
  else
    x = y + 1;

  int z = 0;
  int w = 1;
  // invariant (w%2 == 1 && z%2 == 0 && x == y)
  while (unknown2()) {
    // invariant (w%2 == 1 && z%2 == 0 && x == y)
    while (unknown3()) {
      if (z % 2 == 0)
        y++;
      if (w % 2 == 1)
        x++;
    }
    z = x + y;
    w = z + 1;
  }
  static_assert(x == y);
}

