/* 1. FUNC CONTRACT */
int function(int x, int y) {
    int res ;
    y += 10 ;
    x -= 5 ;
    res = x + y ;
    //@ assert -15 <= res <= 5;
    return res ;
}
/* 2. FUNC CONTRACT */

// write a test
void main() {
    int t = function(-5, 5);
    //@ assert t == 0;
}