#include <stdio.h>

/*@
requires n >= 0;
requires n > 0;
requires \valid(a+(0..n-1));
requires \valid(a + (0..n-1));
ensures sorted: \forall integer i; 0 <= i < n-1 ==> a[i] <= a[i+1];
ensures permutation: \old(\forall integer j; 0 <= j < n ==> \exists integer i; 0 <= i < n && \at(a[i],Post) == \at(a[j], Pre));
*/
void bubbleSort(int *a, int n) {
    if (n <= 0) return;
    int i, j, temp;

    /*@
    loop invariant \forall integer k; i <= k < n-1 ==> a[k] <= a[k+1];
    loop invariant \forall integer k; 0 <= k < i+1 <= n-1 ==> a[k] <= a[i+1];
    loop invariant \forall integer k; 0 <= k < i ==> \exists integer l; k <= l < i && a[k] <= a[l];
    loop invariant 0 <= i;
    */
    for(i=n-1; i>0; i--) {
        /*@
        loop invariant j <= i;
        loop invariant \forall integer k; 0 <= k <= j ==> a[k] <= a[j];
        loop invariant \forall integer k; 0 <= k < j ==> a[k] <= a[j];
        loop invariant 0 <= j;
        loop invariant 0 <= i;
        loop assigns temp;
        loop assigns j;
        loop assigns a[0..i];
        loop assigns a[0..i];
        */
        for(j=0; j<i; j++) {
            if (a[j] > a[j+1]) {
                temp = a[j];
                a[j] = a[j+1];
                a[j+1] = temp;
            }
        }
    }
}

// write a test
void main() {
    int arr[5] = {5, 4, 3, 2, 1};
    bubbleSort(arr, 5);
    //@ assert \forall int i; 0 <= i < 4 ==> arr[i] <= arr[i+1];
}