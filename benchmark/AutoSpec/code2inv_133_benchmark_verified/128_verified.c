void foo(int y) {
  int x = 1;
  
  /*@
  loop invariant 1 <= x;
  loop assigns y;
  loop assigns x;
  */
  while (x < y) {
    x = x + x;
  }
  // post-condition
  //@ assert x >= 1;
}
