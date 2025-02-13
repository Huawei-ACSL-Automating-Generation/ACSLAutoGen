void main() {
  // variable declarations
  int x;
  int y;
  // pre-conditions
  x = 1;
  y = 0;
  // loop body
  /*@
  loop invariant y <= x;
  loop invariant y <= 100000;
  loop invariant 1 <= x;
  loop invariant 0 <= y;
  loop assigns y;
  loop assigns x;
  */
  while (y < 100000) {
    x  = x + y;
    y  = y + 1;
  }
  // post-condition
  //@ assert  (x >= y) ;
}
