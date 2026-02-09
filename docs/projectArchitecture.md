# Project Architecture (Overall Framework Overview)

This document describes ACSLGen's architecture and data flow, helping readers understand the full pipeline from `compile_commands.json` to ACSL output.

---

## 1. System Goals and Main Workflow

ACSLGen is built on Clang for static analysis and source rewriting. Its core goals are:
- Parse C/C++ source code (using real compilation flags from `compile_commands.json`)
- Perform symbolic execution and rule-based inference
- Generate ACSL contracts (function-level contracts, loop invariant/assigns/variant, etc.)
- Write generated results back to `_acsl.c` output files

Main workflow overview:
1. **Clang Tool Entry** (`src/main.cpp`): parse arguments and construct `ClangTool`
2. **AST and Context Initialization** (`src/Context`): initialize `SourceManager`, `Rewriter`, and function sets
3. **Analysis and Symbolic Execution** (`src/Analyzer`): build `ProgramState` and explore execution paths
4. **Specification Generation** (`src/SpecGenerator`): generate ACSL text via plugins
5. **Output and Rewriting**: insert ACSL into source code and write `_acsl.c`

---

## 2. Directory and Module Layout

- `src/main.cpp`  
  Clang tooling entry; handles `-ast-only`, function filtering, and output naming

- `src/Context`  
  AST context wrappers, Rewriter management, function collection, and insertion-point management

- `src/Analyzer`  
  Core semantic analysis and symbolic execution engine (`ProgramState`, `PathConditions`, cross-TU support)

- `src/Analyzer/Symbolic`  
  Symbolic expression system, induction, and evaluation infrastructure

- `src/SpecGenerator`  
  Specification generation plugin system, templates, and scheduling

- `src/Stingx`  
  Constraint and linear-transformation library (for invariant and linear relation inference)

- `src/Utils`  
  Foundational tools and shared helpers

Other directories:
- `tools/`: helper scripts or utilities
- `scripts/`: build/run/debug scripts (see `scripts/README.md`)
- `tests/`: test suites
- `benchmark/`: performance and scenario cases

---

## 3. End-to-End Data Flow

1. **Input Stage**  
   - `compile_commands.json` provides real compiler flags and include paths  
   - Users specify target source files and optional function filters

2. **AST Construction and Preprocessing**  
   - Clang `FrontendAction` parses the TU  
   - Collect function lists, comments, and `SourceManager`/`ASTContext` information  
   - Preprocessing removes old ACSL comments (while keeping top-level `requires`)

3. **Symbolic Execution and State Construction**  
   - Create a `ProgramState` for each function  
   - Execute statement-level steps while maintaining:
     - `MemoryModel` (variables/pointers/struct fields)
     - `PathConditions` (branch conditions)
     - `ReturnExpr` (return value)
   - Also produce preState/postState for spec generation

4. **Specification Generation (Function + Loop)**  
   - `SpecGenerator` schedules plugins by group  
   - Function level: `requires/ensures/assigns`  
   - Loop level: `loop invariant/assigns/variant`  
   - Plugins output ACSL text and post-state information (for merge/substitution)

5. **Merging and Label Insertion**  
   - Merge post-states from multiple paths  
   - Normalize labels/`SourcePoint` and inject markers

6. **Rewrite Output**  
   - Insert ACSL before function definitions  
   - Output `<stem>_acsl.c`

---

## 4. Core Subsystems and Responsibilities

### 4.1 Clang Tooling Entry Layer
- **File**: `src/main.cpp`
- **Responsibilities**:
  - Argument parsing (function filtering, AST-only)
  - Comment handling: remove old ACSL comments except top-level `requires`
  - Output naming and file writing
  - Rewrite strategy: insert ACSL before function definitions to avoid conflicts with generated output

### 4.2 Context (Compilation Context Wrapper)
- **Files**: `src/Context/context.h` / `src/Context/context.cpp`
- **Responsibilities**:
  - Manage `ASTContext` / `SourceManager` / `Rewriter`
  - Collect target functions and insertion points
  - Unify write strategy and label management

### 4.3 Analyzer (Symbolic Execution and Function-Level Analysis)
- **Files**: `src/Analyzer/*`
- **Responsibilities**:
  - Traverse analyzable functions in a TU
  - Build `ProgramState` and execute statement-level symbolic execution
  - Aggregate pre/post states and hand them to `SpecGenerator`
  - Decouple Paths from States; memory model, path conditions, and scope lifetimes are maintained separately

Key components:
- `ProgramState` (`src/Analyzer/state.*`): memory model, path conditions, local-scope lifecycle management
- `ACSLFunction` (`src/Analyzer/function.*`): function wrapper and metadata
- `analysis.cpp`: analysis flow and generated-result insertion
- `crossTU.*`: cross-TU support and compilation-database integration

### 4.4 Symbolic (Expression and Reasoning Foundation)
- **Files**: `src/Analyzer/Symbolic/*`
- **Responsibilities**:
  - Build symbolic expressions and address models
  - Provide expression equivalence, simplification, and hashing operations
  - Provide semantic foundations for invariant/inductive reasoning

Symbolic uses `SymbolicExpr` as the unified base class. The hierarchy can be understood as "base class -> concrete subclasses":

- **`SymbolicExpr` (root base class for expressions)**  
  Provides cloning, equality, simplification, and ACSL-output interfaces. All expression and address nodes inherit from it.

- **Expression branch (directly inheriting from `SymbolicExpr`)**  
  - `LiteralExpr`: literals and constants.  
  - `SymbolValue`: symbolic values with provenance metadata.  
  - `BinaryOpExpr` / `UnaryOpExpr`: arithmetic, comparison, and logical operation nodes.  
  - `UnknownExpr`: conservative placeholder when outside supported capability.  
  - `Structure`: container for struct values, internally maintaining field-value sets and aggregate queries.

- **Address branch (`Address` -> `SymbolicExpr`)**  
  - `VariableAddress`: variable-level addresses.  
  - `FieldAddress`: struct-field addresses.  
  - `SymbolAddress`: symbolic addresses with offset/length, usable as ranges or pointer bases. `offset/length` are themselves `SymbolicExpr`, commonly composed from `SymbolValue`, `LiteralExpr`, and `BinaryOpExpr` (e.g., `i`, `i + k`, `n - i`) for downstream boundary inference in range/aggregate expressions.

- **Provenance information (`Symbol` mix-in)**  
  `Symbol` tracks source addresses and source points via `getFromAddr/getFromPoint`. It is currently carried by `Structure`, `SymbolAddress`, `SymbolValue`, `SumOverRange`, `MaxMinOverRange`, etc.

- **Range and Aggregate Expressions (`OverRangeExpr` family)**  
  `SymbolAddress::RangeIndex` provides index placeholders used as logical iterators in range expressions such as `SumOverRange`, `QuantifierOverRange`, and `MaxMinOverRange`. They are mainly constructed during loop-invariant generation (`src/SpecGenerator/loopInvariantPlugins.cpp`) and consumed in invariant inference and linearization (e.g., `src/Analyzer/Symbolic/invariant.cpp`).

- **Source Binding (`SourcePoint`)**  
  Binds semantic nodes to stable source-code labels for ACSL output and substitution.

### 4.5 SpecGenerator (Specification Generation and Plugin System)
- **Files**: `src/SpecGenerator/*`
- **Responsibilities**:
  - Organize plugins and groups
  - Generate function contracts and loop specifications
  - Merge post-state and convert it into ACSL text
  - Output conservative contracts when information is insufficient, avoiding pipeline interruption

The following function and loop examples illustrate the final contract shapes.

Function-level contracts are generated by `functionContractPlugins.cpp`. Path information and write sets are merged into `assigns` and `behavior`. Branching functions form multiple behaviors with corresponding assumptions/postconditions:
```c
int f(int *p, int x) { if (x > 0) { p[0] = x; return 1; } return 0; }
/*@
  assigns p[0];
  behavior b0:
    assumes x > 0;
    assigns p[0];
    ensures \result == 1;
  behavior b1:
    assumes !(x > 0);
    assigns \nothing;
    ensures \result == 0;
*/
```

Loop specifications are generated in two layers: `loopInfoPlugins.cpp` extracts entry/current states, index variables, and linear-access patterns; `loopInvariantPlugins.cpp` outputs `loop invariant/assigns/variant` based on that information. Typical counting loops often generate write sets, variants, and inductive invariants together:
```c
for (int i = 0; i < n; i++) { p[i]++; x++; }
/*@
  loop assigns p[i .. n - 1], x, i;
  loop invariant x == \at(x, LoopEntry) + i;
  loop variant n - i;
*/
```
For search or max/min loop patterns, plugins introduce quantifier or aggregate templates (e.g., `\forall`, `MaxMinOverRange(...)`) to reduce manual annotations while preserving provability.

Core files:
- `specGenerator.*`: top-level scheduling and output assembly
- `groups.*`: plugin orchestration
- `functionContractPlugins.cpp`: function specification generation
- `loopInfoPlugins.cpp` / `loopInvariantPlugins.cpp`: loop specification generation
- `*_Templates.h`: ACSL text templates

### 4.6 Stingx (Linear Relations and Invariant Utilities)
- **Files**: `src/Stingx/*`
- **Responsibilities**:
  - Provide linear expressions, matrices, transformations, and constraint management
  - Provide infrastructure for invariant inference
  - Related algorithms are based on linear-constraint inference with Farkas' lemma; technical details:
    @inproceedings{ke2025affine,
      title={Affine disjunctive invariant generation with farkas' lemma},
      author={Ke, Jingyu and Fu, Hongfei and Liu, Hongming and Sun, Zhouyue and Chen, Liqian and Li, Guoqiang},
      booktitle={International Conference on Verification, Model Checking, and Abstract Interpretation},
      pages={187--213},
      year={2025},
      organization={Springer}
    }

### 4.7 Utils (Shared Helpers)
- **Files**: `src/Utils/*`
- **Responsibilities**: general utility functions, simplification logic, and foundational data structures
