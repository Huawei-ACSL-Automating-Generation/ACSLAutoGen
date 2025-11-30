#include <assert.h>
int unknown1();
int unknown2();
int unknown3();
int unknown4();

/*
 * Based on "larg_const.c" from InvGen test suite  
 */
/*@
requires n > 0;
requires n < 10;
*/
void foo(int n) {
  int c1 = 4000;
  int c2 = 2000;
  int v;
  int i, k, j;
  assume(n > 0);
  assume(n < 10);
  k = 0;
  i = 0;
  // invarian (i == 0 || k >= 2000)
  while( i < n ) {
    i++;
    if(unknown2() % 2 == 0) 
      v = 0;
    else v = 1;
    
    if( v == 0 )
      k += c1;
    else 
      k += c2;
  }
  
  static_assert(k > n);
}

