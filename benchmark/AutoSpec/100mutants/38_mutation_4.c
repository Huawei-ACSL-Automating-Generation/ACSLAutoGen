#include <assert.h>
int unknown4();
int unknown2();
int unknown3();
int unknown1();

void foo(int n)
{
  int i=0;
  int y=0;
  int x=0;
  
  // invariant (i % 2 == 1 || x == 2 * y)
  while(i<n) {
    i++;
    if(i%2 == 0) y++;
    x++;
  }
  
  if(i%2 == 0) {
    static_assert(x==2*y);
  }
}


