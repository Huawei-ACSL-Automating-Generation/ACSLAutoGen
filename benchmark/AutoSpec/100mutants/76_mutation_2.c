/*@
requires y >= 127;
*/
void foo(int y) {
  int x = 0;
  int q = 36 * y;
  
  while (unknown()) {
    if (x < 36)
    {
      q = q + 1;
      x = x + 1;
    }
  }
  // post-condition
  if (q < 0) {
    if (q >= 4608)  {
      assert(x >= 36);
    }
  }
}
