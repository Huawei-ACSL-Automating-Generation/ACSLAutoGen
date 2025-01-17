// goto-cc -o invar_loop_constant_pass.goto invar_loop_constant_pass.c && goto-instrument --apply-loop-contracts invar_loop_constant_pass.goto invar_loop_constant_pass_inst.goto && cbmc invar_loop_constant_pass_inst.goto --pointer-check --bounds-check --signed-overflow-check
#include <assert.h>

int main()
{
  int r, s = 1;
  __CPROVER_assume(r >= 0);
  while(r > 0)
    // clang-format off
    __CPROVER_loop_invariant(r >= 0 && s == 1)
    __CPROVER_decreases(r)
    // clang-format on
    {
      s = 1;
      r--;
    }
  assert(r == 0);
  assert(s == 1);

  return 0;
}