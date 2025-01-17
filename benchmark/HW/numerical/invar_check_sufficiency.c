// goto-cc -o invar_check_sufficiency.goto invar_check_sufficiency.c && goto-instrument --apply-loop-contracts invar_check_sufficiency.goto invar_check_sufficiency_inst.goto && cbmc invar_check_sufficiency_inst.goto --pointer-check --bounds-check --signed-overflow-check
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
    }

  assert(r == 0);

  return 0;
}