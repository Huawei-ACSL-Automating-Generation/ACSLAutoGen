/*@
requires k >= 0;
requires b >= 0;
requires k >= b;
*/
void foo(int k, int b) {
  int x = 0;

  while (unknown()) {
    if (x < b)
    {
      x = x + 1;
    }
  }
  // post-condition
  if (x < b) {
    assert(0 <= x);
  }
}


