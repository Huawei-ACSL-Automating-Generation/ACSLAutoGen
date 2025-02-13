#include <assert.h>
int unknown1();
int unknown2();
int unknown3();
int unknown4();

/*
 * From "The Octagon Abstract Domain" HOSC 2006 by Mine.
 */

int main() {
  int a = 0;
  int j;
  int m;
  int __BLAST_NONDET;
  if(m<=0)
    return 0;
  /*@
  loop invariant a <= m;
  loop invariant a <= j - 1;
  loop invariant -m <= a;
  loop invariant -j + 1 <= a;
  loop assigns j;
  loop assigns a;
  loop assigns __BLAST_NONDET;
  */
  for(j = 1; j <= m ; j++){
    if(unknown1()) 
       a++;
    else
       a--; 
  }
  //@ assert a>=-m;
  //@ assert a<=m;
}
