/*@
requires n >= 0;
*/
void foo(int n) {
  int x = n;
  int y = 0;
  
  /*@
  loop invariant y == n - x;
  loop invariant y <= n;
  loop invariant x <= n;
  loop invariant x + y == n;
  loop invariant n == x + y;
  loop invariant 0 <= y;
  loop invariant 0 <= x;
  loop assigns y;
  loop assigns x;
  */
  while (x > 0) {
    y  = y + 1;
    x  = x - 1;
  }
  // post-condition
  //@ assert n == x + y;
}
