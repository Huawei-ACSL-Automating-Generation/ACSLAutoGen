#!/usr/bin/env python3
"""
Rewrite assert-like calls as ACSL annotations.

For every source file inside a directory, each line that contains only one of
`assert`, `__VERIFIER_assert`, or `static_assert` is replaced with the Frama-C
style `//@ assert(...)`. Lines that are currently inside block comments (between
`/*` and `*/`) are left untouched.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path
from typing import Iterable, Sequence, Tuple

ASSERT_LINE = re.compile(
    r"^(?P<indent>\s*)(?P<token>assert|__VERIFIER_assert|static_assert)"
    r"\s*\((?P<body>.+)\)\s*;\s*$"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert assert calls into //@ assert annotations."
    )
    parser.add_argument("directory", type=Path, help="Directory to scan recursively.")
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
        help="Only report files that would change.",
    )
    return parser.parse_args()


def split_line_ending(line: str) -> Tuple[str, str]:
    if line.endswith("\r\n"):
        return line[:-2], "\r\n"
    if line.endswith("\n"):
        return line[:-1], "\n"
    if line.endswith("\r"):
        return line[:-1], "\r"
    return line, ""


def update_block_state(content: str, in_block: bool) -> bool:
    idx = 0
    while idx < len(content):
        if in_block:
            end = content.find("*/", idx)
            if end == -1:
                return True
            idx = end + 2
            in_block = False
        else:
            start = content.find("/*", idx)
            if start == -1:
                break
            end = content.find("*/", start + 2)
            if end == -1:
                return True
            idx = end + 2
    return in_block


def transform_line(content: str, in_block: bool) -> str:
    stripped = content.lstrip()
    line_in_comment = in_block or stripped.startswith("//") or stripped.startswith("/*")
    if line_in_comment:
        return content

    match = ASSERT_LINE.match(content)
    if not match:
        return content

    expr = match.group("body").strip()
    if match.group("token") == "static_assert":
        expr = expr.split(",", 1)[0].strip()
    new_line = f"{match.group('indent')}//@ assert({expr});"
    return new_line


def replace_asserts(text: str) -> Tuple[str, int]:
    lines = text.splitlines(keepends=True)
    in_block = False
    changed = 0
    result: list[str] = []

    for line in lines:
        content, ending = split_line_ending(line)
        original_content = content
        new_content = transform_line(content, in_block)
        if new_content != content:
            changed += 1
        result.append(new_content + ending)
        in_block = update_block_state(original_content, in_block)

    return "".join(result), changed


def source_files(root: Path, extensions: Sequence[str]) -> Iterable[Path]:
    for path in root.rglob("*"):
        if path.is_file() and path.suffix in extensions:
            yield path


def process_file(path: Path, dry_run: bool) -> int:
    try:
        original = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        original = path.read_text(encoding="utf-8", errors="ignore")
    updated, count = replace_asserts(original)
    if count and not dry_run:
        path.write_text(updated, encoding="utf-8")
    if count:
        print(f"{'Would update' if dry_run else 'Updated'}: {path} ({count} lines)")
    return count


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
        print("No assertions converted.")
    else:
        print(f"Done. Lines converted: {total}")


if __name__ == "__main__":
    main()
