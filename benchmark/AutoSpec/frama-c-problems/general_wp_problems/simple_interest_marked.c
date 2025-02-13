#include<limits.h>

/* 1. FUNC CONTRACT */
int simple(int p,int n,int r)
{
    int si;
    si = p*n*r/100;
    return si;
}
 
/* 2. FUNC CONTRACT */
int main()
{
    int s = simple(10000, 3,10);
    return 0;
}