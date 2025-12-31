#!/bin/bash

# Run ACSLG + Frama-C WP for a single function.
# Usage:
#   ./run_single.sh BN_SetBit
#   ./run_single.sh BN_SetBit path/to/file.c

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Make Ctrl+C stop the whole run (including child processes).
trap 'echo "[INFO] Interrupted, exiting..."; kill -- -$$ 2>/dev/null; exit 130' INT TERM

if [ $# -lt 1 ]; then
  echo "Usage: $0 <FunctionName> [path/to/source.c]" >&2
  exit 1
fi

FUNC="$1"

# Build ACSLG unless SKIP_BUILD=1
if [ "${SKIP_BUILD:-0}" != "1" ]; then
  "$SCRIPT_DIR/compile.sh"
fi

OPENHITLS_ROOT="${OPENHITLS_ROOT:-}"
if [ -z "$OPENHITLS_ROOT" ]; then
  if [ -d "$ROOT_DIR/../openhitls" ]; then
    OPENHITLS_ROOT="$ROOT_DIR/../openhitls"
  elif [ -d "$ROOT_DIR/../openHiTLS" ]; then
    OPENHITLS_ROOT="$ROOT_DIR/../openHiTLS"
  else
    echo "[ERROR] Cannot locate OpenHiTLS root; set OPENHITLS_ROOT=/abs/path/to/openhitls" >&2
    exit 2
  fi
fi
OPENHITLS_ROOT="$(realpath "$OPENHITLS_ROOT")"

# Prefer compilation database at project root; fall back to build dir.
if [ -f "$OPENHITLS_ROOT/compile_commands.json" ]; then
  COMP_DB_DIR="$OPENHITLS_ROOT"
elif [ -f "$OPENHITLS_ROOT/build/compile_commands.json" ]; then
  COMP_DB_DIR="$OPENHITLS_ROOT/build"
else
  echo "[ERROR] Cannot find compile_commands.json under $OPENHITLS_ROOT or $OPENHITLS_ROOT/build" >&2
  exit 3
fi

SRC_PATH="${2:-$OPENHITLS_ROOT/crypto/bn/src/bn_basic.c}"
if [[ "$SRC_PATH" != /* ]]; then
  SRC_PATH="$ROOT_DIR/$SRC_PATH"
fi
SRC_PATH="$(realpath "$SRC_PATH")"

src_dir=$(dirname "$SRC_PATH")
src_base=$(basename "$SRC_PATH")
src_stem="${src_base%.*}"
src_ext="${src_base##*.}"

ACSLG_TIMEOUT="${ACSLG_TIMEOUT:-30}"
WP_TIMEOUT="${WP_TIMEOUT:-60}"
KEEP_GENERATED="${KEEP_GENERATED:-success}"

RUN_TAG="${RUN_TAG:-$(date +%Y%m%d_%H%M%S)}"
RUN_DIR_DEFAULT="$ROOT_DIR/runlogs/single_${FUNC}_${RUN_TAG}"
RUN_DIR="${RUN_DIR_OVERRIDE:-$RUN_DIR_DEFAULT}"
LOG_DIR="${LOG_DIR_OVERRIDE:-$RUN_DIR/logs}"
GEN_DIR="${GEN_DIR_OVERRIDE:-$RUN_DIR/generated}"
RESULT_FILE="${RESULT_FILE_OVERRIDE:-$RUN_DIR/results.csv}"

mkdir -p "$LOG_DIR" "$GEN_DIR"

ACSLG_LOG="${LOG_DIR}/acslg_${FUNC}.log"
WP_LOG="${LOG_DIR}/wp_${FUNC}.log"

echo "[INFO] OpenHiTLS root:  $OPENHITLS_ROOT"
echo "[INFO] Comp DB dir:     $COMP_DB_DIR"
echo "[INFO] Source:          $SRC_PATH"
echo "[INFO] Function:        $FUNC"
echo "[INFO] Run dir:         $RUN_DIR"
echo

# Frama-C preprocessing command (same as experiment.sh)
CPP_CMD="gcc -C -E \
  -D__FRAMAC__ \
  -DHITLS_CRYPTO_BN \
  -DHITLS_SIXTY_FOUR_BITS \
  -DOPENHITLSDIR=\\\"/usr/local/\\\" \
  -I$OPENHITLS_ROOT/config/macro_config \
  -I$OPENHITLS_ROOT/include \
  -I$OPENHITLS_ROOT/include/bsl \
  -I$OPENHITLS_ROOT/include/crypto \
  -I$OPENHITLS_ROOT/crypto/include \
  -I$OPENHITLS_ROOT/crypto/bn/src \
  -I$OPENHITLS_ROOT/crypto/bn/include \
  -I$OPENHITLS_ROOT/platform/Secure_C/include \
  -I$OPENHITLS_ROOT/bsl/include \
  -I$OPENHITLS_ROOT/bsl/err/include \
  -I$OPENHITLS_ROOT/bsl/asn1/include \
  -I$OPENHITLS_ROOT/tls/include \
  -I$OPENHITLS_ROOT/include/tls \
  -I$OPENHITLS_ROOT/include/pki \
  -I$OPENHITLS_ROOT/include/auth"

echo "[INFO] Frama-C cpp-command:"
echo "       $CPP_CMD"
echo

with_acsl_new="$GEN_DIR/${src_stem}_acsl.${src_ext}"
with_acsl_old1="$GEN_DIR/${src_stem}_with_acsl.${src_ext}"
with_acsl_old2="$GEN_DIR/${src_stem}.${src_ext}_with_acsl"
with_acsl="$with_acsl_new"

rm -f "$with_acsl_new" "$with_acsl_old1" "$with_acsl_old2"

first_issue_line() {
  local log="$1"
  local issue
  issue="$(rg --text -m1 -n '\[(ERROR|UNIMPLEMENT)\b' "$log" || true)"
  if [ -z "$issue" ]; then
    issue="$(rg --text -m1 -n '\bAssertion\b|\bassertion\b' "$log" || true)"
  fi
  echo "$issue"
}

first_wp_issue_line() {
  local log="$1"
  local issue
  issue="$(rg --text -m1 -n 'annot-error|User Error:|fatal error:|Frama-C aborted:|warning .* treated as fatal' "$log" || true)"
  echo "$issue"
}

parse_wp_summary() {
  local wp_log="$1"
  if rg --text -q "\\[wp\\] Proved goals:" "$wp_log"; then
    local line proved total
    line="$(rg --text "\\[wp\\] Proved goals:" "$wp_log" | tail -n 1)"
    proved="$(echo "$line" | awk '{print $4}')"
    total="$(echo "$line" | awk '{print $6}')"
    echo "${proved},${total}"
    return 0
  fi
  echo "NA,NA"
  return 1
}

time_file="$(mktemp)"
set +e
timeout --signal=TERM --kill-after=5 "$ACSLG_TIMEOUT" \
  /usr/bin/time -f "%e" -o "$time_file" \
    bash -lc "ulimit -c 0; exec nice -n 10 \"$ROOT_DIR/build/src/ACSLG\" -p \"$COMP_DB_DIR\" \"$SRC_PATH\" --func \"$FUNC\" --out-dir \"$GEN_DIR\" --log-level warn --extra-arg=-D__FRAMAC__ --extra-arg=-DHITLS_CRYPTO_BN --extra-arg=-DHITLS_SIXTY_FOUR_BITS --extra-arg=-Wno-unknown-warning-option --extra-arg=-Wno-error=unknown-warning-option" \
      >"$ACSLG_LOG" 2>&1
acslg_rc=$?
set -e

acslg_time_raw="$(cat "$time_file" 2>/dev/null || true)"
rm -f "$time_file"
acslg_time="$(echo "$acslg_time_raw" | rg -o '^[0-9.]+$' | head -n 1 || true)"
if [ -z "$acslg_time" ]; then
  acslg_time="NA"
fi

issue="$(first_issue_line "$ACSLG_LOG")"
if [ "$acslg_rc" -eq 124 ]; then
  issue="TIMEOUT (${ACSLG_TIMEOUT}s)"
fi
issue="${issue//$'\t'/ }"
issue="${issue//$'\n'/ }"
issue="${issue//,/;}"

acslg_error="no"
if [ "$acslg_rc" -ne 0 ] || [ -n "$issue" ]; then
  acslg_error="yes"
fi

if [ ! -f "$with_acsl" ]; then
  if [ -f "$with_acsl_old1" ]; then
    mv -f "$with_acsl_old1" "$with_acsl"
  elif [ -f "$with_acsl_old2" ]; then
    mv -f "$with_acsl_old2" "$with_acsl"
  fi
fi

wp_proved="NA"
wp_total="NA"
wp_result="none"
wp_issue=""
if [ "$acslg_error" = "yes" ] || [ ! -f "$with_acsl" ]; then
  echo "[WARN] Skip WP: ACSLG failed or output missing (rc=$acslg_rc, file=$with_acsl)" >"$WP_LOG"
  wp_issue="skip (acslg_error=$acslg_error, file_missing=$([ -f "$with_acsl" ] && echo no || echo yes))"
else
  set +e
  timeout --signal=TERM --kill-after=5 "$WP_TIMEOUT" \
    bash -lc "ulimit -c 0; exec nice -n 10 frama-c -wp -wp-prover Qed -cpp-command \"$CPP_CMD\" \"$with_acsl\"" \
      >"$WP_LOG" 2>&1
  wp_rc=$?
  set -e

  if [ "$wp_rc" -eq 124 ]; then
    wp_result="timeout"
    wp_issue="TIMEOUT (${WP_TIMEOUT}s)"
  else
    summary="$(parse_wp_summary "$WP_LOG" || true)"
    wp_proved="${summary%%,*}"
    wp_total="${summary##*,}"
    if [ "$wp_proved" != "NA" ] && [ "$wp_total" != "NA" ]; then
      if [ "$wp_proved" = "$wp_total" ]; then
        wp_result="all"
      else
        wp_result="partial"
      fi
    else
      wp_result="none"
      wp_issue="$(first_wp_issue_line "$WP_LOG")"
    fi
  fi
fi

wp_issue="${wp_issue//$'\t'/ }"
wp_issue="${wp_issue//$'\n'/ }"
wp_issue="${wp_issue//,/;}"

echo "suite,source,function,acslg_time_sec,acslg_rc,acslg_error,acslg_issue,wp_proved,wp_total,wp_result,wp_issue" >"$RESULT_FILE"
echo "single,${SRC_PATH},${FUNC},${acslg_time},${acslg_rc},${acslg_error},${issue},${wp_proved},${wp_total},${wp_result},${wp_issue}" >>"$RESULT_FILE"

case "$KEEP_GENERATED" in
  1|all)
    : # keep everything
    ;;
  success)
    if [ ! -f "$with_acsl" ]; then
      rm -rf "$GEN_DIR"
    fi
    ;;
  0|none|"")
    rm -rf "$GEN_DIR"
    ;;
  *)
    echo "[WARN] Unknown KEEP_GENERATED value '$KEEP_GENERATED' (expected success|all|none); treating as none" >&2
    rm -rf "$GEN_DIR"
    ;;
esac

echo "[INFO] Logs:            $ACSLG_LOG , $WP_LOG"
echo "[INFO] Results:         $RESULT_FILE"
