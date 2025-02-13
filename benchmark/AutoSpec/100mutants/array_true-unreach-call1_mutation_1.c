#include "assert.h"

void main() {
  int X[2048];
  int i;

  // invariant (forall i: 0 <= i && i < 1024 => X[i] == i)
  for (i = 0; i < 1024; i++) {
    X[i] = i;
  }

  __VERIFIER_assert(X[1023] == 1023);
}
