#!/bin/bash
#
# Sweep all function definitions in a C file with ACSLG (ACSLG only; no Frama-C),
# producing one log per function and a TSV summary.
#
# Usage:
#   ./scripts/sweep_acslg_only.sh path/to/file.c
#   ./scripts/sweep_acslg_only.sh --timeout 30 path/to/file.c
#   OPENHITLS_ROOT=/abs/path/to/openhitls ./scripts/sweep_acslg_only.sh path/to/file.c
#
# Notes (safety for WSL2/Docker):
# - Uses `--no-output` to avoid writing *_with_acsl.c next to the source.
# - Uses `--log-level=warn` to reduce log volume.
# - Disables core dumps (`ulimit -c 0`) and runs with lower priority (`nice -n 10`).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ $# -lt 1 ]; then
  echo "Usage: $0 [--timeout <seconds>] <path/to/source.c>" >&2
  exit 1
fi

TIMEOUT_SECS="${PER_FUNC_TIMEOUT:-}"

if [ "${1:-}" = "--timeout" ]; then
  if [ $# -lt 3 ]; then
    echo "Usage: $0 --timeout <seconds> <path/to/source.c>" >&2
    exit 1
  fi
  TIMEOUT_SECS="$2"
  shift 2
fi

SRC_PATH="$1"
if [[ "$SRC_PATH" != /* ]]; then
  SRC_PATH="$ROOT_DIR/$SRC_PATH"
fi
SRC_PATH="$(realpath "$SRC_PATH")"

if [ ! -f "$SRC_PATH" ]; then
  echo "[ERROR] Source file not found: $SRC_PATH" >&2
  exit 2
fi

OPENHITLS_ROOT="${OPENHITLS_ROOT:-}"
if [ -z "$OPENHITLS_ROOT" ]; then
  if [ -d "$ROOT_DIR/../openhitls" ]; then
    OPENHITLS_ROOT="$ROOT_DIR/../openhitls"
  elif [ -d "$ROOT_DIR/../openHiTLS" ]; then
    OPENHITLS_ROOT="$ROOT_DIR/../openHiTLS"
  else
    echo "[ERROR] Cannot locate OpenHiTLS root; set OPENHITLS_ROOT=/abs/path/to/openhitls" >&2
    exit 3
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
  exit 4
fi

src_base="$(basename "$SRC_PATH")"
src_stem="${src_base%.*}"

OUT_DIR="$ROOT_DIR/runlogs/acslg_sweep_${src_stem}_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$OUT_DIR/logs"

SUMMARY="$OUT_DIR/summary.tsv"
printf "file\tfunc\texit\tstatus\tissue_or_reason\n" >"$SUMMARY"

extract_funcs() {
  # Heuristic: line ending with ')' (not prototype ';'), followed by a '{' on the next non-empty line.
  awk '
    function trim(s){ sub(/^[ \t]+/,"",s); sub(/[ \t]+$/,"",s); return s }
    BEGIN{pending=0; name=""}
    {
      line=$0

      if (pending) {
        if (line ~ /^[ \t]*\{[ \t]*$/) { print name; pending=0; name=""; next }
        if (trim(line)=="" || line ~ /^[ \t]*\/\//) next
        pending=0; name=""
      }

      if (line ~ /^[ \t]*#/) next
      if (line ~ /\)[ \t]*$/ && line !~ /;[ \t]*$/) {
        sig=line
        sub(/\(.*/,"",sig)
        sig=trim(sig)
        n=split(sig, a, /[ \t\*]+/)
        if (n>0) { name=a[n]; pending=1 }
      }
    }
  ' "$SRC_PATH" | sort -u
}

FUNCS="$(extract_funcs)"
if [ -z "$FUNCS" ]; then
  echo "[ERROR] No function definitions found in $SRC_PATH" >&2
  exit 5
fi

echo "[INFO] Source:        $SRC_PATH"
echo "[INFO] Comp DB dir:   $COMP_DB_DIR"
echo "[INFO] Output dir:    $OUT_DIR"
echo "[INFO] Functions:     $(echo "$FUNCS" | wc -l)"
echo

run_one() {
  local func="$1"
  local log="$OUT_DIR/logs/${func}.log"

  set +e
  if [ -n "$TIMEOUT_SECS" ]; then
    timeout --signal=TERM --kill-after=5 "$TIMEOUT_SECS" \
      bash -lc "ulimit -c 0; exec nice -n 10 \"$ROOT_DIR/build/src/ACSLG\" -p \"$COMP_DB_DIR\" \"$SRC_PATH\" --func \"$func\" --no-output --log-level=warn --extra-arg=-D__FRAMAC__ --extra-arg=-Wno-unknown-warning-option --extra-arg=-Wno-error=unknown-warning-option" \
      >"$log" 2>&1
    code=$?
  else
    bash -lc "ulimit -c 0; exec nice -n 10 \"$ROOT_DIR/build/src/ACSLG\" -p \"$COMP_DB_DIR\" \"$SRC_PATH\" --func \"$func\" --no-output --log-level=warn --extra-arg=-D__FRAMAC__ --extra-arg=-Wno-unknown-warning-option --extra-arg=-Wno-error=unknown-warning-option" \
      >"$log" 2>&1
    code=$?
  fi
  set -e

  local status="FAIL"
  [ "$code" -eq 0 ] && status="OK"

  local issue
  issue="$(rg -m1 -n "\\[(UNIMPLEMENT|ERROR)\\b" "$log" || true)"
  if [ -z "$issue" ] && [ "$code" -ne 0 ]; then
    issue="$(rg -m1 -n "\\bAssertion\\b|\\bassertion\\b" "$log" || true)"
  fi
  if [ -z "$issue" ] && [ "$code" -eq 124 ]; then
    issue="TIMEOUT (${TIMEOUT_SECS}s)"
  fi
  if [ -z "$issue" ] && [ "$code" -ne 0 ]; then
    issue="(non-zero exit; no [ERROR]/[UNIMPLEMENT] in log)"
  fi
  issue="${issue//$'\t'/ }"

  printf "%s\t%s\t%s\t%s\t%s\n" "$SRC_PATH" "$func" "$code" "$status" "$issue" >>"$SUMMARY"
}

while IFS= read -r fn; do
  [ -z "$fn" ] && continue
  echo "[sweep] $fn"
  run_one "$fn"
  sleep 0.1
done <<<"$FUNCS"

echo
echo "[INFO] Done."
echo "[INFO] Summary: $SUMMARY"
