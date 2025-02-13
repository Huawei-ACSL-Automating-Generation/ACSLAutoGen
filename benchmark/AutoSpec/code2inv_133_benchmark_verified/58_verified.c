/*@
requires n > 0;
*/
void foo(int n) {
  int c = 0;
  
  /*@
  loop invariant 0 <= c;
  loop assigns n;
  loop assigns c;
  */
  while (unknown()) {
    if (unknown()) {
      if (c != n) {
        c = c + 1;
      }
    } else {
      if (c == n) {
        c = 1;
      }
    }
  }
  // post-condition
  if(c != n) {
    //@ assert c >= 0;
  }
}
