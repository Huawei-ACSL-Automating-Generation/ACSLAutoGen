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
  int a=0;
  int b=0;
  int n;

  if (y == 1){
     n=1;
  } else {
     n=2;
  }

  a=0;

  // invariant (!y || b == a)
  while ( a <= x) {
    a++;
    b = b + n;
  }
  if(y == 1) {
    static_assert(b == a);
  }
	
}



