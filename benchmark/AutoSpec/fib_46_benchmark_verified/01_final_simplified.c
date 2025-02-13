#include <assert.h>
int unknown1();
int unknown2();
int unknown3();
int unknown4();

/*
 * IC3 motivating example
 */ 

void main()
{
 int x = 1;
 int y = 1;
 /*@
 loop invariant 1 <= y;
 loop invariant 1 <= x;
 loop assigns y;
 loop assigns x;
 */
 while(unknown1()) {
   int t1 = x;
   int t2 = y;
   x = t1 + t2;
   y = t1 + t2;
 }
 //@ assert y>=1;
}

