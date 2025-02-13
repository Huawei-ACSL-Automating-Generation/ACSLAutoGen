void foo(int x) {
  int i = 0;
  int j = 0;
  int y = 2;
  
  /*@
  loop invariant y == 2;
  loop invariant j == i*2;
  loop invariant j == i * 2;
  loop invariant j == 2*i;
  loop invariant 0 <= j;
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
  if (y == 1) {
    //@ assert i == j;
  }
}