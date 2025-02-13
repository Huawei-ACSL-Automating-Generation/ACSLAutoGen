/*@
requires y >= 127;
*/
void foo(int y) {
  int z = 36 * y;
  int c = 0;
  
  while (unknown()) {
    if (c < 36)
    {
      c = c + 1;
      z = z + 1;
    }
  }
  // post-condition
  if (z < 0) {
    if (z >= 4608)  {
      assert(c >= 36);
    }
  }
}
