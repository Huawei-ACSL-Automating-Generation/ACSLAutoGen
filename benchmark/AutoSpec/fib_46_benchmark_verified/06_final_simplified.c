#include <assert.h>
int unknown1();
int unknown2();
int unknown3();
int unknown4();

void main()
{


  int w = 1;
  int z = 0;
  int x = 0;
  int y = 0;

  /*@
  loop invariant x == y;
  loop invariant w == z + 1;
  loop invariant 1 <= w;
  loop invariant 0 <= x;
  loop assigns z;
  loop assigns y;
  loop assigns x;
  loop assigns w;
  */
  while(unknown1()) {
	  
    /*@
    loop invariant x == y;
    loop invariant 0 <= x;
    loop assigns y;
    loop assigns x;
    */
    while(unknown2()){
      if(w%2 == 1) x++;
      if(z%2 == 0) y++;
    }
    z = x + y;
    w = z + 1;
  }

  
  //@ assert x == y;
}
