#include "assert.h"

void main() {
  int A[2048];
  int i;

  /*@
  loop invariant \forall integer j; 0 <= j < i ==> A[j] == j;
  loop assigns i;
  loop assigns A[0..1023];
  */
  for (i = 0; i < 1024; i++) {
    A[i] = i;
  }

  //@ assert A[1023] == 1023;
}