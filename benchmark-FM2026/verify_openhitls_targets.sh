#!/usr/bin/env bash

set -u
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

ACSLG_BIN="${ACSLG_BIN:-$ROOT_DIR/build/src/ACSLG}"
OPENHITLS_ROOT="${OPENHITLS_ROOT:-$SCRIPT_DIR/openhitls}"
OPENHITLS_BUILD="${OPENHITLS_BUILD:-$OPENHITLS_ROOT/build}"
COMPILE_DB="${COMPILE_DB:-$OPENHITLS_BUILD/compile_commands.json}"
OUT_DIR="${OUT_DIR:-$SCRIPT_DIR/openhitls_target_verify}"

GEN_TIMEOUT="${GEN_TIMEOUT:-60s}"
WP_TIMEOUT="${WP_TIMEOUT:-30s}"

TARGET_FILES=(
  # Keep target scope intentionally narrow: only bn_basic.c.
  "bn|crypto/bn/src/bn_basic.c"
)

RESULTS_TSV="$OUT_DIR/results.tsv"
GEN_LOG_DIR="$OUT_DIR/gen_logs"
PRE_LOG_DIR="$OUT_DIR/preprocess_logs"
VERIFY_LOG_DIR="$OUT_DIR/verify_logs"
WITH_ACSL_DIR="$OUT_DIR/with_acsl"
PROCESSED_DIR="$OUT_DIR/processed"

mkdir -p "$OUT_DIR" "$GEN_LOG_DIR" "$PRE_LOG_DIR" "$VERIFY_LOG_DIR" "$WITH_ACSL_DIR" "$PROCESSED_DIR"

printf "Group\tFile\tFunction\tGenExit\tGenStatus\tPreprocessStatus\tVerifyExit\tVerifyStatus\tNotes\n" > "$RESULTS_TSV"

if [ ! -x "$ACSLG_BIN" ]; then
  echo "ACSLG binary not found or not executable: $ACSLG_BIN" >&2
  exit 1
fi

if [ ! -d "$OPENHITLS_ROOT" ]; then
  echo "openHiTLS source directory not found: $OPENHITLS_ROOT" >&2
  exit 1
fi

if [ ! -d "$OPENHITLS_BUILD" ]; then
  echo "openHiTLS build directory not found: $OPENHITLS_BUILD" >&2
  exit 1
fi

if [ ! -f "$COMPILE_DB" ]; then
  echo "compile_commands.json not found: $COMPILE_DB" >&2
  echo "Re-run cmake with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON in openHiTLS build dir." >&2
  exit 1
fi

extract_cpp_flags() {
  local src_file="$1"
  python3 - "$COMPILE_DB" "$src_file" <<'PY'
import json
import os
import shlex
import sys

db_path = sys.argv[1]
src_path = os.path.realpath(sys.argv[2])

with open(db_path, "r", encoding="utf-8") as f:
    db = json.load(f)

entry = None
for item in db:
    if os.path.realpath(item.get("file", "")) == src_path:
        entry = item
        break

if entry is None:
    sys.exit(2)

if "arguments" in entry:
    argv = entry["arguments"]
else:
    argv = shlex.split(entry.get("command", ""))

flags = []
i = 0
while i < len(argv):
    a = argv[i]
    if a in ("-I", "-isystem"):
        if i + 1 < len(argv):
            flags.append(f"{a} {argv[i + 1]}")
            i += 2
            continue
    if a.startswith("-I") or a.startswith("-isystem"):
        flags.append(a)
    i += 1

print(" ".join(flags))
PY
}

run_preprocess_chain() {
  local in_dir="$1"
  local out_dir="$2"
  local log_file="$3"

  : > "$log_file"

  python3 "$SCRIPT_DIR/preprocess-scripts/strip_with_acsl_suffix.py" "$in_dir" "$out_dir" >> "$log_file" 2>&1 || return 1
  python3 "$SCRIPT_DIR/preprocess-scripts/convert_asserts_to_acsl.py" "$out_dir" >> "$log_file" 2>&1 || return 2
  python3 "$SCRIPT_DIR/preprocess-scripts/ensure_unknown_decls.py" "$out_dir" >> "$log_file" 2>&1 || return 3
  python3 "$SCRIPT_DIR/preprocess-scripts/ensure_unknown_assigns.py" "$out_dir" >> "$log_file" 2>&1 || return 4
  python3 "$SCRIPT_DIR/preprocess-scripts/merge_split_acsl.py" "$out_dir" >> "$log_file" 2>&1 || return 5
}

extract_functions_from_file() {
  local src_file="$1"
  python3 - "$src_file" <<'PY'
import sys

KEYWORDS = {
    "auto","break","case","char","const","continue","default","do","double",
    "else","enum","extern","float","for","goto","if","inline","int","long",
    "register","restrict","return","short","signed","sizeof","static",
    "struct","switch","typedef","union","unsigned","void","volatile","while",
    "_Bool","_Complex","_Imaginary","__attribute__","__attribute","__asm",
    "__asm__","__inline__","__inline","__declspec","__extension__",
    "__thread","__volatile__","__volatile","__restrict","__restrict__",
    "static_assert"
}

WHITESPACE = set(" \t\r\n\v\f")

def strip_comments_and_strings(src: str) -> str:
    out = []
    i = 0
    n = len(src)
    state = "code"
    while i < n:
        ch = src[i]
        if state == "code":
            if ch == '"':
                out.append(' ')
                i += 1
                state = "string"
                continue
            if ch == "'":
                out.append(' ')
                i += 1
                state = "char"
                continue
            if ch == '/' and i + 1 < n:
                nxt = src[i + 1]
                if nxt == '/':
                    out.extend('  ')
                    i += 2
                    state = "line_comment"
                    continue
                if nxt == '*':
                    out.extend('  ')
                    i += 2
                    state = "block_comment"
                    continue
            out.append(ch)
            i += 1
        elif state == "string":
            if ch == '\\' and i + 1 < n:
                out.extend('  ')
                i += 2
            elif ch == '"':
                out.append(' ')
                i += 1
                state = "code"
            else:
                out.append(' ')
                i += 1
        elif state == "char":
            if ch == '\\' and i + 1 < n:
                out.extend('  ')
                i += 2
            elif ch == "'":
                out.append(' ')
                i += 1
                state = "code"
            else:
                out.append(' ')
                i += 1
        elif state == "line_comment":
            if ch == '\n':
                out.append('\n')
                i += 1
                state = "code"
            else:
                out.append(' ')
                i += 1
        elif state == "block_comment":
            if ch == '*' and i + 1 < n and src[i + 1] == '/':
                out.extend('  ')
                i += 2
                state = "code"
            else:
                out.append('\n' if ch == '\n' else ' ')
                i += 1
    return ''.join(out)

def drop_preprocessor(text: str) -> str:
    lines = []
    for line in text.splitlines():
        stripped = line.lstrip()
        if stripped.startswith("#"):
            lines.append("")
        else:
            lines.append(line)
    return "\n".join(lines)

def skip_attributes(text: str, idx: int) -> int:
    n = len(text)
    while idx < n:
        while idx < n and text[idx] in WHITESPACE:
            idx += 1
        matched = False
        for keyword in ("__attribute__", "__attribute", "__declspec"):
            if text.startswith(keyword, idx):
                idx += len(keyword)
                while idx < n and text[idx] in WHITESPACE:
                    idx += 1
                if idx < n and text[idx] == '(':
                    depth = 1
                    idx += 1
                    while idx < n and depth > 0:
                        if text[idx] == '(':
                            depth += 1
                        elif text[idx] == ')':
                            depth -= 1
                        idx += 1
                matched = True
                break
        if not matched:
            break
    return idx

def extract_functions(text: str):
    funcs = []
    seen = set()
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == '_' or ch.isalpha():
            start = i
            i += 1
            while i < n and (text[i] == '_' or text[i].isalnum()):
                i += 1
            name = text[start:i]
            if name in KEYWORDS:
                continue
            j = i
            while j < n and text[j] in WHITESPACE:
                j += 1
            if j >= n or text[j] != '(':
                continue
            depth = 1
            k = j + 1
            while k < n and depth > 0:
                if text[k] == '(':
                    depth += 1
                elif text[k] == ')':
                    depth -= 1
                k += 1
            if depth != 0:
                i = j + 1
                continue
            m = skip_attributes(text, k)
            while m < n and text[m] in WHITESPACE:
                m += 1
            if m >= n or text[m] != '{':
                i = j + 1
                continue
            prev = start - 1
            while prev >= 0 and text[prev] in WHITESPACE:
                prev -= 1
            skip = False
            if prev >= 0 and text[prev] == '.':
                skip = True
            elif prev >= 1 and text[prev - 1] == '-' and text[prev] == '>':
                skip = True
            if skip:
                i = j + 1
                continue
            if name not in seen:
                seen.add(name)
                funcs.append(name)
            i = k
            continue
        i += 1
    return funcs

path = sys.argv[1]
with open(path, "r", encoding="utf-8", errors="ignore") as src:
    code = src.read()

cleaned = drop_preprocessor(strip_comments_and_strings(code))
for fn in extract_functions(cleaned):
    print(fn)
PY
}

echo "ACSLG binary     : $ACSLG_BIN"
echo "openHiTLS source : $OPENHITLS_ROOT"
echo "openHiTLS build  : $OPENHITLS_BUILD"
echo "compile DB       : $COMPILE_DB"
echo "results.tsv      : $RESULTS_TSV"
echo

for item in "${TARGET_FILES[@]}"; do
  IFS='|' read -r group rel_file <<< "$item"
  src_file="$OPENHITLS_ROOT/$rel_file"
  src_abs="$(realpath "$src_file" 2>/dev/null || true)"
  base_name="$(basename "$rel_file" .c)"
  rel_safe="${rel_file//\//__}"

  echo ">>> [$group] $rel_file"

  if [ ! -f "$src_file" ]; then
    notes="source file missing"
    printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
      "$group" "$rel_file" "-" "-" "Skipped" "Skipped" "-" "Skipped" "$notes" \
      >> "$RESULTS_TSV"
    continue
  fi

  mapfile -t funcs < <(extract_functions_from_file "$src_file")
  if [ "${#funcs[@]}" -eq 0 ]; then
    printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
      "$group" "$rel_file" "-" "-" "Skipped" "Skipped" "-" "Skipped" "no function definitions found" \
      >> "$RESULTS_TSV"
    continue
  fi

  echo "    functions: ${#funcs[@]}"
  for func in "${funcs[@]}"; do
    safe_func="${func//[^a-zA-Z0-9_]/_}"
    tag="${group}__${rel_safe}__${safe_func}"

    gen_log="$GEN_LOG_DIR/${tag}.log"
    pre_log="$PRE_LOG_DIR/${tag}.log"
    verify_log="$VERIFY_LOG_DIR/${tag}.log"
    rm -f "$gen_log" "$pre_log" "$verify_log"

    gen_exit="-"
    gen_status="Skipped"
    pre_status="Skipped"
    verify_exit="-"
    verify_status="Skipped"
    notes=""

    gen_out="${src_file}_with_acsl"
    rm -f "$gen_out"

    timeout "$GEN_TIMEOUT" "$ACSLG_BIN" \
      -p "$OPENHITLS_BUILD" \
      --extra-arg=-Wno-error \
      --extra-arg=-Wno-unknown-warning-option \
      --func "$func" \
      "$src_file" \
      > "$gen_log" 2>&1
    gen_exit="$?"

    if [ -f "$gen_out" ]; then
      gen_status="OutputExists"
    elif [ "$gen_exit" = "124" ]; then
      gen_status="Timeout"
    else
      gen_status="NoOutput"
    fi

    if [ "$gen_status" = "OutputExists" ]; then
      in_dir="$WITH_ACSL_DIR/$group/${base_name}/$safe_func"
      out_dir="$PROCESSED_DIR/$group/${base_name}/$safe_func"
      rm -rf "$in_dir" "$out_dir"
      mkdir -p "$in_dir" "$out_dir"
      cp "$gen_out" "$in_dir/${base_name}.c_with_acsl"

      run_preprocess_chain "$in_dir" "$out_dir" "$pre_log"
      pre_rc="$?"
      if [ "$pre_rc" = "0" ]; then
        pre_status="OK"
      else
        pre_status="Fail($pre_rc)"
        notes="preprocess failed"
      fi

      processed_file="$out_dir/${base_name}.c"
      if [ "$pre_status" = "OK" ] && [ -f "$processed_file" ]; then
        cpp_flags="$(extract_cpp_flags "$src_abs" 2>/dev/null || true)"
        if [ -z "$cpp_flags" ]; then
          notes="compile flags missing"
        fi

        timeout "$WP_TIMEOUT" frama-c \
          -wp \
          -wp-prop "$func" \
          -wp-prover alt-ergo \
          -cpp-extra-args="$cpp_flags" \
          "$processed_file" \
          > "$verify_log" 2>&1
        verify_exit="$?"

        if [ "$verify_exit" = "0" ]; then
          if rg -q "No goal generated" "$verify_log"; then
            verify_status="Success(NoGoal)"
          else
            verify_status="Success"
          fi
        elif [ "$verify_exit" = "124" ]; then
          verify_status="Timeout"
        else
          verify_status="Fail"
        fi
      fi
    fi

    printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
      "$group" "$rel_file" "$func" "$gen_exit" "$gen_status" "$pre_status" "$verify_exit" "$verify_status" "$notes" \
      >> "$RESULTS_TSV"
    rm -f "$gen_out"
  done
done

echo
echo "Done. See:"
echo "  $RESULTS_TSV"
echo "  $GEN_LOG_DIR"
echo "  $PRE_LOG_DIR"
echo "  $VERIFY_LOG_DIR"
