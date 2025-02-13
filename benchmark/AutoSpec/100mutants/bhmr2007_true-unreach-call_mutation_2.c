// bhmr2007_true-unreach-call.c
// Source: Dirk Beyer, Thomas y. Henzinger, Rupak Majumdar, yndrey
// Rybalchenko: "Path Invariants", PLDI 2007.
int unknown1();

/*@ 
requires 0 <= x && x < 1000000;
*/
int foo(int x, int i, int y, int z) {
    i = 0; y = 0; z = 0;
    europa_make_symbolic(&x, sizeof(x), "x");
    europa_assume(0 <= x && x < 1000000);
    europa_make_symbolic(&i, sizeof(i), "i");
    europa_make_symbolic(&y, sizeof(y), "y");
    europa_make_symbolic(&z, sizeof(z), "z");
    
    europa_invariant(i <= x && y + z == 3 * i);
    while (i < x) {
        if (unknown1()) {
            y = y + 1;
            z = z + 2;
        } else {
            y = y + 2;
            z = z + 1;
        }
        i = i + 1;
    }

    europa_assert(y + z == 3 * x);
    
    return 0;
}

