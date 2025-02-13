/*@
requires n > 0;
*/
void foo(int n) {
  int c = 0;
  
  /*@
  loop invariant c > 0 ==> c <= n;
  loop invariant c <= n;
  loop invariant 0 <= c;
  loop invariant (c == n) || (c < n);
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
  if(c < 0) {
    if(c > n) {
      //@ assert c == n;
    }
  }
}
