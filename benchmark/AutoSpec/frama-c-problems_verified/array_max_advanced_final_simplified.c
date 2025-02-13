// This program used an advanced ACSL clause: \max()
// Note: Some versions of 'wp' plugin may not support the \lambda clause.
//       The program may not verify in every machine.

/*@
requires \valid(arr + (0..n-1));
ensures \result >= \max(\lambda integer i; 0 <= i < n; arr[i]);
assigns \nothing;
*/
int array_max_advanced(int* arr, int n) {
    int max = arr[0];
    /*@
    loop invariant \forall integer k; 0 <= k < i ==> max >= arr[k];
    loop invariant 0 <= i;
    loop assigns max;
    loop assigns i;
    loop assigns arr[0..n-1];
    */
    for (int i = 0; i < n; i++) {
        if (arr[i] > max) {
            max = arr[i];
        }
    }
    return max;
}

// write a test
void main() {
    int arr[5] = {1, 2, 3, 4, 5};
    int max = array_max_advanced(arr, 5);
    //@ assert max >= arr[0];
    //@ assert max >= arr[1];
    //@ assert max >= arr[2];
    //@ assert max >= arr[3];
    //@ assert max >= arr[4];
}