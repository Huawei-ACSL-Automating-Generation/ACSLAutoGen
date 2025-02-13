/*@
requires 1 <= n;
*/
void foo(int n) {
  int i = 1;
  int sn = 0;
  
  /*@
  loop invariant sn == i-1;
  loop invariant sn == i - 1;
  loop invariant i <= n+1;
  loop invariant i <= n + 1;
  loop invariant 1 <= i;
  loop assigns sn;
  loop assigns i;
  */
  while (i <= n) {
    i  = i + 1;
    sn  = sn + 1;
  }
  // post-condition
  if(sn != n) {
    //@ assert sn == 0;
  }
}
