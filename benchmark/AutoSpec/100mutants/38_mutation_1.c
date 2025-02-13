#include <assert.h>
int unknown1();
int unknown4();
int unknown3();
int unknown2();

void foo(int a)
{
  int x=0;
  int y=0;
  int i=0;
  
  // invariant (i % 2 == 1 || x == 2 * y)
  while(i<a) {
    i++;
    x++;
    if(i%2 == 0) y++;
  }
  
  if(i%2 == 0) {
    static_assert(x==2*y);
  }
}

