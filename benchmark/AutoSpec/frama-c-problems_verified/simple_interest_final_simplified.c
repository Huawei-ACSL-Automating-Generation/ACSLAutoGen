#include<limits.h>

/*@
assigns \nothing;
ensures \result == p*n*r/100;
requires INT_MIN < r <= INT_MAX;
requires INT_MIN < n <= INT_MAX;
requires INT_MIN < p <= INT_MAX;
*/
int simple(int p,int n,int r)
{
    int si;
    si = p*n*r/100;
    return si;
}
 
int main()
{
    int s = simple(10000, 3,10);
    return 0;
}