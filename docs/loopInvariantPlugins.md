# loopInvariantPlugins.cpp Plugin Guide

This document explains each "loop specification" plugin in `src/SpecGenerator/loopInvariantPlugins.cpp`: how they extract information from symbolic execution states and `LoopInfo`, how they generate ACSL `loop invariant` / `loop assigns` / `loop variant` clauses, and what auxiliary information they return for later post-state synthesis.

---

## 1. Where These Plugins Sit in the Overall Architecture

Loop specification generation in this project is roughly split into two stages:

1. **LoopInfo parsing stage** (see `src/SpecGenerator/loopInfoPlugins.cpp`)  
   Build `LoopInfo` for a loop statement, including:
   - Structural loop info: AST pointers for init/cond/inc/body
   - Symbolic states for loop entry/current
   - Index info (`indexInfo`): recognized index variable, bound, step, loop-count upper bounds, etc.
   - Change patterns (`patternInfo`): which addresses evolve linearly with the loop (`init + step`)
   - Shared state (`sharedMemoryMap`): values still determined after merging all real entry paths

2. **Loop invariant/assigns/variant generation stage** (this document; see `src/SpecGenerator/loopInvariantPlugins.cpp`)  
   Based on `LoopInfo`, plugins generate:
   - `loop invariant ...;`
   - `loop assigns ...;`
   - `loop variant ...;`
   They also return information used to build post-state (which may affect downstream spec generation or inlined assertions).

Finally, in `emitLoopInvariant(...)` in `src/SpecGenerator/specGenerator.cpp`:
- Plugins are fetched and run by group
- Returned clauses are categorized by type (assigns/invariant/variant)
- Returned post-info is substituted on real entry paths and merged into the final post-state

---

## 2. Path-Insensitive vs Path-Sensitive: Two Loop Plugin Categories

Loop specification plugins are divided into two categories (interfaces in `src/SpecGenerator/specGenerator.h`):

### 2.1 Path-Insensitive (PI) Plugins

Interface: `PathInsensitiveLoopInvPlugin`  
Return type: `PathInsensitiveLoopInvPlugin::GenResultType`

Characteristics:
- Plugin conclusions are treated as globally merged across all paths (no branch-level distinction).
- Besides `acsl`, the return structure also includes:
  - `globalNormalPathPostInfo`: global post info for normal exit paths
  - `globalInterruptPathsPostInfo`: global post info for each interrupt path (break/return/etc.)

Typical use: generating `loop assigns`, `loop variant`, and invariants that hold globally.

### 2.2 Path-Sensitive (PS) Plugins

Interface: `PathSensitiveLoopInvPlugin`  
Return type: `PathSensitiveLoopInvPlugin::GenResultType`

Characteristics:
- Can distinguish normal paths from interrupt paths, providing different post-states/conditions per path.
- Good for path-dependent properties like existence/necessity, e.g., search loops:
  - Normal exit: no hit in the interval
  - Interrupt exit: at least one hit in the interval

---

## 3. Default Enabled Plugin Groups

Not all plugins are enabled at once. Selection is group-based. Default group config is in `src/SpecGenerator/groups.cpp`:

- `DefaultLoopInfo`: `SetEntryAndCurrent`, `setPatterns`, `setSharedState`, `setIndex`
- `DefaultPathInsensitiveLoopInv`: `checkAndDumpLoopInfo`, `loopAssigns`, `paradigmMaxMin`, `loopVariant`
- `DefaultPathSensitiveLoopInv`: `paradigmSearch`, `StInGXPlugin`
- `ComplexPathInsensitiveLoopInv`: `checkAndDumpLoopInfo`, `complexLoopAssigns`
- `ComplexPathSensitiveLoopInv`: `StInGXPluginForComplexLoop`

In other words:
- Regular loops: run `DefaultLoopInfo` first to populate `LoopInfo`, then run default PI/PS loop plugins.
- Complex loops: may switch to `Complex*` groups with more conservative or lower-dependency plugins.

---

## 4. `LoopInfo` Fields Most Relevant to This File (Quick View)

`LoopInfo` is defined in `src/SpecGenerator/specGenerator.h`. This file primarily uses:

- `entryAndCurrentInfo` (filled by `SetEntryAndCurrentPlugin`)
  - `symbolicLoopEntry`: `ProgramState` after treating loop entry as symbolic start (usually one path)
  - `symbolicLoopCurrent`: `ProgramState` after one loop-body iteration (`cond -> body -> inc`) (possibly multiple paths)
  - `inactivePaths`: paths that become inactive during one iteration (break/return/etc.)
  - `loopEntryPoint`: `SourcePoint` for unified labels (`LoopEntry`)

- `indexInfo` (filled by `SetIndexPlugin`)
  - `indexRealAddr / indexSymbolicAddr / indexSymbolicValue`
  - `op / indexBound`
  - `indexPattern` (`init + step`)
  - `preciseLoopCount / maxLoopCount`

- `patternInfo` (filled by `SetPatternsPlugin`)
  - `normalExitPatternsMap`: consistent `(addr -> pattern?)` over normal step-paths
  - `interruptedPathPatternsMaps`: `(addr -> pattern?)` per interrupt path
  - `allPatternsMap`: pattern merged across all paths (more conservative)

- `sharedMemoryMap` (filled by `SetSharedStatePlugin`)
  - Memory-write set that remains determined after merging all real entry paths (improves stability for downstream inference)

---

## 5. Plugin-by-Plugin Explanation

### 5.1 `CheckAndDumpLoopInfoPlugin` (ID: `checkAndDumpLoopInfo`)

- Type: PI
- Main purpose: debugging/diagnostics (does not generate ACSL)
- Dependencies: no hard dependency (prints when available; reports unset otherwise)
- Output:
  - `acsl = nullopt`
  - empty post-info

Implementation logic:
1. If `entryAndCurrentInfo` exists: dump `symbolicLoopEntry` (and check it has exactly one path)
2. If `indexInfo` exists: print index address, symbolic value, bound, loop counts, etc.
3. If `patternInfo` exists: print each address pattern in `normalExitPatternsMap` (or `nullopt` if too complex)

Effect:
- Exposes `LoopInfo` parsing results in logs, commonly used to diagnose:
  - whether `loopInfoPlugins` correctly recognized index/pattern
  - whether path count is abnormal (blocking downstream plugins)

---

### 5.2 `LinearInvariantPlugin` (ID: `StInGXPlugin`)

- Type: PS
- Main purpose: generate loop invariants from "index condition + symbolic states" and return path-sensitive post-info
- Dependencies: `entryAndCurrentInfo` + `indexInfo`
- Output:
  - `acsl`: a possible combination of `loop invariant ...;` (generated by analyzer layer; may be empty)
  - `normalPathPostInfos` / `interruptPathsPostInfos`: for downstream post-state synthesis

Core implementation steps:
1. **Dependency checks**: fail immediately if `entryAndCurrentInfo` or `indexInfo` is missing
2. **Build loopCond (symbolic loop condition)**:
   - Compose a binary expression from `indexInfo.indexSymbolicValue`, `indexInfo.indexBound`, and `indexInfo.op`
   - Normalize `<` / `>` to `<=` / `>=` (e.g., `i < n` to `i <= n-1`) for easier printing/induction
   - Apply directional normalization for `!=` (depends on step sign)
3. **Prepare entry/current state**:
   - Clone entry path (must be single-path)
   - If `sharedMemoryMap` exists, write constantizable `VariableAddress` values into entry `memoryState` to reduce `Unknown`
   - Clone `loopCurrent` (may have multiple paths)
4. **Control state explosion**:
   - Give up (return empty optional) if `loopCurrent.paths + inactivePaths` is too large
   - If loop contains array/pointer operations, disable `generateBranches` (prefer branch merging)
5. **Call analyzer-layer induction**: `analyzer::buildLoopInvariant(loopCond, entryPath, loopCurrent, inactivePaths, generateBranches)`
6. **Wrap return values**:
   - Convert analyzer-returned `(memoryMap, pathConds)` to `PostPSInfo`
   - For return-path `returnExpr`, current implementation replaces complex return expressions with `Unknown`

Typical effect:
- For for/while loops with clear index conditions, it can generate relatively strong `loop invariant` clauses (exact format depends on analyzer implementation).

Common limitations:
- Entry path must be single-path (earlier path splitting may cause plugin failure)
- `indexInfo` quality depends on `SetIndexPlugin` recognition capability

---

### 5.3 `LinearInvariantPluginForComplexLoop` (ID: `StInGXPluginForComplexLoop`)

- Type: PS
- Main purpose: StInGX variant for complex loops; does not depend on `indexInfo`, computes `loopCond` directly from symbolic evaluation of `condExpr`
- Dependencies: `entryAndCurrentInfo`
- Output: same as `LinearInvariantPlugin`

Key differences from the regular version:
- Does not manually compose `loopCond` from `indexInfo`
- Directly evaluates `evalExpr(loopInfo.condExpr)` on entry path, requiring non-branching results (`evalExprs.size() == 1`)

Applicable scenario:
- `condExpr` cannot be recognized by `SetIndexPlugin` as a simple index condition, but symbolic execution still yields a usable `loopCond`

Limitation:
- Symbolic evaluation of `condExpr` must not branch

---

### 5.4 `LoopAssignsPlugin` (ID: `loopAssigns`)

- Type: PI
- Main purpose: generate `loop assigns ...;` and try to construct global post-state info for normal/interrupt paths
- Dependencies: `entryAndCurrentInfo` + `indexInfo` + `patternInfo`
- Output:
  - `acsl`: `loop assigns ...;` or `loop assigns \\nothing;`
  - `globalNormalPathPostInfo`: post values for written addresses (inferred when possible, otherwise `Unknown`) + required conditions (`pathConds`)
  - `globalInterruptPathsPostInfo`: written address sets for each interrupt path (mostly `Unknown`)

Core implementation idea:
1. **Determine non-local observable address set**  
   Filter out local addresses via `isLocal(addr)` (root decl not in `preState.varAddrMap`).

2. **Use `patternInfo` to determine which addresses change in the loop**  
   Iterate over `patternInfo.normalExitPatternsMap`:
   - `pattern == nullopt`: change too complex (or branch-inconsistent), handle conservatively
   - `pattern != nullopt`: treat as linear evolution `init + step`

3. **Try to lift index-shifted addresses to Range (`SymbolAddress`)**  
   Check with `tryGetAsRange`:
   - Base address itself has a pattern in `normalExitPatternsMap` (pointer base moves)
   - Or offset is a `SymbolValue` with pattern (typical: `i` changes)

   If liftable, construct a `SymbolAddress` and set `length` to loop count (`preciseLoopCount` or `maxLoopCount`).

4. **Infer post values when possible**  
   For non-range addresses:
   - If `preciseLoopCount` is known (typically step=+/-1, no extra conditions, no interrupt path), use:
     `post = init + step * loopCount`
   - If loopCount is imprecise but step magnitude matches (`abs(step)` consistent), try building post from index-after-loop and add guard conditions:
     - `index_post >= bound` and `index_post < bound + step` (step > 0)
     - analogous for step < 0

   If inference fails, set post value to `Unknown`.

5. **Generate ACSL text for assigns**  
   `assignedAddrs` stores symbolic addresses; each must be substituted on real entry paths before printing.
   Printing uses `getACSLOfValue(...)`, with hash-based deduplication for repeated fragments.

6. **Interrupt-path assigns/post-info**  
   For each interrupt path:
   - Mark all `assignedAddrs` recognized on normal paths as `Unknown`
   - Also add addresses appearing in `interruptedPathPatternsMaps[i]` (as `Unknown`)

Strengths and limitations:
- Can generate relatively compact assigns (possibly with ranges) for common linear array/pointer accesses
- Still heuristic and conservative for complex ranges/address expressions (many TODOs remain)

---

### 5.5 `ComplexLoopAssignsPlugin` (ID: `complexLoopAssigns`)

- Type: PI
- Main purpose: fallback `loop assigns` plugin for complex loops
- Dependencies: `entryAndCurrentInfo`
- Output: same shape as `LoopAssignsPlugin` (more conservative)

Implementation logic (conceptually simpler):
1. Collect addresses from all paths of `symbolicLoopCurrent`:
   - Skip locals
   - Skip addresses "pointing to structs" (currently unsupported)
   - Skip addresses unchanged relative to entry path
2. The collected set is the assigns set; printing still requires substitution and `getACSL`
3. post-state is always `Unknown` (conservative)
4. For each inactive path, collect one more written-address set to build interrupt-path post-info

Applicable scenario:
- When `patternInfo`/`indexInfo` generation fails or is unavailable, still provide a conservative write-set approximation

---

### 5.6 `ParadigmMaxMinPlugin` (ID: `paradigmMaxMin`)

- Type: PI
- Main purpose: recognize max/min idioms and generate stronger invariant + post-state
- Dependencies: `entryAndCurrentInfo` + `indexInfo` + `patternInfo`
- Output:
  - `acsl`: template invariants from `loopInvTemplates.h` (possibly multiple segments)
  - `globalNormalPathPostInfo`: model post value of `m` as `MaxMinOverRange(...)`

Idiom recognition flow:
1. Only works when index step is +/-1 (otherwise abort)
2. Traverse each `if` in loop body:
   - Match variants like `m < a[i]` / `a[i] > m` / `m > a[i]` in condition, then classify as max or min
   - Parse forms of `a[i]`, supporting:
     - `p[i]` (`ArraySubscriptExpr`)
     - `*(p+i)` (`Deref(Add)`)
     - `*it` (pointer `it` that evolves with same step as index)
3. Symbolic-execution validation:
   - After `then` branch, `m` should equal `elementValue` (`a[i]`)
   - After `else` branch, `m` must stay unchanged
4. On successful match:
   - Append template invariants (templates in `src/SpecGenerator/loopInvTemplates.h`)
   - Update post-state: set post value of `m` to `MaxMinOverRange(arrayRange, "k", extremum, pointAfterLoop)`

Template output in plain terms:
- For all scanned elements, `m` is an upper/lower bound
- There exists some position `j` such that `m` equals an element
- Index range constraints (e.g., `0 <= index <= n`)

Limitations:
- Current array-range selection is simplified (commonly `[0, bound)`); more precise offset/length handling is still pending
- Idiom matching is strict: if symbolic validation fails, no output is produced (prefer no output over unsound output)

---

### 5.7 `LoopVariantPlugin` (ID: `loopVariant`)

- Type: PI
- Main purpose: generate `loop variant ...;`
- Dependencies: `indexInfo`
- Output:
  - `acsl`: `loop variant <expr>;` (if `<expr>` can be converted to ACSL)

Implementation logic:
- Directly use `indexInfo.maxLoopCount` (computed by `SetIndexPlugin`) as variant expression
- Abort (return `nullopt`) if `getACSL` fails

Rationale:
- `maxLoopCount` is intended as an upper bound of remaining distance to the boundary; in suitable loops it can serve as a variant for termination proofs.
- This choice is simple and conservative; it is not guaranteed to hold strictly for all loops, so failure is allowed by design.

---

### 5.8 `ParadigmSearchPlugin` (ID: `paradigmSearch`)

- Type: PS
- Main purpose: recognize search-loop idioms and generate path-sensitive quantified invariants and post-info
- Dependencies: `entryAndCurrentInfo` + `indexInfo` + `patternInfo`
- Output:
  - `acsl`: tries to generate `loop invariant \\forall integer k; ...;` (empty on failure)
  - `normalPathPostInfos` / `interruptPathsPostInfos`: carries quantified conditions such as forall/exists

Preconditions (important):
- `inactivePaths.size() == 1`: only one "search succeeded" interrupt path is allowed
- index step is +/-1
- interrupt-path path-condition count is 1 (currently only single predicate is handled)

Core implementation idea:
1. Take the interrupt-path predicate (e.g., `a[i] == x`, `a[i] != x`) as `cond(i)`
2. Convert `cond(i)` to `pred(k)`:
   - Find each `Symbol` used in `cond`
   - If the symbol's source address has a pattern (`init`, `step`), replace with `init + step * (k - i_init)` (or reversed form)
   - If symbol does not change with the loop, clone directly
   - If symbol source is `SymbolAddress` (array/pointer access), also substitute symbols in sub-offset and record array base
3. Build two quantified conditions from `pred(k)`:
   - Normal exit path: `forall k in range. !pred(k)`
   - Interrupt exit path: `exists k in range. pred(k)`
4. Construct quantifier interval `range` based on `indexStep`:
   - step > 0: from current index to bound (excluding bound)
   - step < 0: from bound to current index (excluding current index)
5. Try printing the normal-path forall form as ACSL text; if printing fails, return only post-info

Effect (intuition):
- If loop never triggers break/return, you get an invariant saying no hit exists in the interval
- If loop triggers break/return, you can express existence of a hit point in the interval

Limitations:
- Only handles single predicate and single interrupt path
- Symbols in predicate must be expressible as functions of `k` via `patternInfo`, otherwise abort

---

## 6. How to Add/Extend Loop Plugins (Brief Suggestions)

When adding a new loop plugin in this framework, you typically need to decide:
1. Which `LoopInfo` fields are required dependencies (entry/current, index, pattern, shared)
2. Whether output is PI or PS (whether interrupt paths must be distinguished)
3. Which clause class the output belongs to (assigns/invariant/variant; `specGenerator` classifies by prefix)
4. Whether post-state (`memoryMap/pathConds/returnExpr`) should be updated to help downstream inference

Useful references:
- `src/SpecGenerator/loopInfoPlugins.cpp`: how `LoopInfo` is populated (pattern/index/shared)
- `src/SpecGenerator/specGenerator.cpp`: how plugin results are substituted, merged, and written back into final post-state
