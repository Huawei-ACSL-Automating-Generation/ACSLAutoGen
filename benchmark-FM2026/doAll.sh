#!/usr/bin/env bash

# Always run from the directory containing this script so relative paths work.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

bash gen_acsl.sh
bash preprocess.sh
bash verify_results.sh
python3 collect_successful_goals.py verify_logs fully_verified