#!/usr/bin/env python3
"""Insert //@ assigns \nothing; before unknown* declarations."""

from __future__ import annotations

import argparse
import re
from pathlib import Path
from typing import Iterable, Sequence

DECL_RE = re.compile(r"^(?P<indent>\s*)int\s+(unknown(?:[1-4])?)\s*\(\s*\)\s*;\s*$")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Ensure each 'int unknown[1-4]()' prototype is preceded by '//@ assigns \\nothing;'"
        )
    )
    parser.add_argument("directory", type=Path, help="Directory to process recursively.")
    parser.add_argument(
        "--extensions",
        "-e",
        nargs="+",
        default=[".c", ".h", ".i"],
        help="File extensions (with dot) to include (default: .c .h .i).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Report files that would change without modifying them.",
    )
    return parser.parse_args()


def needs_annotation(lines: list[str], idx: int) -> bool:
    prev = idx - 1
    while prev >= 0 and lines[prev].strip() == "":
        prev -= 1
    if prev >= 0 and lines[prev].lstrip().startswith("//@"):
        return False
    return True


def annotate_lines(lines: list[str]) -> int:
    i = 0
    inserted = 0
    while i < len(lines):
        line = lines[i]
        stripped_line = line.rstrip("\r\n")
        match = DECL_RE.match(stripped_line)
        if match and needs_annotation(lines, i):
            indent = match.group("indent")
            newline = "\n"
            if line.endswith("\r\n"):
                newline = "\r\n"
            elif line.endswith("\r"):
                newline = "\r"
            annotation = f"{indent}//@ assigns \\nothing;{newline}"
            lines.insert(i, annotation)
            inserted += 1
            i += 1  # skip the annotation we just inserted
        i += 1
    return inserted


def process_file(path: Path, dry_run: bool) -> int:
    try:
        content = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        content = path.read_text(encoding="utf-8", errors="ignore")

    lines = content.splitlines(keepends=True)
    inserted = annotate_lines(lines)
    if inserted and not dry_run:
        path.write_text("".join(lines), encoding="utf-8")
    if inserted:
        print(f"{'Would update' if dry_run else 'Updated'}: {path} (+{inserted} annotations)")
    return inserted


def source_files(root: Path, extensions: Sequence[str]) -> Iterable[Path]:
    for path in root.rglob("*"):
        if path.is_file() and path.suffix in extensions:
            yield path


def main() -> None:
    args = parse_args()
    root = args.directory.resolve()
    if not root.is_dir():
        raise SystemExit(f"Error: '{root}' is not a directory.")

    extensions = tuple(args.extensions)
    total = 0
    for file_path in source_files(root, extensions):
        total += process_file(file_path, args.dry_run)

    if total == 0:
        print("No annotations added.")
    else:
        print(f"Done. Annotations added: {total}")


if __name__ == "__main__":
    main()
