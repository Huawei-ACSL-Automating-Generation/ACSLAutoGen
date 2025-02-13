/*@
ensures \forall integer i; 0 <= i < n ==> a[i] == \old(a[i]) * 2;
*/
void arrayDouble(int *a, unsigned int n) {
    int p = 0;

    /*@
    loop invariant \forall integer k; p <= k < n ==> a[k] == \at(a[k], Pre);
    loop invariant \forall integer k; 0 <= k < p ==> a[k] == 2 * \at(a[k], Pre);
    loop invariant 0 <= p;
    loop assigns p;
    loop assigns a[0..n-1];
    */
    while (p < n) {
        a[p] = a[p] * 2;
        p = p + 1;
    } 
}

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