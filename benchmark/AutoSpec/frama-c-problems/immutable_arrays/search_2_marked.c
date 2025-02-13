/* 2. FUNC CONTRACT */
int arraySearch(int *a, int x, int n) {
    int p = 0;

    /* 1. LOOP INVARIANT */
    while (p < n) {
        if (a[p] == x) {
            return 1;
        }
        p++;
    }
    return 0;
}

// write a test
/* 3. FUNC CONTRACT */
void main() {
    int arr[5] = {1, 2, 3, 4, 5};
    int sum = arraySearch(arr, 3, 5);
    //@ assert sum == 1;
}