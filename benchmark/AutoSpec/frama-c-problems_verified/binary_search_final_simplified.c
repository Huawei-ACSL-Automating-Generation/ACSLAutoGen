/*@
requires n >= 0;
requires \internal: \forall integer i; 0 <= i < n ==> \forall integer j; 0 <= j < n ==> \separated(a+i, a+j);
ensures (\result >= 0 && \result < n && a[\result] == x) || \result == -1;
ensures (\result == -1) ==> (\forall integer i; 0 <= i < n ==> a[i] != x);
assigns \nothing;
*/
int binarysearch(int* a, int x, int n) {

    int low = -1;
    int high = n;
    int p;

    /*@
    loop invariant high <= n;
    loop invariant -1 <= low;
    loop assigns p;
    loop assigns low;
    loop assigns high;
    loop assigns a[0..n-1];
    */
    while (low+1 < high) {
        p = (low + high) / 2;
        if (a[p] == x) 
            return p;
        else 
            if (a[p] < x)
            low = p;
            else high = p;
    }
    return -1;
}

// write a test
void main() {
    int arr[5] = {1, 2, 3, 4, 5};
    int sum = binarysearch(arr, 3, 5);
    //@ assert arr[sum] == 3;
}