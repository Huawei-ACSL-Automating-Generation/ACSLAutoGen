// goto-cc -o invar_check_nested_loops.goto invar_check_nested_loops.c && goto-instrument --apply-loop-contracts invar_check_nested_loops.goto invar_check_nested_loops_inst.goto && cbmc invar_check_nested_loops_inst.goto --pointer-check --bounds-check --signed-overflow-check
#include <assert.h>

int main()
{
  int n, s = 0;
  __CPROVER_assume(n >= 0);

  for(int i = 0; i < n; ++i)
    // clang-format off
    __CPROVER_loop_invariant(0 <= i && i <= n && s == i)
    __CPROVER_decreases(n - i)
    // clang-format on
    {
      int a, b;
      __CPROVER_assume(b >= 0 && a == b);

      while(a > 0)
        // clang-format off
        __CPROVER_loop_invariant(a >= 0 && s == i + (b - a))
        __CPROVER_decreases(a)
        // clang-format on
        {
          s++;
          a--;
        }

      s -= (b - 1);
    }

  assert(s == n);

  return 0;
}
