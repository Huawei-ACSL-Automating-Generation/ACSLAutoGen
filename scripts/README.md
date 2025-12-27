# Scripts

This directory consolidates the project’s executable helper scripts. Each script is designed to be invoked from any working directory; all paths are resolved relative to the script’s own location to avoid assumptions about the caller’s current directory.

## Script Descriptions

- **compile.sh**: Configures and builds the AutoGen project (Debug build) in `AutoGen/build/` and generates compile commands for downstream tooling.
- **frama_cpp_wrapper.sh**: Provides a GCC preprocessing wrapper for Frama-C by supplying the required include paths and feature macros from the neighboring openHiTLS tree.
- **print-ast.sh**: Builds the project and runs ACSLG in AST-only mode on `AutoGen/test.c` to inspect the parsed syntax tree.
- **run-tests.sh**: Configures and builds the test targets, then executes the test suite via CTest with failure diagnostics.
- **example.sh**: Executes the ACSLG + Frama-C WP pipeline over a predefined set of `bn_basic.c` functions, capturing per-function logs and writing a CSV summary to `AutoGen/bn_basic_wp_results.txt`.
- **run_single.sh**: Executes the ACSLG + Frama-C WP pipeline for a single function and stores logs under `AutoGen/wp_logs_single/`.

## Usage Notes

- The scripts assume the project layout in which `AutoGen` and `openHiTLS` are sibling directories.
- Relative source paths supplied as arguments are interpreted relative to the `AutoGen` directory, not the caller’s working directory.
