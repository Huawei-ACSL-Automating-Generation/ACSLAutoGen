#include "assert.h"

void main() {
  int X[2048];
  int a;

  // invariant (forall a: 0 <= a && a < 1024 => X[a] == a)
  for (a = 0; a < 1024; a++) {
    X[a] = a;
  }

  __VERIFIER_assert(X[1023] == 1023);
}

