#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

"$SCRIPT_DIR/compile.sh"
"$ROOT_DIR/build/src/ACSLG" --ast-only -extra-arg=-x -extra-arg=c -p "$ROOT_DIR/build" "$ROOT_DIR/test.c"
