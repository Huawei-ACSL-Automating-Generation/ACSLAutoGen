#include "assert.h"

/*@
requires 0 < n;
*/
int foo(int n) {
	int k = 0;
	int i = 0;

	/*@
	loop invariant k == i;
	loop assigns k;
	loop assigns i;
	*/
	while (i < n) {
		i++;
		k++;
	}
	int j = n;
	/*@
	loop invariant j <= k;
	loop assigns k;
	loop assigns j;
	loop assigns i;
	*/
	while (j > 0) {
		//@ assert k > 0;
		j--;
		k--;
	}

	return 0;
}