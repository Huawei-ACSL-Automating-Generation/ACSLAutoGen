/*@
requires y >= 127;
*/
void foo(int y) {
  int c = 0;
  int z = 36 * y;
  
  /*@
  loop invariant z == 36*y + c;
  loop invariant z == 36 * y + c;
  loop invariant z == (36 * y) + c;
  loop invariant z <= 36 * y + 36;
  loop invariant z < 0 ==> z >= 4608 ==> c >= 36;
  loop invariant c <= 36;
  loop invariant 36*y <= z;
  loop invariant 36 * y <= z;
  loop invariant 0 <= c;
  loop assigns z;
  loop assigns c;
  */
  while (unknown()) {
    if (c < 36)
    {
      z = z + 1;
      c = c + 1;
    }
  }
  // post-condition
  if (z < 0) {
    if (z >= 4608)  {
      //@ assert c >= 36;
    }
  }
}
