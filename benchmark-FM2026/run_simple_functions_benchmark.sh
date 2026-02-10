#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

bash "$SCRIPT_DIR/gen_acsl.sh"
bash "$SCRIPT_DIR/preprocess.sh"
bash "$SCRIPT_DIR/verify_results.sh"
python3 "$SCRIPT_DIR/collect_successful_goals.py" \
  "$SCRIPT_DIR/verify_logs" \
  "$SCRIPT_DIR/fully_verified" \
  --source-root "$SCRIPT_DIR"
