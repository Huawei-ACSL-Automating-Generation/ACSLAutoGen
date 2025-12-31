#!/bin/bash

# Run ACSLG + Frama-C WP for a single function in bn_basic.c (or a user-specified file).
# Usage:
#   ./run_single.sh BN_SetBit                     # uses default ../openHiTLS/crypto/bn/src/bn_basic.c
#   ./run_single.sh BN_SetBit path/to/file.c      # custom source file

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <FunctionName> [path/to/source.c]" >&2
    exit 1
fi

FUNC="$1"
SRC_PATH="${2:-$ROOT_DIR/../openHiTLS/crypto/bn/src/bn_basic.c}"
if [[ "$SRC_PATH" != /* ]]; then
    SRC_PATH="$ROOT_DIR/$SRC_PATH"
fi
SRC_ABS=$(realpath "$SRC_PATH")

src_dir=$(dirname "$SRC_PATH")
src_base=$(basename "$SRC_PATH")
src_stem="${src_base%.*}"
src_ext="${src_base##*.}"

# ACSLG output names: the tool emits "<stem>_acsl.<ext>"
# Keep fallbacks for older naming "<stem>_with_acsl.<ext>" and "<stem>.<ext>_with_acsl".
RAW_ACSL_PATH_NEW="${src_dir}/${src_stem}_acsl.${src_ext}"
RAW_ACSL_PATH_OLD1="${src_dir}/${src_stem}_with_acsl.${src_ext}"
RAW_ACSL_PATH_OLD2="${src_dir}/${src_stem}.${src_ext}_with_acsl"
WITH_ACSL_PATH="${RAW_ACSL_PATH_NEW}"

LOG_DIR="$ROOT_DIR/wp_logs_single"
mkdir -p "$LOG_DIR"

ACSLG_LOG="${LOG_DIR}/acslg_${FUNC}.log"
WP_LOG="${LOG_DIR}/wp_${FUNC}.log"

echo "[INFO] Source:          $SRC_PATH"
echo "[INFO] Raw ACSL file:   ${RAW_ACSL_PATH_NEW} (or ${RAW_ACSL_PATH_OLD1}/${RAW_ACSL_PATH_OLD2})"
echo "[INFO] Frama-C input:   $WITH_ACSL_PATH"
echo "[INFO] Logs:            $ACSLG_LOG , $WP_LOG"
echo

# Frama-C preprocessing command (same as example.sh)
CPP_CMD="gcc -C -E \
  -D__FRAMAC__ \
  -DHITLS_CRYPTO_BN \
  -DHITLS_SIXTY_FOUR_BITS \
  -DOPENHITLSDIR=\\\"/usr/local/\\\" \
  -I$ROOT_DIR/../openHiTLS/config/macro_config \
  -I$ROOT_DIR/../openHiTLS/include \
  -I$ROOT_DIR/../openHiTLS/include/bsl \
  -I$ROOT_DIR/../openHiTLS/include/crypto \
  -I$ROOT_DIR/../openHiTLS/crypto/include \
  -I$ROOT_DIR/../openHiTLS/crypto/bn/include \
  -I$ROOT_DIR/../openHiTLS/platform/Secure_C/include \
  -I$ROOT_DIR/../openHiTLS/bsl/include \
  -I$ROOT_DIR/../openHiTLS/bsl/err/include \
  -I$ROOT_DIR/../openHiTLS/bsl/asn1/include \
  -I$ROOT_DIR/../openHiTLS/tls/include \
  -I$ROOT_DIR/../openHiTLS/include/tls \
  -I$ROOT_DIR/../openHiTLS/include/pki \
  -I$ROOT_DIR/../openHiTLS/include/auth"

echo "[INFO] Frama-C cpp-command:"
echo "       $CPP_CMD"
echo

# Clean previous generated files
rm -f "$RAW_ACSL_PATH_NEW" "$RAW_ACSL_PATH_OLD1" "$RAW_ACSL_PATH_OLD2" "$WITH_ACSL_PATH"

# 1) Run ACSLG for the specific function
set +e
"$ROOT_DIR/build/src/ACSLG" \
  -extra-arg=-x -extra-arg=c \
  -extra-arg=-D__FRAMAC__ \
  -p "$ROOT_DIR/../openHiTLS/build" \
  "$SRC_PATH" \
  --func "$FUNC" \
  >"$ACSLG_LOG" 2>&1
acslg_rc=$?
set -e

# Locate the generated ACSL file
GEN_FILE=""
if [ -f "$RAW_ACSL_PATH_NEW" ]; then
    GEN_FILE="$RAW_ACSL_PATH_NEW"
elif [ -f "$RAW_ACSL_PATH_OLD1" ]; then
    GEN_FILE="$RAW_ACSL_PATH_OLD1"
elif [ -f "$RAW_ACSL_PATH_OLD2" ]; then
    GEN_FILE="$RAW_ACSL_PATH_OLD2"
fi

if [ $acslg_rc -ne 0 ] || [ -z "$GEN_FILE" ]; then
    echo "[ERROR] ACSLG failed; see $ACSLG_LOG (rc=$acslg_rc)" >&2
    exit 2
fi

if [ "$GEN_FILE" != "$WITH_ACSL_PATH" ]; then
    mv -f "$GEN_FILE" "$WITH_ACSL_PATH"
fi

# 2) Run Frama-C WP
set +e
frama-c -wp -wp-prover Qed \
  -cpp-command "$CPP_CMD" \
  "$WITH_ACSL_PATH" >"$WP_LOG" 2>&1
wp_rc=$?
set -e

if grep -q "\\[wp\\] Proved goals:" "$WP_LOG"; then
    summary_line="$(grep '\\[wp\\] Proved goals:' "$WP_LOG" | tail -n 1)"
    echo "[INFO] WP summary: $summary_line"
else
    echo "[WARN] No WP summary line found; frama-c rc=$wp_rc (see $WP_LOG)"
fi

exit $wp_rc
