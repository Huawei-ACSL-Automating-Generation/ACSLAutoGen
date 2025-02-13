/* 2. FUNC CONTRACT */
int arraysearch(int* a, int x, int n) { 

  /* 1. LOOP INVARIANT */
  for (int p = 0; p < n; p++) {
    // STart
    if (x == a[p]) 
       return 1;
    // End
  }
  return 0;
}

// write a test
/* 3. FUNC CONTRACT */
void main() {
  int arr[5] = {1, 2, 3, 4, 5};
  int sum = arraysearch(arr, 3, 5);
  //@ assert sum == 1;
}