int unknown();

/*@
requires x > 0;
*/
void foo(int x) {
  int y = 0;
  assume(x > 0);
  
  while (unknown()) {
    if (unknown()) {
      if (y != x) {
        y = y + 1;
      }
    } else {
      if (y == x) {
        y = 1;
      }
    }
  }
  // post-condition
  if(y != x) {
    assert(y <= x);
  }
}

