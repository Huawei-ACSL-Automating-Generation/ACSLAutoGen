#include <assert.h>
int unknown1();
int unknown2();
int unknown3();
int unknown4();

void main()
{
  int x = 0;
  int y = 0;
  int i = 0;
  int j = 0;

  // invariant (x == y && i >= j)
  while(unknown1())
  {
    // invariant (x == y && i >= j)
    while(unknown2())
    {
       if(x != y)
          j++;
       else
          i++;
    }
    if(i < j)
       y++;
	else {
       x++;
       y++;
	}
  }

  static_assert(j <= i);
}

