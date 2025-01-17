#include <assert.h>
#include <stdio.h>
#include <malloc.h>

void assign(int *p, int len)
__CPROVER_requires(len >= 0)
__CPROVER_ensures(__CPROVER_forall {int i; (0 <= i && i < len) ==> (p[i] == i)})
__CPROVER_assigns(*p)
{
    for (int i = 0; i < len; ++i)
		__CPROVER_loop_invariant((0 <= i) && (i <= len))
		__CPROVER_loop_invariant(__CPROVER_forall {int k; (0 <= k && k < len) ==> ( k < i ==> p[k] == k )})
		__CPROVER_decreases(len-i)
    {
        p[i] = i;
    }
}


int main() {
    int len = 0;
	for(int i = 0; i < 11; i++)
		__CPROVER_loop_invariant((0 <= i) && (i <= 11))
		__CPROVER_loop_invariant(__CPROVER_forall {int k; (0 <= k && k < 11) ==> ( k < i ==> len == 1000 )})
		__CPROVER_decreases(11-i)
	{
		len = 1000;
	}
	assert(len == 1000);
	
    int *p = malloc(sizeof(int)*len);
    assign(p, len);

    __CPROVER_output("%d", p);
	assert(p[len-1] == len-1);
}