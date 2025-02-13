/* @ >>> INFILL <<< */
int arraysearch(int* a, int x, int n) { 

  /*@
  loop invariant \forall integer k; 0 <= k < p ==> x != a[k];
  loop invariant \forall integer k; 0 <= k < p ==> a[k] != x;
  loop invariant \forall integer i; 0 <= i < p ==> a[i] != x;
  loop invariant 0 <= p;
  loop invariant (\forall integer i; 0 <= i < p ==> a[i] != x);
  loop assigns x;
  loop assigns p;
  loop assigns n;
  loop assigns a[0..n-1];
  */
  for (int p = 0; p < n; p++) {
    // STart
    if (x == a[p]) 
       return 1;
    // End
  }
  return 0;
}

// write a test
void main() {
  int arr[5] = {1, 2, 3, 4, 5};
  int sum = arraysearch(arr, 3, 5);
  // @ assert sum == 1;
}