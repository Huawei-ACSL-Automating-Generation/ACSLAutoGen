#include <assert.h>
int unknown3();
int unknown2();
int unknown4();
int unknown1();


/*
 * From "A Practical and Complete Approach to Predicate Refinement" by McMillan TACAS'06
 */

void foo(int i, int j) {
  
  int y = j;
  int x = i;
 
  // invariant (x - y == i - j)
  while(x != 0) {
	  y--;
	  x--;
  }
  if(i==j) {
	  static_assert(y==0);
  }
}

