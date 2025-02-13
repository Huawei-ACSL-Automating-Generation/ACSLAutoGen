int unknown1();
int unknown2();


int main(int flag)
{
  int t = 0;
  int s = 0;
  int a = 0;
  int b = 0;
  /*@
  loop invariant t <= 2*s;
  loop invariant a == b;
  loop invariant 0 <= a;
  loop assigns t;
  loop assigns s;
  loop assigns b;
  loop assigns a;
  */
  while(unknown1()) {
    a++;
    b++;
    s += a;
    t += b;
    if(flag) {
      t += a;
    }
  } 
  //2s >= t
  int x = 1;
  if(flag) {
    x = t - 2*s + 2;
  }
  //x <= 2
  int y = 0;
  /*@
  loop invariant y <= 4;
  loop assigns y;
  loop assigns t;
  loop assigns s;
  loop assigns b;
  loop assigns a;
  */
  while(y <= x){
    if(unknown2()) 
       y++;
    else 
       y += 2;
  }
  //@ assert y <= 4;
}

