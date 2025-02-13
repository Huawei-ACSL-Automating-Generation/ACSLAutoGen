void foo(int x) {
  int lock = 0;
  int y = x + 1;
  
  /*@
  loop invariant x <= y;
  loop invariant x != y ==> lock == 0;
  loop invariant lock == 0 ==> x + 1 == y;
  loop invariant x == y || x == y - 1;
  loop invariant x == y || x + 1 == y;
  loop invariant lock == 1 ==> x == y;
  loop invariant lock == 0 || lock == 1;
  loop invariant lock <= 1;
  loop invariant 0 <= lock;
  loop assigns y;
  loop assigns x;
  loop assigns lock;
  */
  while (x != y) {
    if (unknown()) {
      lock = 1;
      x  = y;
    } else {
      lock = 0;
      x  = y;
      y  = y + 1;
    }
  }
  // post-condition
  //@ assert lock == 1;
}
