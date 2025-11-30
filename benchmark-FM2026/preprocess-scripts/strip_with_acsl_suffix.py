#!/usr/bin/env python3
"""Copy *_with_acsl* files to a mirror tree without the suffix."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Copy files whose names end with a specific suffix into a new "
            "directory, removing the suffix while keeping the relative "
            "directory layout."
        )
    )
    parser.add_argument("source", type=Path, help="Directory to scan recursively.")
    parser.add_argument("destination", type=Path, help="Directory to write outputs.")
    parser.add_argument(
        "--suffix",
        default="_with_acsl",
        help="Filename suffix to remove before the extension (default: %(default)s).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show the planned copies without writing files",
    )
    return parser.parse_args()


def _trim_suffix(name: str, suffix: str) -> str | None:
    if not name.endswith(suffix):
        return None
    trimmed = name[: -len(suffix)]
    return trimmed or None


def target_name(path: Path, suffix: str) -> str | None:
    suffix = suffix or ""
    if not suffix:
        return None
    full_ext = "".join(path.suffixes)
    stem_with_marker = path.name[: len(path.name) - len(full_ext)] if full_ext else path.name
    trimmed = _trim_suffix(stem_with_marker, suffix)
    if trimmed is not None:
        return trimmed + full_ext
    return _trim_suffix(path.name, suffix)


def copy_files(src: Path, dst: Path, suffix: str, dry_run: bool) -> int:
    count = 0
    for file_path in src.rglob("*"):
        if not file_path.is_file():
            continue
        relative = file_path.relative_to(src)
        new_name = target_name(file_path, suffix)
        if not new_name:
            continue
        target_rel = relative.parent / new_name if relative.parent != Path('.') else Path(new_name)
        target_path = dst / target_rel
        if target_path.exists():
            raise SystemExit(f"Error: target already exists: {target_path}")
        if not dry_run:
            target_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file_path, target_path)
        print(("Would write" if dry_run else "Wrote"), f"{target_path}")
        count += 1
    return count


def main() -> None:
    args = parse_args()
    source = args.source.resolve()
    destination = args.destination.resolve()

    if not source.is_dir():
        raise SystemExit(f"Error: source directory not found: {source}")
    if destination == source or source in destination.parents:
        raise SystemExit("Error: destination must be outside/independent from source")

    copied = copy_files(source, destination, args.suffix, args.dry_run)
    print(f"Matched files: {copied}")


if __name__ == "__main__":
    main()
