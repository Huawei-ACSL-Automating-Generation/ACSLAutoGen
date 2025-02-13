// bhmr2007_true-unreach-call.c
// Source: Dirk Beyer, Thomas A. Henzinger, Rupak Majumdar, Andrey
// Rybalchenko: "Path Invariants", PLDI 2007.
int unknown1();

/*@ 
requires 0 <= n && n < 1000000;
*/
int foo(int n, int i, int a, int b) {
    b = 0; a = 0; i = 0;
    europa_make_symbolic(&n, sizeof(n), "n");
    europa_assume(0 <= n && n < 1000000);
    europa_make_symbolic(&b, sizeof(b), "b");
    europa_make_symbolic(&i, sizeof(i), "i");
    europa_make_symbolic(&a, sizeof(a), "a");
    
    europa_invariant(i <= n && a + b == 3 * i);
    while (i < n) {
        i = i + 1;
        if (unknown1()) {
            a = a + 1;
            b = b + 2;
        } else {
            b = b + 1;
            a = a + 2;
        }
    }

    europa_assert(3 * n == a + b);
    
    return 0;
}

