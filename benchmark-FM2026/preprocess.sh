#!/usr/bin/env bash

# Copy *_with_acsl* files to a mirror tree, stripping the suffix.
python3 preprocess-scripts/strip_with_acsl_suffix.py benchmark benchmark_with_acsl

# Convert asserts in the copied tree into ACSL assertions.
python3 preprocess-scripts/convert_asserts_to_acsl.py benchmark_with_acsl

# Add missing forward declarations for stub helpers with assigns \nothing.
python3 preprocess-scripts/ensure_unknown_decls.py benchmark_with_acsl

# Prepend assigns \nothing to unknown* prototypes that lack it.
python3 preprocess-scripts/ensure_unknown_assigns.py benchmark_with_acsl

# Merge consecutive ACSL comment blocks so annotations stay contiguous.
python3 preprocess-scripts/merge_split_acsl.py benchmark_with_acsl
