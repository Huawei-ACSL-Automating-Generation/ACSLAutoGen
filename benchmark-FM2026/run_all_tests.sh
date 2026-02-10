#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

RUN_TAG="${RUN_TAG:-$(date +%Y%m%d_%H%M%S)}"
RUN_ROOT="${RUN_ROOT:-$ROOT_DIR/runlogs/full_test_${RUN_TAG}}"

OPENHITLS_ROOT="${OPENHITLS_ROOT:-$SCRIPT_DIR/openhitls}"
OPENHITLS_SUITES="${OPENHITLS_SUITES:-basic}"
SKIP_BUILD="${SKIP_BUILD:-1}"

mkdir -p "$RUN_ROOT"

echo "[INFO] Full test run root: $RUN_ROOT"
echo "[INFO] Step 1/3: Running original benchmark suite under benchmark-FM2026/benchmark ..."

(
  cd "$SCRIPT_DIR"
  bash run_simple_functions_benchmark.sh
) | tee "$RUN_ROOT/original_benchmark_console.log"

cp -f "$SCRIPT_DIR/benchmark_results.tsv" "$RUN_ROOT/original_benchmark_results.tsv"
cp -f "$SCRIPT_DIR/benchmark_summary.txt" "$RUN_ROOT/original_benchmark_summary.txt"
cp -f "$SCRIPT_DIR/verify_results.tsv" "$RUN_ROOT/original_verify_results.tsv"
cp -f "$SCRIPT_DIR/verify_summary.txt" "$RUN_ROOT/original_verify_summary.txt"
if [ -f "$SCRIPT_DIR/fully_verified" ]; then
  cp -f "$SCRIPT_DIR/fully_verified" "$RUN_ROOT/original_fully_verified.txt"
fi

echo "[INFO] Collecting fully/partially verified original benchmark sources ..."
(
  cd "$SCRIPT_DIR"
  python3 collect_successful_goals.py \
    verify_logs \
    "$RUN_ROOT/original_fully_verified_sources" \
    --source-root "$SCRIPT_DIR" \
    --goal-status full \
    --stats-file "$RUN_ROOT/original_fully_verified_counts.tsv"

  python3 collect_successful_goals.py \
    verify_logs \
    "$RUN_ROOT/original_partially_verified_sources" \
    --source-root "$SCRIPT_DIR" \
    --goal-status partial \
    --stats-file "$RUN_ROOT/original_partially_verified_counts.tsv"
)

prepare_openhitls() {
  local root="$1"
  if [ ! -d "$root" ]; then
    echo "[INFO] openHiTLS not found locally. Cloning openhitls-0.2.1 ..."
    git clone https://gitcode.com/openHiTLS/openhitls.git "$root"
    git -C "$root" checkout -f tags/openhitls-0.2.1
  fi

  if [ ! -f "$root/config/macro_config/hitls_build.h" ]; then
    echo "[ERROR] Invalid openHiTLS root: $root" >&2
    exit 2
  fi

  echo "[INFO] Updating openHiTLS submodules ..."
  git -C "$root" submodule update --init --recursive

  if [ ! -d "$root/build" ]; then
    mkdir -p "$root/build"
  fi

  local need_rebuild=0
  if [ ! -f "$root/build/compile_commands.json" ]; then
    need_rebuild=1
  elif rg -q "x86_64-conda-linux-gnu|conda" "$root/build/compile_commands.json"; then
    # Container image sets CC/CXX to conda cross-compiler globally.
    # For ACSLG parsing we need host system headers (stdbool.h, etc.), so
    # regenerate compilation database with system toolchain.
    need_rebuild=1
  fi

  if [ "$need_rebuild" = "1" ]; then
    echo "[INFO] Building openHiTLS (openhitls-0.2.1) with system compiler ..."
    rm -f "$root/build/CMakeCache.txt" "$root/build/compile_commands.json"
    (
      cd "$root/build"
      CC=/usr/bin/cc \
      CXX=/usr/bin/c++ \
      python3 ../configure.py \
        --enable hitls_bsl hitls_crypto hitls_tls hitls_pki hitls_auth \
        --lib_type static \
        --bits=64 \
        --system=linux
      CC=/usr/bin/cc \
      CXX=/usr/bin/c++ \
      cmake -DCMAKE_C_COMPILER=/usr/bin/cc \
            -DCMAKE_CXX_COMPILER=/usr/bin/c++ \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
      cmake --build . -j"$(nproc)"
    )

    if [ ! -f "$root/build/compile_commands.json" ]; then
      echo "[ERROR] openHiTLS build did not produce compile_commands.json" >&2
      exit 3
    fi
  fi
}

echo "[INFO] Step 2/3: Preparing openHiTLS source/build ..."
prepare_openhitls "$OPENHITLS_ROOT"

OPENHITLS_RUN_DIR="$RUN_ROOT/openhitls"
mkdir -p "$OPENHITLS_RUN_DIR"

echo "[INFO] Step 3/3: Running openHiTLS function-level experiments ..."
(
  cd "$ROOT_DIR"
  SKIP_BUILD="$SKIP_BUILD" \
  OPENHITLS_ROOT="$OPENHITLS_ROOT" \
  SUITES_OVERRIDE="$OPENHITLS_SUITES" \
  RUN_DIR_OVERRIDE="$OPENHITLS_RUN_DIR" \
  bash "$SCRIPT_DIR/experiment_adapted.sh"
) | tee "$RUN_ROOT/openhitls_console.log"

python3 - "$RUN_ROOT" <<'PY'
import csv
import os
import sys
from collections import Counter

run_root = sys.argv[1]
bench_gen_tsv = os.path.join(run_root, "original_benchmark_results.tsv")
bench_wp_tsv = os.path.join(run_root, "original_verify_results.tsv")
bench_full_counts_tsv = os.path.join(run_root, "original_fully_verified_counts.tsv")
bench_partial_counts_tsv = os.path.join(run_root, "original_partially_verified_counts.tsv")
openhitls_csv = os.path.join(run_root, "openhitls", "results.csv")
summary_md = os.path.join(run_root, "summary.md")

def count_tsv_status(path, idx):
    c = Counter()
    with open(path, encoding="utf-8", errors="ignore") as f:
        next(f, None)
        for line in f:
            cols = line.rstrip("\n").split("\t")
            if len(cols) > idx:
                c[cols[idx]] += 1
    return c

def read_benchmark_counts(path):
    out = Counter()
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8", errors="ignore") as f:
        next(f, None)
        for line in f:
            cols = line.rstrip("\n").split("\t")
            if len(cols) >= 2:
                name = cols[0]
                try:
                    cnt = int(cols[1])
                except ValueError:
                    continue
                out[name] += cnt
    return out

bench_gen = count_tsv_status(bench_gen_tsv, 2)
bench_wp = count_tsv_status(bench_wp_tsv, 2)
bench_full = read_benchmark_counts(bench_full_counts_tsv)
bench_partial = read_benchmark_counts(bench_partial_counts_tsv)

rows = list(csv.DictReader(open(openhitls_csv, encoding="utf-8", errors="ignore")))
acslg_rc = Counter(r["acslg_rc"] for r in rows)
wp_res = Counter(r["wp_result"] for r in rows)
suite_ct = Counter(r["suite"] for r in rows)

lines = []
lines.append("# Unified Test Summary")
lines.append("")
lines.append("## Original Benchmark (benchmark-FM2026/benchmark)")
lines.append(f"- Total generation cases: {sum(bench_gen.values())}")
lines.append(f"- Generation status: {dict(bench_gen)}")
lines.append(f"- Total WP verification cases: {sum(bench_wp.values())}")
lines.append(f"- Verification status: {dict(bench_wp)}")
lines.append(f"- Fully verified sources (all goals proved): {sum(bench_full.values())}")
lines.append(f"- Fully verified by benchmark bucket: {dict(bench_full)}")
lines.append(f"- Partially verified sources (some goals proved): {sum(bench_partial.values())}")
lines.append(f"- Partially verified by benchmark bucket: {dict(bench_partial)}")
lines.append("")
lines.append("## openHiTLS Function-Level Tests")
lines.append(f"- Total cases: {len(rows)}")
lines.append(f"- Cases by suite: {dict(suite_ct)}")
lines.append(f"- ACSLG return-code distribution: {dict(acslg_rc)}")
lines.append(f"- WP result distribution: {dict(wp_res)}")
lines.append("")
lines.append("## Artifacts")
lines.append(f"- Original benchmark logs/results: `{run_root}`")
lines.append(f"- Original fully verified sources: `{os.path.join(run_root, 'original_fully_verified_sources')}`")
lines.append(f"- Original partially verified sources: `{os.path.join(run_root, 'original_partially_verified_sources')}`")
lines.append(f"- openHiTLS logs/results: `{os.path.join(run_root, 'openhitls')}`")
lines.append("")
lines.append("All outputs are in English and machine-readable TSV/CSV formats are preserved.")

with open(summary_md, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
PY

echo "[INFO] All tests finished."
echo "[INFO] English summary: $RUN_ROOT/summary.md"
