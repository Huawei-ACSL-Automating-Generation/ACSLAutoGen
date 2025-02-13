int main() {
  int x = 0;
  
  /*@
  loop invariant x <= 100;
  loop invariant 0 <= x;
  loop assigns x;
  */
  while (x < 100) {
    x = x + 1;
  }
  // post-condition
  //@ assert x == 100;
}