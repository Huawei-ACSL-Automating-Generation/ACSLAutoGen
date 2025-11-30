#!/usr/bin/env python3
"""
Scan every file under a directory and merge consecutive ACSL comment blocks.

Example (invalid):
    /*@
    requires a > 0;
    requires b > a;
    */
    /*@
      assigns \\nothing;
    */

Becomes:
    /*@
    requires a > 0;
    requires b > a;

      assigns \\nothing;
    */

Usage:
    python3 merge_split_acsl.py <directory>
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path
from typing import Tuple

BLOCK_ACSL = r"/\*@[\s\S]*?\*/"
LINE_ACSL = r"(?:[ \t]*//@\s?.*(?:\n|$))+"
ACSL_PAIR = re.compile(
    rf"({BLOCK_ACSL})(\s*)({BLOCK_ACSL}|{LINE_ACSL})", re.MULTILINE
)


def parse_args() -> Path:
    parser = argparse.ArgumentParser(
        description="Merge consecutive ACSL comment blocks inside files."
    )
    parser.add_argument("directory", help="Root directory to scan recursively.")
    return Path(parser.parse_args().directory).resolve()


def _strip_acsl_segment(segment: str) -> str:
    """Return the inner text of an ACSL block or line segment without markers."""
    if segment.lstrip().startswith("/*@"):
        inner = segment[3:-2]  # remove /*@ and */
        return inner.strip("\n")

    lines = []
    for line in segment.splitlines():
        match = re.match(r"\s*//@\s?(.*)", line)
        if match:
            lines.append(match.group(1))
    return "\n".join(lines).strip("\n")


def _merge_two_blocks(first: str, second: str) -> str:
    """Merge exactly two ACSL blocks into one."""
    inner_first = _strip_acsl_segment(first)
    inner_second = _strip_acsl_segment(second)

    parts = []
    if inner_first:
        parts.append(inner_first)
    if inner_second:
        if parts:
            parts.append("")  # blank line between sections
        parts.append(inner_second)

    merged_inner = "\n".join(parts).rstrip() + "\n"
    return "/*@\n" + merged_inner + "*/"


def merge_acsl_comments(text: str) -> Tuple[str, bool]:
    """
    Merge consecutive ACSL blocks (block or line style) separated only by whitespace.
    Repeats until no more eligible pairs are found.
    """
    changed = False
    while True:
        match = ACSL_PAIR.search(text)
        if not match:
            break
        merged = _merge_two_blocks(match.group(1), match.group(3))
        start, end = match.span()
        text = text[:start] + merged + text[end:]
        changed = True
    return text, changed


def process_file(path: Path) -> bool:
    try:
        original = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        original = path.read_text(encoding="utf-8", errors="ignore")

    updated, changed = merge_acsl_comments(original)
    if changed:
        path.write_text(updated, encoding="utf-8")
    return changed


def main() -> None:
    root = parse_args()
    if not root.is_dir():
        print(f"Error: '{root}' is not a directory.", file=sys.stderr)
        sys.exit(1)

    changed_files = 0
    for file_path in root.rglob("*"):
        if file_path.is_file():
            if process_file(file_path):
                changed_files += 1
                print(f"Fixed: {file_path}")

    print(f"Done. Files modified: {changed_files}")


if __name__ == "__main__":
    main()
