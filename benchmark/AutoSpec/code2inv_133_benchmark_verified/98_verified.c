void foo(int x) {
  int i = 0;
  int j = 0;
  int y = 2;

  /*@
  loop invariant y == 2;
  loop invariant j == y*i;
  loop invariant j == i*y;
  loop invariant j == 2*i;
  loop invariant 0 <= i;
  loop assigns y;
  loop assigns j;
  loop assigns i;
  */
  while (i <= x) {
    i  = i + 1;
    j  = j + y;
  }
  // post-condition
  if (i != j) {
    //@ assert y != 1;
  }
}
