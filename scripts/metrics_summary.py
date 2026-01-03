#!/usr/bin/env python3
import csv
import sys
from collections import defaultdict


def as_int(s: str) -> int | None:
    s = (s or "").strip()
    if not s or s == "NA":
        return None
    try:
        return int(float(s))
    except Exception:
        return None


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: metrics_summary.py <results.csv>", file=sys.stderr)
        return 2

    path = sys.argv[1]
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))

    by_suite: dict[str, list[dict[str, str]]] = defaultdict(list)
    for r in rows:
        by_suite[r.get("suite", "")].append(r)

    total = len(rows)
    acslg_ok = sum(1 for r in rows if r.get("acslg_error") == "no")
    acslg_fail = total - acslg_ok
    wp_all = sum(1 for r in rows if r.get("wp_result") == "all")
    wp_partial = sum(1 for r in rows if r.get("wp_result") == "partial")
    wp_timeout = sum(1 for r in rows if r.get("wp_result") == "timeout")
    wp_none = sum(1 for r in rows if r.get("wp_result") == "none")

    proved_sum = 0
    goals_sum = 0
    proved_funcs = 0
    for r in rows:
        proved = as_int(r.get("wp_proved", ""))
        goals = as_int(r.get("wp_total", ""))
        if proved is None or goals is None:
            continue
        proved_sum += proved
        goals_sum += goals
        proved_funcs += 1

    def pct(n: int, d: int) -> str:
        if d == 0:
            return "NA"
        return f"{(100.0 * n / d):.1f}%"

    print("# Metrics Summary")
    print()
    print(f"- Total functions: {total}")
    print(f"- ACSLG ok/fail: {acslg_ok}/{acslg_fail} ({pct(acslg_ok,total)})")
    print(f"- WP all/partial/none/timeout: {wp_all}/{wp_partial}/{wp_none}/{wp_timeout}")
    if goals_sum > 0:
        print(f"- WP goals proved/total: {proved_sum}/{goals_sum} ({pct(proved_sum,goals_sum)})")
        print(f"- WP goals parsed funcs: {proved_funcs}/{total} ({pct(proved_funcs,total)})")
    print()

    print("## By Suite")
    print()
    print("| suite | funcs | acslg_ok | acslg_fail | wp_all | wp_partial | wp_none | wp_timeout |")
    print("|---|---:|---:|---:|---:|---:|---:|---:|")
    for suite in sorted(by_suite.keys()):
        srows = by_suite[suite]
        stotal = len(srows)
        sok = sum(1 for r in srows if r.get("acslg_error") == "no")
        sfail = stotal - sok
        sall = sum(1 for r in srows if r.get("wp_result") == "all")
        spart = sum(1 for r in srows if r.get("wp_result") == "partial")
        snone = sum(1 for r in srows if r.get("wp_result") == "none")
        stime = sum(1 for r in srows if r.get("wp_result") == "timeout")
        print(f"| {suite} | {stotal} | {sok} | {sfail} | {sall} | {spart} | {snone} | {stime} |")

    print()
    print("## WP Goals By Suite")
    print()
    print("| suite | funcs_with_goals | proved_goals | total_goals | proved_ratio |")
    print("|---|---:|---:|---:|---:|")
    for suite in sorted(by_suite.keys()):
        srows = by_suite[suite]
        sp = 0
        st = 0
        sn = 0
        for r in srows:
            proved = as_int(r.get("wp_proved", ""))
            goals = as_int(r.get("wp_total", ""))
            if proved is None or goals is None:
                continue
            sp += proved
            st += goals
            sn += 1
        ratio = pct(sp, st) if st > 0 else "NA"
        print(f"| {suite} | {sn} | {sp} | {st} | {ratio} |")

    print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
