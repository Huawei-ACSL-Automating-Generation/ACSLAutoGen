# Scripts

This directory consolidates the project’s executable helper scripts. Each script is designed to be invoked from any working directory; all paths are resolved relative to the script’s own location to avoid assumptions about the caller’s current directory.

## Script Descriptions

- **compile.sh**: Configures and builds the AutoGen project (Debug build) in `AutoGen/build/` and generates compile commands for downstream tooling.
- **frama_cpp_wrapper.sh**: Provides a GCC preprocessing wrapper for Frama-C by supplying the required include paths and feature macros from the neighboring openHiTLS tree.
- **print-ast.sh**: Builds the project and runs ACSLG in AST-only mode on `AutoGen/test.c` to inspect the parsed syntax tree.
- **run-tests.sh**: Configures and builds the test targets, then executes the test suite via CTest with failure diagnostics.
- **experiment.sh**: Executes the ACSLG + Frama-C WP pipeline over `bn_basic.c`, `bn_bincal.c`, and `noasm_bn_bincal.c` (configurable via `SUITES_OVERRIDE`), capturing per-function logs and writing `results.csv`, `report.md`, and `bn_wp_results.txt` under `AutoGen/runlogs/` (generated files are kept when `KEEP_GENERATED=success|all`).
- **run_single.sh**: Executes the ACSLG + Frama-C WP pipeline for a single function, writing `results.csv` plus logs under `AutoGen/runlogs/single_<FUNC>_<timestamp>/`.
- **sweep_acslg_only.sh**: Sweeps all function definitions in a C file with ACSLG only (no Frama-C), writing per-function logs and a TSV summary under `AutoGen/runlogs/`.

## Usage Notes

- The scripts assume the project layout in which `AutoGen` and `openHiTLS` are sibling directories.
- Relative source paths supplied as arguments are interpreted relative to the `AutoGen` directory, not the caller’s working directory.
