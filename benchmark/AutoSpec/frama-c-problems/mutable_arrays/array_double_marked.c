/* 2. FUNC CONTRACT */
void arrayDouble(int *a, int n) {
    int p = 0;

    /* 1. LOOP INVARIANT */
    while (p < n) {
        a[p] = a[p] * 2;
        p = p + 1;
    } 
}

/* 3. FUNC CONTRACT */
int main() {
    int arr[] = {0,1,2,3,4,5};
    arrayDouble(arr, 6);
    //@ assert arr[0] == 0;
    //@ assert arr[1] == 2;
    //@ assert arr[2] == 4;
    //@ assert arr[3] == 6;
    //@ assert arr[4] == 8;
    //@ assert arr[5] == 10;
}