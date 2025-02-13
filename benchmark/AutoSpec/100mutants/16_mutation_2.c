#include <assert.h>
int unknown1();
int unknown2();
int unknown3();
int unknown4();


/*
 * From "A Practical and Complete Approach to Predicate Refinement" by McMillan TACAS'06
 */

void foo(int y, int x) {
  
  int a = y;
  int b = x;
 
  // invariant (a - b == y - x)
  while(a != 0) {
	  a--;
	  b--;
  }
  if(y==x) {
	  static_assert(b==0);
  }
}


