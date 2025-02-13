int unknown();

/*@
requires n > 0;
*/
void foo(int n) {
  int c = 0;
  
  /*@
  loop invariant \exists integer k; k < 0 && c == n ==> c == 1;
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
  if (n <= -1) {
    //@ assert c != n;
  }
}
