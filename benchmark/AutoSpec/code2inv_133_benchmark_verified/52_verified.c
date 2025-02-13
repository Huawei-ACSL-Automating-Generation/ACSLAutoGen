int main() {
  int c = 0;
  
  /*@
  loop invariant c <= 4;
  loop invariant \exists integer k; 0 <= k <= 4 && c == k;
  loop invariant 0 <= c;
  loop invariant (c >= 0 && c <= 4) || (c == 0);
  loop assigns c;
  */
  while (unknown()) {
    if (unknown()) {
      if (c != 4) {
        c = c + 1;
      }
    } else {
      if (c == 4) {
        c = 1;
      }
    }
  }
  // post-condition
  if (c < 0) {
    if (c > 4) {
      //@ assert c == 4;
    }
  }
}
