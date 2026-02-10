#!/usr/bin/env bash
#
# Run the verification suite
# - Use frama-c WP on every function that has a body
# - Parse all function definitions per file and pass them via -wp-prop in one call
# - Success/failure is determined by frama-c exit code (0 = success)
#

set -u
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# --- Configuration ---
BENCHMARK_BASE="${BENCHMARK_BASE:-$SCRIPT_DIR/benchmark_with_acsl}" # Root directory containing all benchmark groups
FRAMAC_BIN="${FRAMAC_BIN:-frama-c}"                                  # frama-c executable
FRAMAC_ARGS_HEAD=(-wp)                                                # -wp must be first
FRAMAC_ARGS_TAIL=(-wp-prover alt-ergo)                               # Remaining WP options
TIME_LIMIT="${TIME_LIMIT:-10s}"                                      # Timeout per file
RESULTS_FILE="${RESULTS_FILE:-$SCRIPT_DIR/verify_results.tsv}"       # Detailed TSV results
LOG_DIR="${LOG_DIR:-$SCRIPT_DIR/verify_logs}"                        # Per-run stdout/stderr logs
SUMMARY_FILE="${SUMMARY_FILE:-$SCRIPT_DIR/verify_summary.txt}"       # Final summary report

# --- Initialize counters ---
mkdir -p "$LOG_DIR"

declare -A group_total
declare -A group_success
declare -A group_timeout
declare -A group_fail

total_all=0
success_all=0
timeout_all=0
fail_all=0

TIME_FILE=$(mktemp)
trap "rm -f $TIME_FILE" EXIT

printf "Group\tFile\tStatus\tTime(s)\n" > "$RESULTS_FILE"
> "$SUMMARY_FILE"

echo "Running benchmarks from: $BENCHMARK_BASE"
echo "Binary: $FRAMAC_BIN ${FRAMAC_ARGS_HEAD[*]} -wp-prop <functions> ${FRAMAC_ARGS_TAIL[*]}"
echo "Time Limit: $TIME_LIMIT"
echo "Detailed TSV results will be in: $RESULTS_FILE"
echo "Detailed run logs will be in: $LOG_DIR"
echo "Final summary will be in: $SUMMARY_FILE"
echo "================================================="

# -------------------------------------------------------------------
#  Core change 1: Use Python to extract all function definitions
# -------------------------------------------------------------------
read -r -d '' find_functions_py <<'EOF'
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
            if ch == '"' :
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
        else:
            i += 1
    return funcs

def main():
    if len(sys.argv) < 2:
        sys.exit(0)
    path = sys.argv[1]
    with open(path, "r", encoding="utf-8", errors="ignore") as src:
        code = src.read()
    cleaned = drop_preprocessor(strip_comments_and_strings(code))
    for func in extract_functions(cleaned):
        print(func)

if __name__ == "__main__":
    main()
EOF
# -------------------------------------------------------------------
#  End core change 1
# -------------------------------------------------------------------

# --- Main loop: iterate over each group ---
for group_dir in $(find "$BENCHMARK_BASE" -mindepth 1 -maxdepth 1 -type d); do
    group_name=$(basename "$group_dir")
    : "${group_total[$group_name]:=0}"
    : "${group_success[$group_name]:=0}"
    : "${group_timeout[$group_name]:=0}"
    : "${group_fail[$group_name]:=0}"
    echo
    echo "Processing Group: $group_name"
    echo "-------------------------------------------------"

    # --- Inner loop: recursively find all .c / .c_with_acsl files ---
    while IFS= read -r -d '' filepath; do
        echo "  --- Running on $filepath"

        relative_path=${filepath#"$BENCHMARK_BASE"/}
        safe_name=${relative_path//\//_}
        safe_name=${safe_name// /_}
        run_log="$LOG_DIR/${safe_name}.log"
        status=""
        exec_time="0.00"
        exit_code=0

        # ----------------------------------------------------
        #  Core change 2: Python script extracts all functions
        # ----------------------------------------------------
        mapfile -t FUNC_NAMES < <(python3 - "$filepath" <<<"$find_functions_py")

        if [ "${#FUNC_NAMES[@]}" -eq 0 ]; then
            echo "  !!! FAILED to find valid function definitions in $filepath. Skipping."
            status="Fail (No Func)"
            ((group_fail[$group_name]++))
            ((fail_all++))
        else
            echo "  --- Analyzing functions (${#FUNC_NAMES[@]}): ${FUNC_NAMES[*]}"

            func_prop=$(printf '%s,' "${FUNC_NAMES[@]}")
            func_prop=${func_prop%,}

            cmd=(timeout "$TIME_LIMIT" "$FRAMAC_BIN")
            cmd+=("${FRAMAC_ARGS_HEAD[@]}")
            cmd+=(-wp-prop "$func_prop")
            cmd+=("${FRAMAC_ARGS_TAIL[@]}")
            cmd+=("$filepath")

            /usr/bin/time -f "%e" -o "$TIME_FILE" \
                "${cmd[@]}" \
                > "$run_log" 2>&1

            exit_code=$?
            exec_time=$(cat "$TIME_FILE")
        fi

        ((group_total[$group_name]++))
        ((total_all++))

        # ----------------------------------------------------
        #  Core change 3: Status determined by exit code
        # ----------------------------------------------------

        # If status already set to "Fail (No Func)", skip further checks
        if [ -n "$status" ]; then
            : # Keep status = "Fail (No Func)"

        # 1. Highest-priority failure: timeout
        elif [ $exit_code -eq 124 ]; then
            status="Timeout"
            echo "  !!! Timeout ($TIME_LIMIT) ($exec_time s)"
            ((group_timeout[$group_name]++))
            ((timeout_all++))

        # 2. Only success condition: exit code 0
        elif [ $exit_code -eq 0 ]; then
            status="Success"
            echo "  >>> Success ($exec_time s)"
            ((group_success[$group_name]++))
            ((success_all++))

        # 3. All other non-timeout, non-zero codes are tool failures
        else
            status="Fail (Code $exit_code)"
            echo "  !!! Tool Failed (Code $exit_code) ($exec_time s)"
            ((group_fail[$group_name]++))
            ((fail_all++))
        fi
        # ----------------------------------------------------
        #  End core change block
        # ----------------------------------------------------

        # Append this file's result to the TSV
        printf "%s\t%s\t%s\t%s\n" "$group_name" "$filepath" "$status" "$exec_time" >> "$RESULTS_FILE"

    done < <(find "$group_dir" -type f \( -name "*.c" -o -name "*.c_with_acsl" \) -print0)
done

# --- Summary report (unchanged) ---
{
    echo
    echo "================================================="
    echo "                Benchmark Summary"
    echo "================================================="
    echo

    for group in "${!group_total[@]}"; do
        total=${group_total[$group]:-0}
        success=${group_success[$group]:-0}
        fail=${group_fail[$group]:-0}
        timeout=${group_timeout[$group]:-0}
        
        echo "--- Group: $group ---"
        echo "  Total   : $total"
        echo "  Success : $success"
        echo "  Fail    : $fail"
        echo "  Timeout : $timeout"
        echo
    done

    echo "--- Overall ---"
    echo "Total Files   : $total_all"
    echo "Total Success : $success_all"
    echo "Total Fail    : $fail_all"
    echo "Total Timeout : $timeout_all"
    echo "================================================="

} | tee -a "$SUMMARY_FILE"

trap - EXIT
