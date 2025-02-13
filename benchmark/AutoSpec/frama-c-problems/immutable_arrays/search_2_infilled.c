/* @ >>> INFILL <<< */
int arraySearch(int *a, int x, int n) {
    int p = 0;

    /*@
    loop invariant \forall integer k; 0 <= k < p ==> a[k] != x;
    loop invariant \forall integer i; 0 <= i < p ==> a[i] != x;
    loop invariant \exists integer k; 0 <= k < p && a[k] == x ==> \forall integer i; k <= i < p ==> a[i] != x;
    loop invariant 0 <= p;
    loop invariant (\forall integer i; 0 <= i < p ==> a[i] != x);
    loop assigns x;
    loop assigns p;
    loop assigns a[0..n-1];
    */
    while (p < n) {
        if (a[p] == x) {
            return 1;
        }
        p++;
    }
    return 0;
}

// write a test
void main() {
    int arr[5] = {1, 2, 3, 4, 5};
    int sum = arraySearch(arr, 3, 5);
    // @ assert sum == 1;
}