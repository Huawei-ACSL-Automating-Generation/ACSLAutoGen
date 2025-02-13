/*@
  requires 0 <= x <= 10;
  requires 0 <= y <= 10;
*/
void foo(int x, int y) {
  // variable declarations
  int z1;
  int z2;
  int z3;

  // loop body
  /*@
  loop invariant y != 0 || x != 20;
  loop invariant 1;
  loop invariant 0 <= y;
  loop invariant 0 <= x;
  loop assigns z3;
  loop assigns z2;
  loop assigns z1;
  loop assigns y;
  loop assigns x;
  */
  while (unknown()) {
    x  = x + 10;
    y  = y + 10;
  }
  // post-condition
  if (y == 0) {
    //@ assert x != 20;
  }
}
