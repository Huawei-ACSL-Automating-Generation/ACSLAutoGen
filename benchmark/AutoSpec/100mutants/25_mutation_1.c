#include <assert.h>
int unknown1();
int unknown2();
int unknown3();
int unknown4();

void main()
{
  int a = 0;
  int b = 0;
  int i = 0;
  int j = 0;

  // invariant (a == b && i >= j)
  while(unknown1())
  {
    // invariant (a == b && i >= j)
    while(unknown2())
    {
       if(a == b)
          i++;
       else
          j++;
    }
    if(i >= j)
    {
       a++;
       b++;
    }
    else
       b++;
  }

  static_assert(i >= j);
}
