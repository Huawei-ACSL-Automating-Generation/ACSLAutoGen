int unknown();

/*@
requires x > 0;
*/
void foo(int x) {
  int c = 0;
  assume(x > 0);
  
  while (unknown()) {
    if (unknown()) {
      if (c != x) {
        c = c + 1;
      }
    } else {
      if (c == x) {
        c = 1;
      }
    }
  }
  // post-condition
  if(c != x) {
    assert(c <= x);
  }
}
