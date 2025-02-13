// count_up_down_true-unreach-call_true-termination.c

/*@
requires n > 0;
*/
int foo(int n, int x, int y) {
	x = n;
	y = 0;
	
	/*@
	loop invariant x;
	loop invariant 0 <= x;
	loop assigns y;
	loop assigns x;
	*/
	while (x > 0) {
		x++;
		y--;
	}

	//@ assert y == n;

	return 0;
}
