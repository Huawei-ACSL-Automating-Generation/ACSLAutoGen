void foo(int x, int y) {
  int i = x;
  int j = y;

  /*@
  loop invariant y <= j;
  loop invariant x <= i;
  loop invariant j - y == i - x;
  loop invariant i - x == j - y;
  loop assigns y;
  loop assigns x;
  loop assigns j;
  loop assigns i;
  */
  while (x != 0) {
    x  = x - 1;
    y  = y - 1;
  }
  // post-condition
  if (y != 0) {
    //@ assert i != j;
  }
}
