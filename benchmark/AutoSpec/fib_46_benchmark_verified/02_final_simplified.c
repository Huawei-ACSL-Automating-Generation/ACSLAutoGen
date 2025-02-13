#include <assert.h>
int unknown1();
int unknown2();
int unknown3();
int unknown4();

int main()
{
	int i = 1;
	int j = 0;
	int z = i - j;
	int x = 0;
	int y = 0;
	int w = 0;

	/*@
	loop invariant z % 2 == 1;
	loop invariant x == y;
	loop invariant w == 2 * y;
	loop invariant 0 <= y;
	loop assigns z;
	loop assigns y;
	loop assigns x;
	loop assigns w;
	*/
	while(unknown2()) 
	{
		z += x + y + w;
		y++;
		if (z % 2 == 1) 
		  x++;
		w += 2; 
	}

	//@ assert x==y;
}
