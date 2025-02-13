// Program to find index of an element in an array
// Behaviors not used in this program

int array_find(int* arr, int n, int x) {
    int i = 0;

    /*@
    >>> INFILL <<<
    loop invariant \forall integer k; 0 <= k < i ==> arr[k] != x;
    loop invariant \forall integer j; 0 <= j < i ==> arr[j] != x;
    loop invariant \exists integer j; 0 <= j < i && arr[j] == x ==> (\forall integer k; 0 <= k < j && arr[k] != x);
    loop invariant 0 <= i;
    loop assigns x;
    loop assigns i;
    loop assigns arr[0..n-1];
    */
    for (i = 0; i < n; i++) {
        if (arr[i] == x) {
            return i;
        }
    }
    return -1;
}

// write a test
void main() {
    int arr[5] = {1, 2, 3, 4, 5};
    int index = array_find(arr, 5, 3);
    // @ assert index == 2;
}