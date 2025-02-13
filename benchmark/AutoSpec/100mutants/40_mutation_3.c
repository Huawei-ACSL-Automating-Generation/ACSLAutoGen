#include <assert.h>
int unknown2();
int unknown1();

/*@
requires flag > 0;
*/
void foo(int flag) {
  int i, j, k;
  j = 1;
  if (flag) {
    i = 0;
  } else {
    i = 1;
  }

  // invariant (!flag || j - i == 1)
  while (unknown1()) {
    i += 2;
    if (i % 2 == 0) {
      j += 2;
    } else j++;
  }

  int b = 0;
  int a = 0;

  // invariant (!flog || a == b)
  while (unknown2()) {
    b += (j - i);
    a++;
  }
  if (flag) {
    static_assert(a == b);
  }
}
