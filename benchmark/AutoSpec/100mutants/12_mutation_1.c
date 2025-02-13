/*@
  requires 0 <= a <= 10;
  requires 0 <= y <= 10;
*/
void foo(int a, int y) {
  // variable declarations
  int z1;
  int z2;
  int z3;

  // loop body
  while (unknown()) {
    a  = a + 10;
    y  = y + 10;
  }
  // post-condition
  if (y == 0) {
    assert(a != 20);
  }
}
