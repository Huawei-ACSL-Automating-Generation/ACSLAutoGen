/* 1. FUNC CONTRACT */
int incr_a_by_b(int* a, int const* b){
    *a += *b;
    return *a;
}

// write a test
/* 2. FUNC CONTRACT */
void main() {
    int a = 10;
    int b = 20;
    incr_a_by_b(&a, &b);
    //@ assert a == 30;
    //@ assert b == 20;
}