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

  /*@
  loop invariant x == y;
  loop invariant i >= j || (x == y && i == j);
  loop invariant 0 <= j;
  loop assigns y;
  loop assigns x;
  loop assigns j;
  loop assigns i;
  */
  while(unknown1())
  {
    /*@
    loop invariant j <= i;
    loop invariant 0 <= j;
    loop assigns j;
    loop assigns i;
    */
    while(unknown2())
    {
       if(x == y)
          i++;
       else
          j++;
    }
    if(i >= j)
    {
       x++;
       y++;
    }
    else
       y++;
  }

  //@ assert i >= j;
}
