/* 1. FUNC CONTRACT */
void swap(int* a, int* b) {
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

/* 2. FUNC CONTRACT */
int main(){
    int a = 37;
    int b = 91;
    swap(&a, &b);
    //@ assert a == 91;
    //@ assert b == 37;
}