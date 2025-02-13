/*@
requires n >= 0;
requires k >= 0;
*/
void foo(int k, int n) {
  int i = 0;
  int j = 0;
  
  /*@
  loop invariant j == i*(i+1)/2;
  loop invariant j == i * (i+1) / 2;
  loop invariant j == (i*(i+1))/2;
  loop invariant j <= i*(i+1)/2;
  loop invariant j <= (i+1)*(i+2)/2;
  loop invariant i <= n+1;
  loop invariant i <= n + 1;
  loop invariant 0 <= j;
  loop invariant 0 <= i;
  loop assigns j;
  loop assigns i;
  */
  while (i <= n) {
    i  = i + 1;
    j  = j + i;
  }
  // post-condition
  //@ assert i + j + k > 2*n;
}
