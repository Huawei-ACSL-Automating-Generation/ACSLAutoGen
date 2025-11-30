#!/usr/bin/env python3
"""
Ensure forward declarations for specific stubs exist with ACSL assigns clauses.

The script scans every source file within a directory. If the file contains a
call to one of the supported stub names but lacks the corresponding forward
declaration, the script prepends the following snippet to the file:

    //@ assigns \nothing;
    <return_type> <name>();

Multiple missing declarations are emitted in that order at the top of the file.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path
from typing import Iterable, Sequence, Tuple

DECL_SPECS: dict[str, str] = {
    # unknown* stubs return int
    "unknown": "int",
    "unknown1": "int",
    "unknown2": "int",
    "unknown3": "int",
    "unknown4": "int",
    # SV-COMP style helpers return void
    "__VERIFIER_nondet_int": "int",
    "assume": "void",
    "__VERIFIER_assume": "void",
}

CALL_RE = re.compile(
    r"\b("
    r"unknown(?:[1-4])?"
    r"|assume"
    r"|__VERIFIER_assume"
    r"|__VERIFIER_nondet_int"
    r")\s*\("
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Insert missing stub declarations with ACSL annotations."
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
        help="Report files that would change without editing them.",
    )
    return parser.parse_args()


def find_stub_calls(text: str) -> set[str]:
    return {match.group(1) for match in CALL_RE.finditer(text)}


def has_forward_declaration(text: str, name: str) -> bool:
    return_type = DECL_SPECS[name]
    pattern = re.compile(
        rf"\b{return_type}\s+{re.escape(name)}\s*\(\s*\)\s*;", re.MULTILINE
    )
    return bool(pattern.search(text))


def prepend_declarations(text: str, names: Sequence[str]) -> Tuple[str, int]:
    missing = [name for name in names if not has_forward_declaration(text, name)]
    if not missing:
        return text, 0

    lines = []
    for name in missing:
        lines.append("//@ assigns \\nothing;")
        lines.append(f"{DECL_SPECS[name]} {name}();")
        lines.append("")
    block = "\n".join(lines)
    if not block.endswith("\n"):
        block += "\n"

    if text.startswith("\ufeff"):
        return "\ufeff" + block + text[1:], len(missing)
    return block + text, len(missing)


def process_file(path: Path, dry_run: bool) -> int:
    try:
        original = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        original = path.read_text(encoding="utf-8", errors="ignore")

    call_names = sorted(find_stub_calls(original))
    if not call_names:
        return 0

    updated, added = prepend_declarations(original, call_names)
    if added and not dry_run:
        path.write_text(updated, encoding="utf-8")
    if added:
        print(f"{'Would update' if dry_run else 'Updated'}: {path} (+{added} declarations)")
    return added


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
        print("No declarations added.")
    else:
        print(f"Done. Declarations added: {total}")


if __name__ == "__main__":
    main()
