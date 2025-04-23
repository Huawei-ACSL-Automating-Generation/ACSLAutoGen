#include <limits.h>

/* 1. FUNC CONTRACT */
int abss(int val)
{
    if (val < 0)
        return -val;
    return val;
}

/* 2. FUNC CONTRACT */
void foo(int a)
{
    int b = abss(-42);
    //@ assert b == 42;
    int c = abss(42);
    //@ assert c == 42;
    int d = abss(a);
    int e = abss(INT_MIN);
}
