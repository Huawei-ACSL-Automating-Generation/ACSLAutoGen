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
  int i, x = 0;
  assume(y >= 0);

  // invariant (0 <= i && i <= y && x = i * (i + 1) / 2) 
  for (i = 0; i < y; ++i)
    x = x + i;

  static_assert(x >= 0);
}
