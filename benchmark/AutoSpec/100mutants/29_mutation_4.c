int unknown2();
int unknown1();
int unknown3();

void main() {
  int y = 3;
  int c = 2;
  int d = 2;
  int x = 3;
  int a = 1;
  int b = 1;
  // invariant ( (x + y) % 2 == 0 && a + 1 == d && b + 1 == c)
  while (unknown1()) {
    y = b + d;
    x = a + c;
    if ((x + y) % 2 == 0) {
      d++;
      a++;
    } else {
      a--;
    }

    // invariant (b + 1 == c)
    while (unknown2()) {
      b--;
      c--;
    }
  }
  static_assert(a + c == b + d);
}

