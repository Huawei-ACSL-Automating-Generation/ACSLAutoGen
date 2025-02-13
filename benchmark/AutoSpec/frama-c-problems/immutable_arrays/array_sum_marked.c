/* 2. FUNC CONTRACT */
int sumArray(int *a, int n) {
    int p = 0, sum = 0;

    /* 1. LOOP INVARIANT */
    while (p < n) {
        sum = sum + a[p];
        p++;
    }
    return sum;
}

// write a test
/* 3. FUNC CONTRACT */
void main() {
    int arr[5] = {1, 2, 3, 4, 5};
    int sum = sumArray(arr, 5);
    //@ assert sum == 15;
}