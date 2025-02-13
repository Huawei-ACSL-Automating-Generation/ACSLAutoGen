/*@
    requires n > 0;
    requires \valid_read(a + (0..n-1));

    ensures \forall integer k; 0 <= k < n/2 ==> a[k] == a[n-k-1];
*/
void reverse(int *a, int n) {
    int i = 0;
    int j = n-1;
    /*@
        loop invariant i <= n/2;
        loop invariant j >= n/2;
        loop assigns i, j, a[0..(n-1)];
    */
    while (i < n/2) {
        int temp = a[i];
        a[i] = a[j];
        a[j] = temp;
        i++;
        j--;
    }
}

// write a test
void main() {
    int arr[5] = {1, 2, 3, 4, 5};
    reverse(arr, 5);
    //@ assert arr[0] == 5;
    //@ assert arr[1] == 4;
    //@ assert arr[2] == 3;
    //@ assert arr[3] == 2;
    //@ assert arr[4] == 1;
}