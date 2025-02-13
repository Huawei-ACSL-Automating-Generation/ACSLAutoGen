int unknown1();
int unknown2();
int unknown3();

void foo(int k)
{
  int z = k;
  int x = 0;
  int y = 0;

  /*@
  loop invariant y <= x;
  loop invariant x == y;
  loop invariant x == y || x == y - 1;
  loop assigns z;
  loop assigns y;
  loop assigns x;
  */
  while(unknown1())
  {
    int c = 0;
    /*@
    loop invariant z == k + y;
    loop invariant z == k + y - c;
    loop invariant y <= x;
    loop invariant x == y || x == y - 1;
    loop invariant 0 <= c;
    loop assigns z;
    loop assigns y;
    loop assigns x;
    loop assigns c;
    */
    while(unknown2())
    {
      if(z==k+y-c)
      {
        x++;
        y++;
        c++;
      }else
      {
        x++;
        y--;
        c++;
      }
    }
    /*@
    loop invariant y <= x;
    loop invariant x == y;
    loop invariant x == y || x == y - 1;
    loop invariant 0 <= c;
    loop assigns z;
    loop assigns y;
    loop assigns x;
    loop assigns c;
    */
    while(unknown3())
    {
      x--;
      y--;
    }
    z=k+y;
  }
  //@ assert x==y;
}
