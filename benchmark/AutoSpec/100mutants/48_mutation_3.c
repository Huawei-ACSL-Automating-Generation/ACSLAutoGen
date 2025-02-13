int unknown();

/*@
requires n > 0;
*/
void foo(int n) {
  assume(n > 0);
  int c = 0;
  
  while (unknown()) {
      if(unknown()) {
        if(c != n) {
        (c = (c + 1));
        }
      } else {
        if (c == n) {
          c = 1;
        }
      }
  }
  // post-condition
  if(c == n) {
    assert(n > -1);
  }
}
