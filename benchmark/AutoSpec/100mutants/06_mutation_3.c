#include <assert.h>
int unknown1();
int unknown2();
int unknown3();
int unknown4();

void main()
{


  int x = 0;
  int z = 0;
  int y = 0;
  int w = 1;

  // invariant (w % 2 == 1 && z % 2 == 0 && x == y)
  while(unknown1()) {
	  
    // invariant (w % 2 == 1 && z % 2 == 0 && x == y)
    while(unknown2()){
      if(z%2 == 0) y++;
      if(w%2 == 1) x++;
    }
    z = x + y;
    w = z + 1;
  }

  
  static_assert(x == y);
}
