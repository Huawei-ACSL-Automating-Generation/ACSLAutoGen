#include <assert.h>

int unknown1();
int unknown2();
int unknown3();
int unknown4();

void foo(int p) {
  int a = 0;
  int b = 0;
  int j = 0;
  int i = 0;

  // invariant (a == b && j >= i)
  while (unknown1()) {
    a++;
    b++;
    i += a;
    j += b;
    if (p) {
      j += 1;
    }
  }
  if (j >= i)
    a = b;
  else
    a = b + 1;

  int w = 1;
  int z = 0;
  // invariant (w%2 == 1 && z%2 == 0 && a == b)
  while (unknown2()) {
    // invariant (w%2 == 1 && z%2 == 0 && a == b)
    while (unknown3()) {
      if (w % 2 == 1)
        a++;
      if (z % 2 == 0)
        b++;
    }
    z = a + b;
    w = z + 1;
  }
  static_assert(a == b);
}
