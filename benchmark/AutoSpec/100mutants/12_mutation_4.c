/*@
  requires 0 <= x <= 10;
  requires 0 <= y <= 10;
*/
void foo(int x, int y) {
  // variable declarations
  int z2;
  int z3;
  int z1;

  // loop body
  while (unknown()) {
    y  = y + 10;
    x  = x + 10;
  }
  // post-condition
  if (y == 0) {
    assert(x != 20);
  }
}
