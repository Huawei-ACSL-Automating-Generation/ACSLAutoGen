// Program to find index of an element in an array
// Behaviors not used in this program

/* 2. FUNC CONTRACT */
int array_find(int* arr, int n, int x) {
    int i = 0;

    /* 1. LOOP INVARIANT */
    for (i = 0; i < n; i++) {
        if (arr[i] == x) {
            return i;
        }
    }
    return -1;
}

// write a test
/* 3. FUNC CONTRACT */
void main() {
    int arr[5] = {1, 2, 3, 4, 5};
    int index = array_find(arr, 5, 3);
    //@ assert index == 2;
}