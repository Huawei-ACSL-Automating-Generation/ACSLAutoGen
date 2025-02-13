int unknown1();
int unknown2();
int unknown3();

void main() {
  int a = 1;
  int b = 1;
  int c = 2;
  int d = 2;
  int x = 3;
  int y = 3;
  /*@
  loop invariant c <= 2;
  loop invariant b <= 1;
  loop invariant a + c == b + d;
  loop invariant 0 <= d;
  loop invariant 0 <= a;
  loop assigns y;
  loop assigns x;
  loop assigns d;
  loop assigns c;
  loop assigns b;
  loop assigns a;
  */
  while (unknown1()) {
    x = a + c;
    y = b + d;
    if ((x + y) % 2 == 0) {
      a++;
      d++;
    } else {
      a--;
    }

    /*@
    loop invariant c <= 2;
    loop invariant b <= 1;
    loop invariant a + c == b + d;
    loop invariant 0 <= d;
    loop invariant 0 <= a;
    loop assigns y;
    loop assigns x;
    loop assigns d;
    loop assigns c;
    loop assigns b;
    loop assigns a;
    */
    while (unknown2()) {
      c--;
      b--;
    }
  }
  //@ assert a + c == b + d;
}