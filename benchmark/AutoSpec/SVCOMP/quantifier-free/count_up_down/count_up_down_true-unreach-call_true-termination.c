// count_up_down_true-unreach-call_true-termination.c

/*@
requires n > 0;
*/
int foo(int n, int x, int y) {
	europa_make_symbolic(&n, sizeof(n), "n");
	x = n;
	y = 0;
	europa_make_symbolic(&x, sizeof(x), "x");
	europa_make_symbolic(&y, sizeof(y), "y");
	
	europa_invariant(x >= 0 && x + y == n);
	while (x > 0) {
		x++;
		y--;
	}

	europa_assert(y == n);

	return 0;
}
