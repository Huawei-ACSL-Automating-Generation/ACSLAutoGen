/*@
  requires 0 <= x <= 10;
  requires 0 <= b <= 10;
*/
void foo(int x, int b) {
  // variable declarations
  int z1;
  int z2;
  int z3;

  // loop body
  while (unknown()) {
    x  = x + 10;
    b  = b + 10;
  }
  // post-condition
  if (b == 0) {
    assert(x != 20);
  }
}
