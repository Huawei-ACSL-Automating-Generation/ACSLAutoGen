int main() {
  int x = -50;
  int y = 0;

  /*@
  loop invariant x <= -50 + y*(y-1)/2;
  loop invariant x < y*y;
  loop invariant 0 <= y;
  loop invariant -50 <= x + y*(y-1)/2;
  loop assigns y;
  loop assigns x;
  */
  while (x < 0) {
    x  = x + y;
    y  = y + 1;
  }
  // post-condition
  //@ assert  (y > 0) ;
}
