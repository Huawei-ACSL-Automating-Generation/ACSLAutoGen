# ACSLAutoGen / ACSLGen Tool Guide

## Overview
- ACSLGen is a Clang-based CLI tool that automatically generates ACSL specifications and annotations for C code.
- It consumes the project's compilation database (`compile_commands.json`) to mirror the original build flags and include paths.
- The tool writes annotated outputs next to the input source, using names like `foo.c_with_acsl`.
- A Docker environment is recommended; it bundles LLVM/Clang 19, Z3, GMP, PPL, and other dependencies to keep builds reproducible.

## Getting Started
- Requirements:
  - Linux host, Docker 20.10+.
  - For source builds: CMake ≥ 3.16 plus LLVM/Clang 19, Z3, and related deps (easiest inside the provided container).

- Use a prebuilt Docker image:
  1. Download the image archive (e.g., `acslg-env.tar.gz`) and extract: `tar -xzf acslg-env.tar.gz`
  2. Import the image: `docker load -i acslg-env.tar`
  3. Run a container and mount the repo for building/running:
     ```bash
     docker run --rm -it -v $(pwd):/workspace acslg-env bash
     ```

- Build the Docker image yourself (Dockerfile is at the repo root):
  ```bash
  docker build -t acslg-env .
  docker run --rm -it -v $(pwd):/workspace acslg-env bash
  ```

- Build from source (best run inside the container):
  ```bash
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  cmake --build build -j$(nproc)
  # To enable tests: cmake -S . -B build -DBUILD_TESTS=ON && cmake --build build --target test_all
  ```

## Usage
- Quick single-file run (simple cases): you can invoke ACSLGen directly without a compilation database:
  ```bash
  ./build/src/ACSLG path/to/source.c
  ```

- Project-aware run with a compilation database: prepare `compile_commands.json` (e.g., `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`) and point ACSLGen to that directory with `-p`:
  ```bash
  ./build/src/ACSLG -p build path/to/source.c
  ```
  - Append extra compiler flags after `--`, e.g. `-- -I/path/to/include -DDEBUG`.
  - Output is written alongside the source, named like `source.c_with_acsl`.
  - `-ast-only`: print the full AST for debugging instead of generating ACSL.
- Existing ACSL comments that are not top-level `requires` clauses are removed during rewrite to avoid conflicts with generated specs.
- Build and preview API docs:
  ```bash
  doxygen Doxyfile
  python -m http.server --directory docs/doxygen/html 8000
  ```

## Project Structure & Architecture
To be added; will link to the dedicated design document later.

## Citation & Credits
To be added.
