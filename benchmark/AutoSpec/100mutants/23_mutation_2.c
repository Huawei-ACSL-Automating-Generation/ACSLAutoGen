#include <assert.h>

int unknown1();
int unknown2();
int unknown3();
int unknown4();

/*
 * ex49 from NECLA Static Analysis Benchmarks
 */
/*@
requires y >= 0;
*/
void foo(int y) {
  int z, x = 0;
  assume(y >= 0);

  // invariant (0 <= z && z <= y && x = z * (z + 1) / 2) 
  for (z = 0; z < y; ++z)
    x = x + z;

  static_assert(x >= 0);
}

