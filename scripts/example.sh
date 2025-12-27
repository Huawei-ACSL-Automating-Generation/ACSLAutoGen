#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# 1. 先编译 ACSLG
"$SCRIPT_DIR/compile.sh"

DEFAULT_PATH="$ROOT_DIR/../openHiTLS/crypto/bn/src/bn_basic.c"
SRC_PATH=${1:-$DEFAULT_PATH}
if [[ "$SRC_PATH" != /* ]]; then
  SRC_PATH="$ROOT_DIR/$SRC_PATH"
fi
SRC_ABS=$(realpath "$SRC_PATH")

src_dir=$(dirname "$SRC_PATH")
src_base=$(basename "$SRC_PATH")         # bn_basic.c
src_stem="${src_base%.*}"                # bn_basic
src_ext="${src_base##*.}"                # c

# ACSLG 原始生成的文件：bn_basic.c_with_acsl
RAW_ACSL_PATH="${src_dir}/${src_stem}.${src_ext}_with_acsl"
# 我们希望 Frama-C 读取的文件：bn_basic_with_acsl.c
WITH_ACSL_PATH="${src_dir}/${src_stem}_with_acsl.${src_ext}"

echo "[INFO] Source:          $SRC_PATH"
echo "[INFO] Raw ACSL file:   $RAW_ACSL_PATH"
echo "[INFO] Frama-C input:   $WITH_ACSL_PATH"
echo

# 2. Frama-C 使用的预处理命令（与你现在手动跑成功的版本一致）
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

echo "[INFO] Frama-C will use cpp-command:"
echo "       $CPP_CMD"
echo

# 3. bn_basic.c 中所有需要跑的函数名
FUNCS=(
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

RESULT_FILE="$ROOT_DIR/bn_basic_wp_results.txt"
LOG_DIR="$ROOT_DIR/wp_logs_bn_basic"
mkdir -p "$LOG_DIR"

# 结果 CSV 头
echo "function,acslg_time_sec,acslg_error,wp_proved,wp_total,wp_result" > "$RESULT_FILE"

for func in "${FUNCS[@]}"; do
  echo "=== Running ACSLG + WP for function: ${func} ==="

  # 0. 每轮开始前，先删掉旧的中间文件，防止复用上一次的结果
  rm -f "$RAW_ACSL_PATH" "$WITH_ACSL_PATH"

  # 4.1 跑 ACSLG，计时
  ACSLG_TIME_FILE="$(mktemp)"
  ACSLG_LOG="${LOG_DIR}/acslg_${func}.log"

  set +e
  /usr/bin/time -f "%e" -o "$ACSLG_TIME_FILE" \
    "$ROOT_DIR/build/src/ACSLG" \
      -extra-arg=-x -extra-arg=c \
      -extra-arg=-D__FRAMAC__ \
      -p "$ROOT_DIR/../openHiTLS/build" \
      "$SRC_PATH" \
      --func "$func" \
      >"$ACSLG_LOG" 2>&1
  acslg_rc=$?
  set -e

  # 从 time 输出里抽数字
  acslg_time_raw="$(cat "$ACSLG_TIME_FILE" 2>/dev/null || echo "")"
  rm -f "$ACSLG_TIME_FILE"
  acslg_time="$(echo "$acslg_time_raw" | grep -Eo '^[0-9.]+$' | head -n1)"
  if [ -z "$acslg_time" ]; then
    acslg_time="NA"
  fi

  # 检查 ACSLG log 里是否有 [ERROR（工具内部错误）
  if grep -q "^\[ERROR" "$ACSLG_LOG"; then
    acslg_error="yes"
  else
    acslg_error="no"
  fi

  # 如果 ACSLG 成功生成了原始文件，就重命名成 Frama-C 要用的名字
  if [ -f "$RAW_ACSL_PATH" ]; then
    mv -f "$RAW_ACSL_PATH" "$WITH_ACSL_PATH"
  fi

  # 4.2 跑 Frama-C WP（不看退出码，只看 log）
  WP_LOG="${LOG_DIR}/wp_${func}.log"

  set +e
  frama-c -wp -wp-prover Qed \
    -cpp-command "$CPP_CMD" \
    "$WITH_ACSL_PATH" >"$WP_LOG" 2>&1
  wp_rc=$?
  set -e

  # 解析 WP 输出：找 [wp] Proved goals: X / Y
  wp_proved="NA"
  wp_total="NA"
  wp_result="none"  # 默认：没有这行，大致认为是“解析/配置问题或未生成 VC”

  if grep -q "\[wp\] Proved goals:" "$WP_LOG"; then
    summary_line="$(grep "\[wp\] Proved goals:" "$WP_LOG" | tail -n 1)"
    # 典型行：[wp] Proved goals:   57 / 62
    wp_proved="$(echo "$summary_line" | awk '{print $4}')"
    wp_total="$(echo "$summary_line" | awk '{print $6}')"

    if [ "$wp_proved" = "$wp_total" ]; then
      wp_result="all"
    else
      wp_result="partial"
    fi
  fi

  # 4.3 记录一行结果
  echo "${func},${acslg_time},${acslg_error},${wp_proved},${wp_total},${wp_result}" >> "$RESULT_FILE"
done

echo
echo "Done. Raw results written to ${RESULT_FILE}, logs in ${LOG_DIR}/"
echo

# 5. 汇总统计
total_funcs=${#FUNCS[@]}

acslg_err_count=$(awk -F, 'NR>1 && $3=="yes" {c++} END {print c+0}' "$RESULT_FILE")
wp_all_count=$(     awk -F, 'NR>1 && $6=="all" {c++}      END {print c+0}' "$RESULT_FILE")
wp_partial_count=$( awk -F, 'NR>1 && $6=="partial" {c++} END {print c+0}' "$RESULT_FILE")
wp_none_count=$(    awk -F, 'NR>1 && $6=="none" {c++}    END {print c+0}' "$RESULT_FILE")

avg_time=$(awk -F, '
  NR>1 && $2 ~ /^[0-9.]+$/ {sum+=$2; n++}
  END {
    if (n>0) printf "%.4f", sum/n;
    else print "NA"
  }' "$RESULT_FILE")

echo "======== Summary ========"
echo "Total functions:              ${total_funcs}"
echo "ACSLG functions with [ERROR]: ${acslg_err_count}"
echo "WP all proved (X==Y):         ${wp_all_count}"
echo "WP partial proved:            ${wp_partial_count}"
echo "WP none/parse-error:          ${wp_none_count}"
echo "Average ACSLG time (sec):     ${avg_time}"
echo "========================="
