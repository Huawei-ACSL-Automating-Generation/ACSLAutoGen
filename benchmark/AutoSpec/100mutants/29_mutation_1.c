int unknown1();
int unknown2();
int unknown3();

void main() {
  int a = 1;
  int b = 1;
  int c = 2;
  int d = 2;
  int m = 3;
  int n = 3;
  // invariant ( (m + n) % 2 == 0 && a + 1 == d && b + 1 == c)
  while (unknown1()) {
    m = a + c;
    n = b + d;
    if ((m + n) % 2 == 0) {
      a++;
      d++;
    } else {
      a--;
    }

    // invariant (b + 1 == c)
    while (unknown2()) {
      c--;
      b--;
    }
  }
  static_assert(a + c == b + d);
}
