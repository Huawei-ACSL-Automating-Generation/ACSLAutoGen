#include <assert.h>
int unknown1();
int unknown2();
int unknown3();
int unknown4();

/*
 * Adapted from ex20 from NECLA Static Analysis Benchmarks
 */

void foo(int k, int flag)
{
  int i=0;
  int j=0;
  int n;

  if (flag != 1){
     n=2;
  } else {
     n=1;
  }

  i=0;

  // invariant (!flag || j == i)
  while ( !(i > k)) {
    i++;
    j = j + n;
  }
  if(!(flag != 1)) {
    static_assert(j == i);
  }
	
}



