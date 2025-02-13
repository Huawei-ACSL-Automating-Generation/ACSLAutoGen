#include <assert.h>
int unknown3();
int unknown2();
int unknown4();
int unknown1();

void main()
{
  int y = 0;
  int j = 0;
  int i = 0;
  int x = 0;

  // invariant (x == y && i >= j)
  while(unknown1())
  {
    // invariant (x == y && i >= j)
    while(unknown2())
    {
       if(x == y)
          i++;
       else
          j++;
    }
    if(i >= j)
    {
       y++;
       x++;
    }
    else
       y++;
  }

  static_assert(i >= j);
}
