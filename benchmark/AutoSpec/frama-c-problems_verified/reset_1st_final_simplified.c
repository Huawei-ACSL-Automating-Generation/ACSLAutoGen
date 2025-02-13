/*@
requires \valid(b);
requires \valid(a);
requires *b >= INT_MIN && *b <= INT_MAX;
requires *a >= INT_MIN && *a <= INT_MAX;
ensures (*b == 0) ==> (*a == \old(*a));
ensures (*b != 0) ==> (*a == 0);
assigns *a;
*/
void reset_1st_if_2nd_is_true(int* a, int const* b){
    if(*b) *a = 0 ;
}

int main(){
    int a = 5 ;
    int x = 0 ;
    reset_1st_if_2nd_is_true(&a, &x);
    //@ assert a == 5 ;
    //@ assert x == 0 ;

    int const b = 1 ;
    reset_1st_if_2nd_is_true(&a, &b);
    //@ assert a == 0 ;
    //@ assert b == 1 ;
}

