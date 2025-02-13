#include <assert.h>
int unknown1();
int unknown2();
int unknown3();
int unknown4();


/*
 * From "A Practical and Complete Approach to Predicate Refinement" by McMillan TACAS'06
 */

void foo(int i, int j) {
  
  int a = i;
  int b = j;
 
  // invariant (a - b == i - j)
  while(a != 0) {
	  a--;
	  b--;
  }
  if(i==j) {
	  static_assert(b==0);
  }
}

