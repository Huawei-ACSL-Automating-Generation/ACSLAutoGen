/*@
requires i >= 0 && j >= 0;
*/
int foo(int i, int j) {
    //int i;
    //int j;


    int x = i;
    int y = j;
    
    /*@
    loop invariant j - y == i - x;
    loop assigns y;
    loop assigns x;
    loop assigns j;
    loop assigns i;
    */
    while (x != 0) {
    	x--;
    	y--;
    }

    if (i == j) {
    	//@ assert y == 0;
    }
    
    return 0;
}
