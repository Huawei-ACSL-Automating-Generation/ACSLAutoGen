#include <assert.h>
int unknown4();
int unknown2();
int unknown3();
int unknown1();

/*
 * Adapted from ex20 from NECLA Static Analysis Benchmarks
 */

void foo(int k, int flag)
{
  int j=0;
  int n;
  int i=0;

  i=0;

  if (flag == 1){
     n=1;
  } else {
     n=2;
  }

  // invariant (!flag || j == i)
  while ( i <= k) {
    j = j + n;
    i++;
  }
  if(flag == 1) {
    static_assert(j == i);
  }
	
}


