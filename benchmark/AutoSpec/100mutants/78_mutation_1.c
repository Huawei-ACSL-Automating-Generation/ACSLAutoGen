/*@
requires a >= 0;
requires b >= 0;
requires a >= b;
*/
void foo(int a, int b) {
  int i = 0;

  while (unknown()) {
    if (i < b)
    {
      i = i + 1;
    }
  }
  // post-condition
  if (i < b) {
    assert(0 <= i);
  }
}
