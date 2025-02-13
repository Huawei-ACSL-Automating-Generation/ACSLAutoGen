/*@
requires (x == 100);
*/
void foo(int x) { 
  // loop body
  /*@
  loop invariant x <= 100;
  loop invariant 0 <= x;
  loop assigns x;
  */
  while (x > 0) {
    x  = x - 1;
  }
  // post-condition
  //@ assert x == 0;
}
