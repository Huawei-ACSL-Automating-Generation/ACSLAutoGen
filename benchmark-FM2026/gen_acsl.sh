#!/usr/bin/env bash
#
# Run the benchmark suite
# - Iterate by group (subdirectory)
# - Record execution time per file
# - The only success criterion is the presence of the .c_with_acsl output
# - Timeout takes precedence over all other statuses
# - Summary is printed to console and summary.txt
#

set -u
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# --- Configuration ---
BENCHMARK_BASE="${BENCHMARK_BASE:-$SCRIPT_DIR/benchmark}"        # Root directory containing all benchmark groups
BINARY="${BINARY:-$ROOT_DIR/build/src/ACSLG}"                    # Target executable
COMP_DB_DIR="${COMP_DB_DIR:-$ROOT_DIR/build}"                    # compile_commands.json directory
TIME_LIMIT="${TIME_LIMIT:-30s}"                                  # Timeout per file
RESULTS_FILE="${RESULTS_FILE:-$SCRIPT_DIR/benchmark_results.tsv}" # Detailed TSV results
LOG_DIR="${LOG_DIR:-$SCRIPT_DIR/benchmark_logs}"                 # Logs for each run (stdout/stderr)
SUMMARY_FILE="${SUMMARY_FILE:-$SCRIPT_DIR/benchmark_summary.txt}" # Final summary report

# --- Initialize counters ---
mkdir -p "$LOG_DIR"

# No "invalid" counter is tracked
declare -A group_total
declare -A group_success
declare -A group_timeout
declare -A group_fail

total_all=0
success_all=0
timeout_all=0
fail_all=0

# Reset TSV file and write header
printf "Group\tFile\tStatus\tTime(s)\n" > "$RESULTS_FILE"
# Clear old summary file
> "$SUMMARY_FILE"

echo "Running benchmarks from: $BENCHMARK_BASE"
echo "Binary: $BINARY"
echo "Time Limit: $TIME_LIMIT"
echo "Detailed TSV results will be in: $RESULTS_FILE"
echo "Detailed run logs will be in: $LOG_DIR"
echo "Final summary will be in: $SUMMARY_FILE"
echo "================================================="

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

    # --- Inner loop: find all .c files in the group ---
    for filepath in $(find "$group_dir" -type f -name "*.c"); do
        echo "  --- Running on $filepath"

        run_log="$LOG_DIR/$(basename "$filepath" .c).log"

        # Build the expected output file path
        dir_name=$(dirname "$filepath")
        base_name=$(basename "$filepath" .c)
        expected_output="$dir_name/${base_name}.c_with_acsl"

        # Important: remove any stale output before running
        rm -f "$expected_output"

        # Use /proc/uptime as a monotonic clock to avoid negative time due to VM clock adjustments
        start_time=$(awk '{print $1}' /proc/uptime)

        # Run
        timeout "$TIME_LIMIT" "$BINARY" \
            -p "$COMP_DB_DIR" \
            --extra-arg=-x \
            --extra-arg=c \
            "$filepath" \
            > "$run_log" 2>&1

        exit_code=$?
        end_time=$(awk '{print $1}' /proc/uptime)
        exec_time=$(awk -v start="$start_time" -v end="$end_time" 'BEGIN {printf "%.3f", end - start}')

        ((group_total[$group_name]++))
        ((total_all++))

        # ----------------------------------------------------
        # Core logic: only timeout or output existence determine status
        # ----------------------------------------------------

        # 1. Highest-priority failure: timeout
        if [ $exit_code -eq 124 ]; then
            status="Timeout"
            echo "  !!! Timeout ($TIME_LIMIT) ($exec_time s)"
            ((group_timeout[$group_name]++))
            ((timeout_all++))

        # 2. Only success condition: output file exists
        elif [ -f "$expected_output" ]; then
            status="Success"
            echo "  >>> Success ($exec_time s) (Created $expected_output)"
            ((group_success[$group_name]++))
            ((success_all++))

        # 3. All other cases (not timeout, no output file) count as tool failure
        else
            status="Fail"
            echo "  !!! Tool Failed ($exec_time s) (Output file not found: $expected_output)"
            ((group_fail[$group_name]++))
            ((fail_all++))
        fi
        # ----------------------------------------------------
        # End core logic
        # ----------------------------------------------------

        # Append this file's result to the TSV
        printf "%s\t%s\t%s\t%s\n" "$group_name" "$filepath" "$status" "$exec_time" >> "$RESULTS_FILE"

    done
done

# --- Summary report ---
# Use tee to append the echoed block to the summary file while printing to stdout
{
    echo
    echo "================================================="
    echo "                Benchmark Summary"
    echo "================================================="
    echo

    # Per-group details
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

    # Overall totals
    echo "--- Overall ---"
    echo "Total Files   : $total_all"
    echo "Total Success : $success_all"
    echo "Total Fail    : $fail_all"
    echo "Total Timeout : $timeout_all"
    echo "================================================="

} | tee -a "$SUMMARY_FILE"

# ----------------------------------------------------
# End of summary block
# ----------------------------------------------------
