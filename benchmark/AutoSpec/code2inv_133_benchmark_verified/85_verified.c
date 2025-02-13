int main() {
  int x = -15000;
  int y = 0;
  
  /*@
  loop invariant x <= -15000 || 1 <= y;
  loop invariant x <= -15000 || 0 <= y;
  loop invariant x <= -15000 || -15000 <= x;
  loop invariant x < 0 || 1 <= y;
  loop invariant x < 0 || 0 <= y;
  loop invariant x < 0 || -15000 <= x;
  loop invariant 1 <= y || x <= 0;
  loop invariant 1 <= y || 0 <= y;
  loop invariant 1 <= y || -15000 <= x;
  loop invariant 0 <= y || x <= 0;
  loop invariant 0 <= y || -15000 <= x;
  loop invariant -15000 <= x || x <= 0;
  loop assigns x;
  loop invariant 0 <= y;
  loop invariant -15000 <= x;
  loop assigns y;
  loop assigns x;
  */
  while (x < 0) {
    x  = x + y;
    y  = y + 1;
  }
  // post-condition
  //@ assert y > 0;
}
