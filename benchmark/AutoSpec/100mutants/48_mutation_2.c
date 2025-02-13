int unknown();

/*@
requires a > 0;
*/
void foo(int a) {
  int x = 0;
  assume(a > 0);
  
  while (unknown()) {
      if(unknown()) {
        if(x != a) {
        (x = (x + 1));
        }
      } else {
        if (x == a) {
          x = 1;
        }
      }
  }
  // post-condition
  if(x == a) {
    assert(a > -1);
  }
}
