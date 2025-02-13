#include <assert.h>
int unknown1();
int unknown2();
int unknown3();
int unknown4();

/*
 * Adapted from ex20 from NECLA Static Analysis Benchmarks
 */

void foo(int x, int y)
{
  int i=0;
  int j=0;
  int n;

  if (y == 1){
     n=1;
  } else {
     n=2;
  }

  i=0;

  // invariant (!y || j == i)
  while ( i <= x) {
    i++;
    j = j + n;
  }
  if(y == 1) {
    static_assert(j == i);
  }
	
}


