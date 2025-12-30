#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# 1. 先编译 ACSLG
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

echo "[INFO] OpenHiTLS root:  $OPENHITLS_ROOT"
echo "[INFO] Comp DB dir:     $COMP_DB_DIR"
echo

# 2. Frama-C 使用的预处理命令（与你现在手动跑成功的版本一致）
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

echo "[INFO] Frama-C will use cpp-command:"
echo "       $CPP_CMD"
echo

# Resource limits (to avoid WSL2/Docker disk/CPU stalls).
ACSLG_TIMEOUT="${ACSLG_TIMEOUT:-30}"
WP_TIMEOUT="${WP_TIMEOUT:-60}"

# Whether to keep generated annotated files:
# - "success" (default): keep only when ACSLG successfully produced the output file
# - "1"/"all": keep for all attempted functions (may include empty dirs)
# - "0"/"none": keep nothing (best for disk usage)
KEEP_GENERATED="${KEEP_GENERATED:-success}"

# Allow overriding which suites to run:
#   SUITES_OVERRIDE="basic bincal noasm" ./example.sh
SUITES_OVERRIDE="${SUITES_OVERRIDE:-basic bincal noasm}"
read -r -a SUITES <<<"$SUITES_OVERRIDE"

# Run output locations.
RUN_TAG="${RUN_TAG:-$(date +%Y%m%d_%H%M%S)}"
RUN_DIR_DEFAULT="$ROOT_DIR/runlogs/example_bn_${RUN_TAG}"
RUN_DIR="${RUN_DIR_OVERRIDE:-$RUN_DIR_DEFAULT}"
LOG_DIR="${LOG_DIR_OVERRIDE:-$RUN_DIR/logs}"
GEN_DIR_BASE="${GEN_DIR_OVERRIDE:-$RUN_DIR/generated}"
RESULT_FILE="${RESULT_FILE_OVERRIDE:-$RUN_DIR/results.csv}"
REPORT_FILE="${REPORT_FILE_OVERRIDE:-$RUN_DIR/report.md}"

mkdir -p "$LOG_DIR" "$GEN_DIR_BASE"

# 3. bn_basic.c 中所有需要跑的函数名（默认使用固定白名单，避免全文件过大）
BASIC_FUNCS_MODE="${BASIC_FUNCS_MODE:-list}" # list | all
BASIC_FUNCS_LIST=(
  BN_Create
  BN_Destroy
  BN_Init
  BnVaild
  BN_CbCtxCreate
  BN_CbCtxSet
  BN_CbCtxGetArg
  BN_CbCtxCall
  BN_CbCtxDestroy
  BN_SetSign
  IsLegalFlag
  BN_SetFlag
  BN_Copy
  BN_Dup
  BN_IsZero
  BN_IsOne
  BN_IsNegative
  BN_IsOdd
  BN_IsFlag
  BN_Zeroize
  BN_IsLimb
  BN_SetLimb
  BN_GetLimb
  BN_GetBit
  BN_SetBit
  BN_ClrBit
  BN_MaskBit
  BN_Bits
  BN_Bytes
  BnExtend
  BN_SecBits
)

# Optional per-suite overrides:
#   BASIC_FUNCS_OVERRIDE="BN_Create BN_Destroy" ./example.sh
#   BINCAL_FUNCS_OVERRIDE="BinInc BinDec" ./example.sh
#   NOASM_BINCAL_FUNCS_OVERRIDE="BinAdd" ./example.sh
BASIC_FUNCS_OVERRIDE="${BASIC_FUNCS_OVERRIDE:-}"
BINCAL_FUNCS_OVERRIDE="${BINCAL_FUNCS_OVERRIDE:-}"
NOASM_BINCAL_FUNCS_OVERRIDE="${NOASM_BINCAL_FUNCS_OVERRIDE:-}"

extract_funcs() {
  local file="$1"
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
  ' "$file" | sort -u
}

first_issue_line() {
  local log="$1"
  local issue
  # Note: logs may include ANSI color prefixes before '['.
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

run_one() {
  local suite="$1"
  local src="$2"
  local func="$3"

  local src_base src_stem src_ext
  src_base="$(basename "$src")"
  src_stem="${src_base%.*}"
  src_ext="${src_base##*.}"

  local gen_dir="$GEN_DIR_BASE/$suite/$func"
  local with_acsl="$gen_dir/${src_stem}_with_acsl.${src_ext}"
  local acslg_log="$LOG_DIR/acslg_${suite}__${func}.log"
  local wp_log="$LOG_DIR/wp_${suite}__${func}.log"
  local time_file
  time_file="$(mktemp)"

  mkdir -p "$gen_dir"
  rm -f "$with_acsl"

  set +e
  timeout --signal=TERM --kill-after=5 "$ACSLG_TIMEOUT" \
    /usr/bin/time -f "%e" -o "$time_file" \
      bash -lc "ulimit -c 0; exec nice -n 10 \"$ROOT_DIR/build/src/ACSLG\" -p \"$COMP_DB_DIR\" \"$src\" --func \"$func\" --out-dir \"$gen_dir\" --log-level warn --extra-arg=-D__FRAMAC__ --extra-arg=-DHITLS_CRYPTO_BN --extra-arg=-DHITLS_SIXTY_FOUR_BITS --extra-arg=-Wno-unknown-warning-option --extra-arg=-Wno-error=unknown-warning-option" \
        >"$acslg_log" 2>&1
  local acslg_rc=$?
  set -e

  local acslg_time_raw acslg_time
  acslg_time_raw="$(cat "$time_file" 2>/dev/null || true)"
  rm -f "$time_file"
  acslg_time="$(echo "$acslg_time_raw" | rg -o '^[0-9.]+$' | head -n 1 || true)"
  if [ -z "$acslg_time" ]; then
    acslg_time="NA"
  fi

  local issue
  issue="$(first_issue_line "$acslg_log")"
  if [ "$acslg_rc" -eq 124 ]; then
    issue="TIMEOUT (${ACSLG_TIMEOUT}s)"
  fi
  issue="${issue//$'\t'/ }"
  issue="${issue//$'\n'/ }"
  issue="${issue//,/;}"

  local acslg_error="no"
  if [ "$acslg_rc" -ne 0 ] || [ -n "$issue" ]; then
    acslg_error="yes"
  fi

  local wp_rc=0 wp_proved="NA" wp_total="NA" wp_result="none"
  local wp_issue=""
  if [ "$acslg_error" = "yes" ] || [ ! -f "$with_acsl" ]; then
    echo "[WARN] Skip WP: ACSLG failed or output missing (rc=$acslg_rc, file=$with_acsl)" >"$wp_log"
    wp_rc=2
    wp_issue="skip (acslg_error=$acslg_error, file_missing=$([ -f "$with_acsl" ] && echo no || echo yes))"
  else
    set +e
    timeout --signal=TERM --kill-after=5 "$WP_TIMEOUT" \
      bash -lc "ulimit -c 0; exec nice -n 10 frama-c -wp -wp-prover Qed -cpp-command \"$CPP_CMD\" \"$with_acsl\"" \
        >"$wp_log" 2>&1
    wp_rc=$?
    set -e

    if [ "$wp_rc" -eq 124 ]; then
      wp_result="timeout"
      wp_issue="TIMEOUT (${WP_TIMEOUT}s)"
    else
      summary="$(parse_wp_summary "$wp_log" || true)"
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
        wp_issue="$(first_wp_issue_line "$wp_log")"
      fi
    fi
  fi

  wp_issue="${wp_issue//$'\t'/ }"
  wp_issue="${wp_issue//$'\n'/ }"
  wp_issue="${wp_issue//,/;}"

  echo "${suite},${src},${func},${acslg_time},${acslg_rc},${acslg_error},${issue},${wp_proved},${wp_total},${wp_result},${wp_issue}" >>"$RESULT_FILE"

  # Drop generated artifacts by default to reduce disk pressure (also removes empty dirs).
  case "$KEEP_GENERATED" in
    1|all)
      : # keep everything
      ;;
    success)
      # Keep only if ACSLG actually produced the annotated file.
      if [ ! -f "$with_acsl" ]; then
        rm -rf "$gen_dir"
      fi
      ;;
    0|none|"")
      rm -rf "$gen_dir"
      ;;
    *)
      echo "[WARN] Unknown KEEP_GENERATED value '$KEEP_GENERATED' (expected success|all|none); treating as none" >&2
      rm -rf "$gen_dir"
      ;;
  esac
}

# CSV header
mkdir -p "$(dirname "$RESULT_FILE")"
echo "suite,source,function,acslg_time_sec,acslg_rc,acslg_error,acslg_issue,wp_proved,wp_total,wp_result,wp_issue" >"$RESULT_FILE"

suite_src() {
  local suite="$1"
  case "$suite" in
    basic) echo "$OPENHITLS_ROOT/crypto/bn/src/bn_basic.c" ;;
    bincal) echo "$OPENHITLS_ROOT/crypto/bn/src/bn_bincal.c" ;;
    noasm) echo "$OPENHITLS_ROOT/crypto/bn/src/noasm_bn_bincal.c" ;;
    *)
      echo ""
      return 1
      ;;
  esac
}

suite_funcs() {
  local suite="$1"
  local src="$2"
  case "$suite" in
    basic)
      if [ -n "$BASIC_FUNCS_OVERRIDE" ]; then
        echo "$BASIC_FUNCS_OVERRIDE"
        return 0
      fi
      if [ "$BASIC_FUNCS_MODE" = "all" ]; then
        extract_funcs "$src" | tr '\n' ' '
        return 0
      fi
      printf "%s " "${BASIC_FUNCS_LIST[@]}"
      return 0
      ;;
    bincal)
      if [ -n "$BINCAL_FUNCS_OVERRIDE" ]; then
        echo "$BINCAL_FUNCS_OVERRIDE"
        return 0
      fi
      extract_funcs "$src" | tr '\n' ' '
      return 0
      ;;
    noasm)
      if [ -n "$NOASM_BINCAL_FUNCS_OVERRIDE" ]; then
        echo "$NOASM_BINCAL_FUNCS_OVERRIDE"
        return 0
      fi
      extract_funcs "$src" | tr '\n' ' '
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

echo "[INFO] Run dir:        $RUN_DIR"
echo "[INFO] Result CSV:     $RESULT_FILE"
echo "[INFO] Report:         $REPORT_FILE"
echo "[INFO] Suites:         ${SUITES[*]}"
echo

for suite in "${SUITES[@]}"; do
  src="$(suite_src "$suite")"
  if [ -z "$src" ] || [ ! -f "$src" ]; then
    echo "[WARN] Skip suite '$suite': source not found ($src)" >&2
    continue
  fi

  funcs_str="$(suite_funcs "$suite" "$src")"
  read -r -a funcs <<<"$funcs_str"
  echo "=== Suite: $suite ==="
  echo "[INFO] Source: $src"
  echo "[INFO] Funcs:  ${#funcs[@]}"

  for func in "${funcs[@]}"; do
    [ -z "$func" ] && continue
    echo "  -> $func"
    run_one "$suite" "$src" "$func"
  done
  echo
done

# Clean empty generated directories (including failed cases).
find "$GEN_DIR_BASE" -mindepth 1 -type d -empty -delete 2>/dev/null || true

python3 - <<'PY' "$RESULT_FILE" "$REPORT_FILE"
import csv
import sys
from collections import defaultdict
import os
import re

csv_path, out_path = sys.argv[1], sys.argv[2]
rows = []
with open(csv_path, newline="") as f:
    r = csv.DictReader(f)
    for row in r:
        rows.append(row)

by_suite = defaultdict(list)
for row in rows:
    by_suite[row["suite"]].append(row)

ansi_re = re.compile(r"\x1b\[[0-9;]*m")
def strip_ansi(s: str) -> str:
    return ansi_re.sub("", s or "")

def to_int(x):
    try:
        return int(x)
    except Exception:
        return None

lines = []
lines.append("# ACSLG + Frama-C WP Report")
lines.append("")
lines.append(f"- CSV: `{csv_path}`")
lines.append(f"- Total rows: {len(rows)}")
lines.append(f"- ACSLG timeout: {os.environ.get('ACSLG_TIMEOUT','') or 'NA'}s")
lines.append(f"- WP timeout: {os.environ.get('WP_TIMEOUT','') or 'NA'}s")
lines.append(f"- KEEP_GENERATED: {os.environ.get('KEEP_GENERATED','success')}")
lines.append("")

for suite, srows in sorted(by_suite.items()):
    total = len(srows)
    acslg_err = sum(1 for r in srows if r["acslg_error"] == "yes")
    wp_all = sum(1 for r in srows if r["wp_result"] == "all")
    wp_partial = sum(1 for r in srows if r["wp_result"] == "partial")
    wp_none = sum(1 for r in srows if r["wp_result"] in ("none", "timeout"))
    lines.append(f"## {suite}")
    lines.append(f"- Total functions: {total}")
    lines.append(f"- ACSLG failures:  {acslg_err}")
    lines.append(f"- WP all proved:   {wp_all}")
    lines.append(f"- WP partial:      {wp_partial}")
    lines.append(f"- WP none/timeout: {wp_none}")
    lines.append("")

    # Failure list
    fails = [r for r in srows if r["acslg_error"] == "yes"]
    if fails:
        lines.append("### ACSLG Failures")
        for r in fails:
            issue = strip_ansi((r.get("acslg_issue") or "").strip())
            if not issue:
                issue = "(no issue line)"
            lines.append(f"- `{r['function']}`: {issue}")
        lines.append("")

    # WP none list (excluding ACSLG failures)
    wp_bad = [r for r in srows if r["acslg_error"] == "no" and r["wp_result"] in ("none", "timeout")]
    if wp_bad:
        lines.append("### WP Missing/Timeout (ACSLG OK)")
        for r in wp_bad:
            wp_issue = strip_ansi((r.get("wp_issue") or "").strip())
            suffix = f" ({wp_issue})" if wp_issue else ""
            lines.append(f"- `{r['function']}`: wp_result={r['wp_result']}{suffix}")
        lines.append("")

with open(out_path, "w", encoding="utf-8") as f:
    f.write("\n".join(lines).rstrip() + "\n")
PY

echo "[INFO] Done."
