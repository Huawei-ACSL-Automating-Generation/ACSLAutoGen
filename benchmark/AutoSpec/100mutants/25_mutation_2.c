#include <assert.h>
int unknown1();
int unknown2();
int unknown3();
int unknown4();

void main()
{
  int a = 0;
  int b = 0;
  int x = 0;
  int y = 0;

  // invariant (a == b && x >= y)
  while(unknown1())
  {
    // invariant (a == b && x >= y)
    while(unknown2())
    {
       if(a == b)
          x++;
       else
          y++;
    }
    if(x >= y)
    {
       a++;
       b++;
    }
    else
       b++;
  }

  static_assert(x >= y);
}

