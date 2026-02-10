#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCHMARK_IN="${BENCHMARK_IN:-$SCRIPT_DIR/benchmark}"
BENCHMARK_OUT="${BENCHMARK_OUT:-$SCRIPT_DIR/benchmark_with_acsl}"

# Copy *_with_acsl* files to a mirror tree, stripping the suffix.
python3 "$SCRIPT_DIR/preprocess-scripts/strip_with_acsl_suffix.py" "$BENCHMARK_IN" "$BENCHMARK_OUT"

# Convert asserts in the copied tree into ACSL assertions.
python3 "$SCRIPT_DIR/preprocess-scripts/convert_asserts_to_acsl.py" "$BENCHMARK_OUT"

# Add missing forward declarations for stub helpers with assigns \nothing.
python3 "$SCRIPT_DIR/preprocess-scripts/ensure_unknown_decls.py" "$BENCHMARK_OUT"

# Prepend assigns \nothing to unknown* prototypes that lack it.
python3 "$SCRIPT_DIR/preprocess-scripts/ensure_unknown_assigns.py" "$BENCHMARK_OUT"

# Merge consecutive ACSL comment blocks so annotations stay contiguous.
python3 "$SCRIPT_DIR/preprocess-scripts/merge_split_acsl.py" "$BENCHMARK_OUT"
