// goto-cc -o invar_check_multiple_loops.goto invar_check_multiple_loops.c && goto-instrument --apply-loop-contracts invar_check_multiple_loops.goto invar_check_multiple_loops_inst.goto && cbmc invar_check_multiple_loops_inst.goto --pointer-check --bounds-check --signed-overflow-check
#include <assert.h>

int main()
{
  int r, n, x, y;
  __CPROVER_assume(n > 0 && x == y);

  for(r = 0; r < n; ++r)
    // clang-format off
    __CPROVER_loop_invariant(0 <= r && r <= n)
    __CPROVER_loop_invariant(x == y + r)
    __CPROVER_decreases(n - r)
    // clang-format on
    {
      x++;
    }
  while(r > 0)
    // clang-format off
    __CPROVER_loop_invariant(r >= 0 && x == y + n + (n - r))
    __CPROVER_decreases(r)
    // clang-format on
    {
      y--;
      r--;
    }

  assert(x == y + 2 * n);

  return 0;
}