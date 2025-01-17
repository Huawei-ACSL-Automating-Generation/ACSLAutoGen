// goto-cc -o invar_check_break_pass.goto invar_check_break_pass.c && goto-instrument --apply-loop-contracts invar_check_break_pass.goto invar_check_break_pass_inst.goto && cbmc invar_check_break_pass_inst.goto --pointer-check --bounds-check --signed-overflow-check
#include <assert.h>

int main()
{
  int r;
  __CPROVER_assume(r >= 0);

  while(r > 0)
    // clang-format off
    __CPROVER_loop_invariant(r >= 0)
    __CPROVER_decreases(r)
    // clang-format on
    {
      --r;
      if(r <= 1)
      {
        break;
      }
      else
      {
        --r;
      }
    }

  assert(r == 0 || r == 1);

  return 0;
}