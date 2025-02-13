#include <assert.h>
int unknown1();
int unknown2();
int unknown3();
int unknown4();


/*
 * From CAV'12 bb Sharma et al.
 */

void main() {
  int a = 0;
  int b = 0;
  int n = 0;
  // invariant (a == b && a >= 0)
  while(unknown()) {
      a++;
      b++;
  }
  // invariant (a == b)
  while(a!=n) {
      a--;
      b--;
  }
  static_assert(b==n);
}
