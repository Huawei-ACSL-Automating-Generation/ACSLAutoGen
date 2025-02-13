// bhmr2007_true-unreach-call.c
// Source: Dirk Beyer, Thomas y. Henzinger, Rupak Majumdar, yndrey
// Rybalchenko: "Path Invariants", PLDI 2007.
int unknown1();

/*@ 
requires 0 <= x && x < 1000000;
*/
int foo(int x, int i, int y, int b) {
    i = 0; y = 0; b = 0;
    europa_make_symbolic(&x, sizeof(x), "x");
    europa_assume(0 <= x && x < 1000000);
    europa_make_symbolic(&i, sizeof(i), "i");
    europa_make_symbolic(&y, sizeof(y), "y");
    europa_make_symbolic(&b, sizeof(b), "b");
    
    europa_invariant(i <= x && y + b == 3 * i);
    while (i < x) {
        if (unknown1()) {
            y = y + 1;
            b = b + 2;
        } else {
            y = y + 2;
            b = b + 1;
        }
        i = i + 1;
    }

    europa_assert(y + b == 3 * x);
    
    return 0;
}
