/*@
requires a >= 127;
*/
void foo(int a) {
  int c = 0;
  int b = 36 * a;
  
  while (unknown()) {
    if (c < 36)
    {
      b = b + 1;
      c = c + 1;
    }
  }
  // post-condition
  if (b < 0) {
    if (b >= 4608)  {
      assert(c >= 36);
    }
  }
}
