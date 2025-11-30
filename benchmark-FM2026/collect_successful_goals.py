#!/usr/bin/env python3
"""
Collect source files whose verification logs proved every goal.

The script scans every *.log file inside a log directory. For each log that
contains a line like "Proved goals:   X / Y" with X == Y > 0, the script copies
the referenced source file into a destination directory while preserving the
source tree layout.
"""

from __future__ import annotations

import argparse
import re
import shutil
from collections import Counter
from pathlib import Path
from typing import Iterable, Optional, Sequence, Tuple

PROVED_RE = re.compile(r"Proved goals:\s*(\d+)\s*/\s*(\d+)")
PARSING_RE = re.compile(r"\[kernel\]\s+Parsing\s+(.+?)(?:\s*\(.*\))?\s*$")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Copy sources whose logs show all goals proved."
    )
    parser.add_argument(
        "log_dir", type=Path, help="Directory that contains log files (.log)."
    )
    parser.add_argument(
        "destination",
        type=Path,
        help="Directory where matching sources will be copied.",
    )
    parser.add_argument(
        "--source-root",
        type=Path,
        default=Path.cwd(),
        help="Root directory of the source tree (default: current working directory).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Only report actions without copying files.",
    )
    parser.add_argument(
        "--stats-file",
        type=Path,
        default=None,
        help=(
            "Path to write benchmark success counts (default: "
            "<destination>/successful_counts.tsv)."
        ),
    )
    return parser.parse_args()


def iter_logs(log_dir: Path) -> Iterable[Path]:
    for path in sorted(log_dir.rglob("*.log")):
        if path.is_file():
            yield path


def extract_source_path(lines: Sequence[str]) -> Optional[str]:
    for line in lines:
        match = PARSING_RE.search(line)
        if match:
            candidate = match.group(1).strip().strip("'\"")
            if candidate:
                return candidate
    return None


def log_proved_all(lines: Sequence[str]) -> bool:
    proved = False
    for line in lines:
        match = PROVED_RE.search(line)
        if match:
            lhs, rhs = int(match.group(1)), int(match.group(2))
            if rhs > 0 and lhs == rhs:
                proved = True
    return proved


def resolve_paths(source_str: str, source_root: Path) -> Tuple[Path, Path]:
    raw_path = Path(source_str)
    absolute = raw_path if raw_path.is_absolute() else source_root / raw_path
    absolute = absolute.resolve()
    try:
        relative = absolute.relative_to(source_root.resolve())
    except ValueError:
        relative = Path(absolute.name)
    return absolute, relative


def benchmark_key(rel_path: Path) -> str:
    """
    Derive the benchmark bucket from a relative path.
    If the path starts with benchmark_with_acsl/, use its next component;
    otherwise use the first component, or "." if empty.
    """
    if not rel_path.parts:
        return "."
    if rel_path.parts[0] == "benchmark_with_acsl" and len(rel_path.parts) >= 2:
        return rel_path.parts[1]
    return rel_path.parts[0]


def write_stats(counts: Counter[str], destination: Path, stats_file: Optional[Path], dry_run: bool) -> None:
    if not counts:
        return
    target = stats_file if stats_file is not None else destination / "successful_counts.tsv"
    lines = ["Benchmark\tCount"]
    for name, cnt in sorted(counts.items()):
        lines.append(f"{name}\t{cnt}")
    content = "\n".join(lines) + "\n"
    if dry_run:
        print("[DRY-RUN] Stats file would be written to:", target)
        print(content.rstrip("\n"))
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content, encoding="utf-8")
    print(f"Wrote stats to {target}")


def main() -> None:
    args = parse_args()
    log_dir = args.log_dir.resolve()
    destination = args.destination.resolve()
    source_root = args.source_root.resolve()

    if not log_dir.is_dir():
        raise SystemExit(f"Error: log directory not found: {log_dir}")
    if destination == log_dir or destination in log_dir.parents:
        raise SystemExit("Error: destination cannot be within the log directory.")

    successful: dict[Path, Path] = {}
    for log_path in iter_logs(log_dir):
        try:
            content = log_path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            content = log_path.read_text(encoding="utf-8", errors="ignore")
        lines = content.splitlines()
        if not log_proved_all(lines):
            continue
        source_str = extract_source_path(lines)
        if not source_str:
            print(f"Warning: could not determine source file for {log_path}")
            continue
        abs_path, rel_path = resolve_paths(source_str, source_root)
        if abs_path in successful:
            continue
        successful[abs_path] = rel_path

    if not successful:
        print("No logs with fully proved goals were found.")
        return

    destination.mkdir(parents=True, exist_ok=True)
    counts: Counter[str] = Counter()
    for abs_path, rel_path in successful.items():
        counts[benchmark_key(rel_path)] += 1
        if not abs_path.exists():
            print(f"Warning: source missing, skipping: {abs_path}")
            continue
        target = destination / rel_path
        if args.dry_run:
            print(f"[DRY-RUN] {abs_path} -> {target}")
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(abs_path, target)
        print(f"Copied {abs_path} -> {target}")

    write_stats(counts, destination, args.stats_file, args.dry_run)
    print(f"Done. Sources copied: {len(successful)}")


if __name__ == "__main__":
    main()
