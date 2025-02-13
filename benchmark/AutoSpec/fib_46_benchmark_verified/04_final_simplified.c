#include <assert.h>
int unknown1();
int unknown2();
int unknown3();
int unknown4();

/*
 * Taken from Gulwani PLDI'08:
 * Program Analysis as Constraint Solving
 */

void main() {
  int x, y;

  x = -50;
  /*@
  loop invariant x < 0 || 0 < y;
  loop assigns y;
  loop assigns x;
  */
  while( x < 0 ) {
	x = x + y;
	y++;
  }
  //@ assert y > 0;
}

