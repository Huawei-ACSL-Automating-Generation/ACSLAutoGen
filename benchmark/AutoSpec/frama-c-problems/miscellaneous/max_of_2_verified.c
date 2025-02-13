/*@
    ensures \result == x || \result == y;
    ensures \result >= x && \result >= y
*/
int max(int x, int y) {
    if (x >= y) {
        return x;
    }
    return y;
}

// write a test
void main() {
    int s = max(34,45);
    //@ assert s==45;
    int t = max(-43,34);
    //@ assert t==34;
}