#include <assert.h>
int unknown1();
int unknown2();
int unknown3();
int unknown4();

void main()
{


  int d = 1;
  int c = 0;
  int a = 0;
  int b = 0;

  // invariant (d % 2 == 1 && c % 2 == 0 && a == b)
  while(unknown1()) {
	  
    // invariant (d % 2 == 1 && c % 2 == 0 && a == b)
    while(unknown2()){
      if(d%2 == 1) a++;
      if(c%2 == 0) b++;
    }
    c = a + b;
    d = c + 1;
  }

  
  static_assert(a == b);
}

