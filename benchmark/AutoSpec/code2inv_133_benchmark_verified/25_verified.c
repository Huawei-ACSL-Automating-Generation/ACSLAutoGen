/*@
requires (x == 10000);
*/
void foo(int x) {
  // loop body
  /*@
  loop invariant x <= 10000;
  loop invariant 0 <= x;
  loop assigns x;
  */
  while ((x > 0)) {
    (x  = (x - 1));
  }
  // post-condition
  //@ assert  (x == 0) ;
}
