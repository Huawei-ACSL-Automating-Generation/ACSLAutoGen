#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Make Ctrl+C stop the whole benchmark (including child processes).
trap 'echo "[INFO] Interrupted, exiting..."; kill -- -$$ 2>/dev/null; exit 130' INT TERM

# 1. Build ACSLG first
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

# 2. Frama-C preprocessing command (matches the known-good manual setup)
COMMON_CPP_DEFINES="-D__FRAMAC__ -DHITLS_SIXTY_FOUR_BITS -DOPENHITLSDIR=\\\"/usr/local/\\\""
CPP_CMD_BASE="gcc -C -E \
  $COMMON_CPP_DEFINES \
  -include $OPENHITLS_ROOT/include/crypto/crypt_types.h \
  -I$OPENHITLS_ROOT/config/macro_config \
  -I$OPENHITLS_ROOT/include \
  -I$OPENHITLS_ROOT/include/bsl \
  -I$OPENHITLS_ROOT/include/crypto \
  -I$OPENHITLS_ROOT/crypto/include \
  -I$OPENHITLS_ROOT/crypto/bn/src \
  -I$OPENHITLS_ROOT/crypto/bn/include \
  -I$OPENHITLS_ROOT/crypto/mlkem/include \
  -I$OPENHITLS_ROOT/crypto/mlkem/src \
  -I$OPENHITLS_ROOT/crypto/mldsa/include \
  -I$OPENHITLS_ROOT/crypto/mldsa/src \
  -I$OPENHITLS_ROOT/crypto/slh_dsa/include \
  -I$OPENHITLS_ROOT/crypto/slh_dsa/src \
  -I$OPENHITLS_ROOT/crypto/frodokem/include \
  -I$OPENHITLS_ROOT/crypto/frodokem/src \
  -I$OPENHITLS_ROOT/crypto/xmss/include \
  -I$OPENHITLS_ROOT/crypto/xmss/src \
  -I$OPENHITLS_ROOT/crypto/eal/include \
  -I$OPENHITLS_ROOT/crypto/eal/src \
  -I$OPENHITLS_ROOT/platform/Secure_C/include \
  -I$OPENHITLS_ROOT/bsl/include \
  -I$OPENHITLS_ROOT/bsl/obj/include \
  -I$OPENHITLS_ROOT/bsl/err/include \
  -I$OPENHITLS_ROOT/bsl/asn1/include \
  -I$OPENHITLS_ROOT/tls/include \
  -I$OPENHITLS_ROOT/include/tls \
  -I$OPENHITLS_ROOT/include/pki \
  -I$OPENHITLS_ROOT/include/auth"

echo "[INFO] Frama-C will use cpp-command base:"
echo "       $CPP_CMD_BASE"
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
#   SUITES_OVERRIDE="basic bincal noasm" ./experiment.sh
SUITES_OVERRIDE="${SUITES_OVERRIDE:-basic bincal noasm mlkem mldsa slh_dsa frodokem quantum}"
read -r -a SUITES <<<"$SUITES_OVERRIDE"

# Run output locations.
RUN_TAG="${RUN_TAG:-$(date +%Y%m%d_%H%M%S)}"
RUN_DIR_DEFAULT="$ROOT_DIR/runlogs/example_bn_${RUN_TAG}"
RUN_DIR="${RUN_DIR_OVERRIDE:-$RUN_DIR_DEFAULT}"
LOG_DIR="${LOG_DIR_OVERRIDE:-$RUN_DIR/logs}"
GEN_DIR_BASE="${GEN_DIR_OVERRIDE:-$RUN_DIR/generated}"
RESULT_FILE="${RESULT_FILE_OVERRIDE:-$RUN_DIR/results.csv}"
REPORT_FILE="${REPORT_FILE_OVERRIDE:-$RUN_DIR/report.md}"
RESULT_TABLE_FILE="${RESULT_TABLE_FILE_OVERRIDE:-$RUN_DIR/bn_wp_results.txt}"

mkdir -p "$LOG_DIR" "$GEN_DIR_BASE"

# 3. Functions to run in bn_basic.c (default whitelist to avoid oversized files)
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

MLKEM_FUNCS_LIST=(
  CRYPT_ML_KEM_NewCtx
  CRYPT_ML_KEM_NewCtxEx
  CRYPT_ML_KEM_FreeCtx
  CRYPT_ML_KEM_GetSecBits
)

MLDSA_FUNCS_LIST=(
  CRYPT_ML_DSA_GetInfo
  CRYPT_ML_DSA_NewCtx
  CRYPT_ML_DSA_NewCtxEx
  CRYPT_ML_DSA_FreeCtx
  CRYPT_ML_DSA_DupCtx
)

SLH_DSA_FUNCS_LIST=(
  CRYPT_SLH_DSA_NewCtx
  CRYPT_SLH_DSA_FreeCtx
  CRYPT_SLH_DSA_DupCtx
  CRYPT_SLH_DSA_GetPubKey
  CRYPT_SLH_DSA_SetPubKey
)

FRODOKEM_FUNCS_LIST=(
  CRYPT_FRODOKEM_NewCtx
  CRYPT_FRODOKEM_NewCtxEx
  CRYPT_FRODOKEM_FreeCtx
  CRYPT_FRODOKEM_EncapsInit
  CRYPT_FRODOKEM_DecapsInit
)

QUANTUM_MLKEM_FUNCS_LIST=(
)

QUANTUM_MLDSA_FUNCS_LIST=(
)

QUANTUM_SLH_DSA_FUNCS_LIST=(
  UCAdrsGetAdrsLen
  CAdrsGetAdrsLen
)

QUANTUM_FRODOKEM_FUNCS_LIST=(
  CRYPT_FRODOKEM_EncapsInit
  CRYPT_FRODOKEM_DecapsInit
)

QUANTUM_FRODOKEM_UTIL_FUNCS_LIST=(
)

QUANTUM_FRODOKEM_PARAMS_FUNCS_LIST=(
)

QUANTUM_XMSS_FUNCS_LIST=(
  XAdrsGetAdrsLen
  CheckNotXmssAlgId
)

# Optional per-suite overrides:
#   BASIC_FUNCS_OVERRIDE="BN_Create BN_Destroy" ./experiment.sh
#   BINCAL_FUNCS_OVERRIDE="BinInc BinDec" ./experiment.sh
#   NOASM_BINCAL_FUNCS_OVERRIDE="BinAdd" ./experiment.sh
BASIC_FUNCS_OVERRIDE="${BASIC_FUNCS_OVERRIDE:-}"
BINCAL_FUNCS_OVERRIDE="${BINCAL_FUNCS_OVERRIDE:-}"
NOASM_BINCAL_FUNCS_OVERRIDE="${NOASM_BINCAL_FUNCS_OVERRIDE:-}"
MLKEM_FUNCS_OVERRIDE="${MLKEM_FUNCS_OVERRIDE:-}"
MLDSA_FUNCS_OVERRIDE="${MLDSA_FUNCS_OVERRIDE:-}"
SLH_DSA_FUNCS_OVERRIDE="${SLH_DSA_FUNCS_OVERRIDE:-}"
FRODOKEM_FUNCS_OVERRIDE="${FRODOKEM_FUNCS_OVERRIDE:-}"

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

suite_defines() {
  local suite="$1"
  case "$suite" in
    basic|bincal|noasm) echo "-DHITLS_CRYPTO_BN" ;;
    mlkem) echo "-DHITLS_CRYPTO_MLKEM" ;;
    mldsa) echo "-DHITLS_CRYPTO_MLDSA" ;;
    slh_dsa) echo "-DHITLS_CRYPTO_SLH_DSA" ;;
    frodokem) echo "-DHITLS_CRYPTO_FRODOKEM" ;;
    quantum) echo "-DHITLS_CRYPTO_MLKEM -DHITLS_CRYPTO_MLDSA -DHITLS_CRYPTO_SLH_DSA -DHITLS_CRYPTO_FRODOKEM -DHITLS_CRYPTO_XMSS" ;;
    *) echo "" ;;
  esac
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
  local with_acsl_new="$gen_dir/${src_stem}_acsl.${src_ext}"
  local with_acsl_old1="$gen_dir/${src_stem}_with_acsl.${src_ext}"
  local with_acsl_old2="$gen_dir/${src_stem}.${src_ext}_with_acsl"
  local with_acsl="$with_acsl_new"
  local acslg_log="$LOG_DIR/acslg_${suite}__${func}.log"
  local wp_log="$LOG_DIR/wp_${suite}__${func}.log"
  local time_file
  time_file="$(mktemp)"

  mkdir -p "$gen_dir"
  rm -f "$with_acsl_new" "$with_acsl_old1" "$with_acsl_old2"

  local suite_defs
  local acslg_extra_args
  local cpp_cmd
  local acslg_extra_arg_str

  suite_defs="$(suite_defines "$suite")"
  cpp_cmd="$CPP_CMD_BASE"
  if [ -n "$suite_defs" ]; then
    cpp_cmd="$cpp_cmd $suite_defs"
  fi

  acslg_extra_args=(
    --extra-arg=-D__FRAMAC__
    --extra-arg=-DHITLS_SIXTY_FOUR_BITS
    --extra-arg=-Wno-unknown-warning-option
    --extra-arg=-Wno-error=unknown-warning-option
  )
  if [ -n "$suite_defs" ]; then
    read -r -a suite_def_arr <<<"$suite_defs"
    for def in "${suite_def_arr[@]}"; do
      acslg_extra_args+=(--extra-arg="$def")
    done
  fi
  acslg_extra_arg_str="${acslg_extra_args[*]}"

  set +e
  timeout --signal=TERM --kill-after=5 "$ACSLG_TIMEOUT" \
    /usr/bin/time -f "%e" -o "$time_file" \
      bash -lc "ulimit -c 0; exec nice -n 10 \"$ROOT_DIR/build/src/ACSLG\" -p \"$COMP_DB_DIR\" \"$src\" --func \"$func\" --out-dir \"$gen_dir\" --log-level warn $acslg_extra_arg_str" \
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
  if [ ! -f "$with_acsl" ]; then
    if [ -f "$with_acsl_old1" ]; then
      mv -f "$with_acsl_old1" "$with_acsl"
    elif [ -f "$with_acsl_old2" ]; then
      mv -f "$with_acsl_old2" "$with_acsl"
    fi
  fi
  if [ "$acslg_error" = "yes" ] || [ ! -f "$with_acsl" ]; then
    echo "[WARN] Skip WP: ACSLG failed or output missing (rc=$acslg_rc, file=$with_acsl)" >"$wp_log"
    wp_rc=2
    wp_issue="skip (acslg_error=$acslg_error, file_missing=$([ -f "$with_acsl" ] && echo no || echo yes))"
  else
    set +e
    timeout --signal=TERM --kill-after=5 "$WP_TIMEOUT" \
      bash -lc "ulimit -c 0; exec nice -n 10 frama-c -wp -wp-prover Qed -cpp-command \"$cpp_cmd\" \"$with_acsl\"" \
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
    mlkem) echo "$OPENHITLS_ROOT/crypto/mlkem/src/ml_kem.c" ;;
    mldsa) echo "$OPENHITLS_ROOT/crypto/mldsa/src/ml_dsa.c" ;;
    slh_dsa) echo "$OPENHITLS_ROOT/crypto/slh_dsa/src/slh_dsa.c" ;;
    frodokem) echo "$OPENHITLS_ROOT/crypto/frodokem/src/frodokem.c" ;;
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
    mlkem)
      if [ -n "$MLKEM_FUNCS_OVERRIDE" ]; then
        echo "$MLKEM_FUNCS_OVERRIDE"
        return 0
      fi
      printf "%s " "${MLKEM_FUNCS_LIST[@]}"
      return 0
      ;;
    mldsa)
      if [ -n "$MLDSA_FUNCS_OVERRIDE" ]; then
        echo "$MLDSA_FUNCS_OVERRIDE"
        return 0
      fi
      printf "%s " "${MLDSA_FUNCS_LIST[@]}"
      return 0
      ;;
    slh_dsa)
      if [ -n "$SLH_DSA_FUNCS_OVERRIDE" ]; then
        echo "$SLH_DSA_FUNCS_OVERRIDE"
        return 0
      fi
      printf "%s " "${SLH_DSA_FUNCS_LIST[@]}"
      return 0
      ;;
    frodokem)
      if [ -n "$FRODOKEM_FUNCS_OVERRIDE" ]; then
        echo "$FRODOKEM_FUNCS_OVERRIDE"
        return 0
      fi
      printf "%s " "${FRODOKEM_FUNCS_LIST[@]}"
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

quantum_entries() {
  local entries=()
  if [ ${#QUANTUM_MLKEM_FUNCS_LIST[@]} -gt 0 ]; then
    entries+=("$OPENHITLS_ROOT/crypto/mlkem/src/ml_kem.c|${QUANTUM_MLKEM_FUNCS_LIST[*]}")
  fi
  if [ ${#QUANTUM_MLDSA_FUNCS_LIST[@]} -gt 0 ]; then
    entries+=("$OPENHITLS_ROOT/crypto/mldsa/src/ml_dsa.c|${QUANTUM_MLDSA_FUNCS_LIST[*]}")
  fi
  if [ ${#QUANTUM_SLH_DSA_FUNCS_LIST[@]} -gt 0 ]; then
    entries+=("$OPENHITLS_ROOT/crypto/slh_dsa/src/slh_dsa.c|${QUANTUM_SLH_DSA_FUNCS_LIST[*]}")
  fi
  if [ ${#QUANTUM_FRODOKEM_FUNCS_LIST[@]} -gt 0 ]; then
    entries+=("$OPENHITLS_ROOT/crypto/frodokem/src/frodokem.c|${QUANTUM_FRODOKEM_FUNCS_LIST[*]}")
  fi
  if [ ${#QUANTUM_FRODOKEM_UTIL_FUNCS_LIST[@]} -gt 0 ]; then
    entries+=("$OPENHITLS_ROOT/crypto/frodokem/src/frodo_util.c|${QUANTUM_FRODOKEM_UTIL_FUNCS_LIST[*]}")
  fi
  if [ ${#QUANTUM_FRODOKEM_PARAMS_FUNCS_LIST[@]} -gt 0 ]; then
    entries+=("$OPENHITLS_ROOT/crypto/frodokem/src/frodo_params.c|${QUANTUM_FRODOKEM_PARAMS_FUNCS_LIST[*]}")
  fi
  if [ ${#QUANTUM_XMSS_FUNCS_LIST[@]} -gt 0 ]; then
    entries+=("$OPENHITLS_ROOT/crypto/xmss/src/xmss.c|${QUANTUM_XMSS_FUNCS_LIST[*]}")
  fi
  printf "%s\n" "${entries[@]}"
}

echo "[INFO] Run dir:        $RUN_DIR"
echo "[INFO] Result CSV:     $RESULT_FILE"
echo "[INFO] Report:         $REPORT_FILE"
echo "[INFO] Result table:   $RESULT_TABLE_FILE"
echo "[INFO] Suites:         ${SUITES[*]}"
echo

for suite in "${SUITES[@]}"; do
  if [ "$suite" = "quantum" ]; then
    echo "=== Suite: $suite ==="
    while IFS= read -r entry; do
      [ -z "$entry" ] && continue
      src="${entry%%|*}"
      funcs_str="${entry#*|}"
      if [ -z "$src" ] || [ ! -f "$src" ]; then
        echo "[WARN] Skip quantum entry: source not found ($src)" >&2
        continue
      fi
      read -r -a funcs <<<"$funcs_str"
      echo "[INFO] Source: $src"
      echo "[INFO] Funcs:  ${#funcs[@]}"
      for func in "${funcs[@]}"; do
        [ -z "$func" ] && continue
        echo "  -> $func"
        run_one "$suite" "$src" "$func"
      done
      echo
    done < <(quantum_entries)
    continue
  fi

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

# Generate the Markdown report from results.csv (suite summary + failure lists).
# Note: This also writes the plain-text table to RESULT_TABLE_FILE for quick scanning.
python3 "$SCRIPT_DIR/report_summary.py" "$RESULT_FILE" "$REPORT_FILE" "$RESULT_TABLE_FILE"

# The report generator above now replaces the old awk table build.
# RESULT_TABLE_FILE is produced alongside REPORT_FILE.
echo "[INFO] Done."
