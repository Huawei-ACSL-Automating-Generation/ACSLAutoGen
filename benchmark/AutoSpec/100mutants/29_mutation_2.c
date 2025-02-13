int unknown1();
int unknown2();
int unknown3();

void main() {
  int p = 1;
  int q = 1;
  int r = 2;
  int s = 2;
  int m = 3;
  int n = 3;
  // invariant ( (m + n) % 2 == 0 && p + 1 == s && q + 1 == r)
  while (unknown1()) {
    m = p + r;
    n = q + s;
    if ((m + n) % 2 == 0) {
      p++;
      s++;
    } else {
      p--;
    }

    // invariant (q + 1 == r)
    while (unknown2()) {
      r--;
      q--;
    }
  }
  static_assert(p + r == q + s);
}

