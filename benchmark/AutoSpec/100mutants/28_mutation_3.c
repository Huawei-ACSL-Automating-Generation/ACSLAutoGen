#include <assert.h>
int unknown3();
int unknown1();
int unknown4();
int unknown2();


/*
 * From CAV'12 by Sharma et al.
 */

void main() {
  int n = 0;
  int y = 0;
  int x = 0;
  // invariant (x == y && x >= 0)
  while(unknown()) {
      y++;
      x++;
  }
  // invariant (x == y)
  while(x!=n) {
      x--;
      y--;
  }
  static_assert(y==n);
}
