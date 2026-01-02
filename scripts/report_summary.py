#!/usr/bin/env python3
import csv
import os
import re
import sys
from collections import defaultdict


def main() -> int:
    if len(sys.argv) != 4:
        print("Usage: report_summary.py <results.csv> <report.md> <table.txt>", file=sys.stderr)
        return 2

    csv_path, out_path, table_path = sys.argv[1], sys.argv[2], sys.argv[3]
    rows = []
    with open(csv_path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(row)

    by_suite = defaultdict(list)
    for row in rows:
        by_suite[row["suite"]].append(row)

    ansi_re = re.compile(r"\x1b\[[0-9;]*m")

    def strip_ansi(s: str) -> str:
        return ansi_re.sub("", s or "")

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

        fails = [r for r in srows if r["acslg_error"] == "yes"]
        if fails:
            lines.append("### ACSLG Failures")
            for r in fails:
                issue = strip_ansi((r.get("acslg_issue") or "").strip())
                if not issue:
                    issue = "(no issue line)"
                lines.append(f"- `{r['function']}`: {issue}")
            lines.append("")

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

    # Build a readable text table (module/function + ACSLG/WP status) for quick scanning.
    headers = ["module", "function", "acslg_time_sec", "acslg_error", "wp_proved", "wp_total", "wp_result"]
    widths = [len(h) for h in headers]
    table_rows = []
    totals = {
        "total": 0,
        "acslg_err": 0,
        "wp_all": 0,
        "wp_partial": 0,
        "wp_none": 0,
        "time_sum": 0.0,
        "time_n": 0,
    }

    for row in rows:
        source = row.get("source", "")
        module = source.rsplit("/", 1)[-1].rsplit(".", 1)[0]
        func = row.get("function", "")
        acslg_time = row.get("acslg_time_sec", "")
        acslg_error = row.get("acslg_error", "")
        wp_proved = row.get("wp_proved", "")
        wp_total = row.get("wp_total", "")
        wp_result = row.get("wp_result", "")

        record = [module, func, acslg_time, acslg_error, wp_proved, wp_total, wp_result]
        table_rows.append(record)
        for i, value in enumerate(record):
            if len(value) > widths[i]:
                widths[i] = len(value)

        totals["total"] += 1
        if acslg_error == "yes":
            totals["acslg_err"] += 1
        if wp_result == "all":
            totals["wp_all"] += 1
        elif wp_result == "partial":
            totals["wp_partial"] += 1
        elif wp_result in ("none", "timeout"):
            totals["wp_none"] += 1
        try:
            totals["time_sum"] += float(acslg_time)
            totals["time_n"] += 1
        except Exception:
            pass

    sep = "+"
    for w in widths:
        sep += "-" * (w + 2) + "+"

    out_lines = []
    out_lines.append(sep)
    out_lines.append("|" + "".join(f" {h:<{w}} |" for h, w in zip(headers, widths)))
    out_lines.append(sep)
    for record in table_rows:
        out_lines.append("|" + "".join(f" {v:<{w}} |" for v, w in zip(record, widths)))
    out_lines.append(sep)
    out_lines.append("")
    out_lines.append("Summary:")
    sumsep = "+------------------------------+--------+"
    out_lines.append(sumsep)
    out_lines.append(f"| {'Total functions':<28} | {totals['total']:<6} |")
    out_lines.append(f"| {'ACSLG errors':<28} | {totals['acslg_err']:<6} |")
    out_lines.append(f"| {'WP all proved':<28} | {totals['wp_all']:<6} |")
    out_lines.append(f"| {'WP partial proved':<28} | {totals['wp_partial']:<6} |")
    out_lines.append(f"| {'WP none/parse-error':<28} | {totals['wp_none']:<6} |")
    if totals["time_n"] > 0:
        avg = totals["time_sum"] / totals["time_n"]
        out_lines.append(f"| {'Avg ACSLG time (sec)':<28} | {avg:<6.4f} |")
    else:
        out_lines.append(f"| {'Avg ACSLG time (sec)':<28} | {'NA':<6} |")
    out_lines.append(sumsep)

    with open(table_path, "w", encoding="utf-8") as f:
        f.write("\n".join(out_lines).rstrip() + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
