/* 1. FUNC CONTRACT */
void div_rem(unsigned x, unsigned y, unsigned* q, unsigned* r) {
    *q = x / y;
    *r = x % y;
}

// write a test
/* 2. FUNC CONTRACT */
void main() {
    unsigned q, r;
    div_rem(10, 3, &q, &r);
    //@ assert q == 3;
    //@ assert r == 1;
}