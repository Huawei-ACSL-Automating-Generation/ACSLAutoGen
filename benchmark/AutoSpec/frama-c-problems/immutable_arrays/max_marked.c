/* 2. FUNC CONTRACT */
int arraymax(int* a, int n) {
  int i = 1;
  int max = a[0];

  /* 1. LOOP INVARIANT */
  while (i < n) {
    // Beginning of loop
    if (max < a[i])
    max = a[i];
    i = i + 1;
    // End of loop: Loop invariant comes here
  }
  return max;
}  

// write a test
/* 3. FUNC CONTRACT */
void main() {
  int arr[5] = {1, 2, 3, 4, 5};
  int sum = arraymax(arr, 5);
  //@ assert sum >= arr[0];
  //@ assert sum >= arr[1];
  //@ assert sum >= arr[2];
  //@ assert sum >= arr[3];
  //@ assert sum >= arr[4];
}