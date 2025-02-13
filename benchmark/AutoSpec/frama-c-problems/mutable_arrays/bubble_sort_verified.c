#include <stdio.h>
/*@ 
    requires \valid(a+(0..n-1));
    requires n > 0;
    ensures \forall integer i,j; 0<=i<=j<=n-1 ==> a[i]<=a[j];
*/
void bubbleSort(int *a, int n) {
    if (n <= 0) {
        return;
    }
    int i, j, temp;
    /*@ 
        loop invariant \forall integer p,q; i<=p<=q<=n-1 ==> a[p]<=a[q];
        loop invariant \forall integer p,q; 0<=p<i+1==q<=n-1 ==> a[p]<=a[q];
        loop invariant 0<=i<n;
        loop assigns i,j,temp,a[0..n-1];
    */
    for(i=n-1; i>0; i--) {
        /*@  loop invariant 0<=j<=i<n;
            loop invariant \forall integer k; 0<=k<=j ==> a[k] <= a[j];
            loop invariant \forall integer p, q; 0<=p<i+1==q<=n-1 ==> a[p]<=a[q];
            loop assigns j,temp,a[0..i];
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