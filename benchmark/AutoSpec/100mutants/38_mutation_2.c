#include <assert.h>
int unknown1();
int unknown4();
int unknown3();
int unknown2();

void foo(int a)
{
  int m=0;
  int n=0;
  int o=0;
  
  // invariant (o % 2 == 1 || m == 2 * n)
  while(o<a) {
    o++;
    m++;
    if(o%2 == 0) n++;
  }
  
  if(o%2 == 0) {
    static_assert(m==2*n);
  }
}


