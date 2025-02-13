//terminator_03_true-unreach-call_true-termination.c

/*@
requires y <= 1000000;
*/
int foo(int x, int y) {
	//int x;
	//int y;

	if (y > 0) {
		/*@
		loop assigns y;
		loop assigns x;
		*/
		while (x < 100) {
			x = x + y;
		}
	}
	
	//@ assert y <= 0 || (y > 0 && x >= 100);

	return 0;
}
