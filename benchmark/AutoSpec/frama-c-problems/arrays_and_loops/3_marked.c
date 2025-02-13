/* 1. FUNC CONTRACT */
int func(int a) {
    int x, y;
    int sum, res;
    if (a == 0){
        x = 0; y = 0;
    }
    else {
        x = 5; y = 5;
    }
    sum = x + y; 
    res = 10/sum; 
    return res;
}

/* 2. FUNC CONTRACT */
int main() {
    int a = func(4);
    //@ assert a == 1;
    return 0;
}