#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCHMARK_IN="${BENCHMARK_IN:-$SCRIPT_DIR/benchmark}"
BENCHMARK_OUT="${BENCHMARK_OUT:-$SCRIPT_DIR/benchmark_with_acsl}"
CLEAN_BENCHMARK_OUT="${CLEAN_BENCHMARK_OUT:-1}"

# Make the preprocessing stage rerunnable by default. The copy script refuses
# to overwrite existing targets, so stale outputs must be removed first.
if [ "$CLEAN_BENCHMARK_OUT" = "1" ] && [ -d "$BENCHMARK_OUT" ]; then
  if [ "$BENCHMARK_OUT" = "/" ] || [ -z "$BENCHMARK_OUT" ]; then
    echo "[ERROR] Refusing to remove unsafe BENCHMARK_OUT='$BENCHMARK_OUT'" >&2
    exit 2
  fi
  rm -rf "$BENCHMARK_OUT"
fi

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
