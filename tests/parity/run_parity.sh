#!/usr/bin/env bash
# run_parity.sh — cross-language semantic parity check
#
# Builds the C parity runner, runs both C and Python runners, and diffs
# their output.  Exits 0 if outputs are identical, 1 otherwise.
#
# Usage (from repo root):
#   bash tests/parity/run_parity.sh
#
# Prerequisites:
#   - A C toolchain (cc/make)
#   - uv (for Python runner)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
C_DIR="$REPO_ROOT/c"
PY_DIR="$REPO_ROOT/python"
TMP_DIR="$(mktemp -d)"
C_OUT="$TMP_DIR/c_trace.txt"
PY_OUT="$TMP_DIR/py_trace.txt"

cleanup() { rm -rf "$TMP_DIR"; }
trap cleanup EXIT

echo "=== rxnet parity check ==="

# Build C parity runner
echo "Building C parity runner..."
make -C "$C_DIR" parity --silent

# Run C runner
echo "Running C runner..."
"$C_DIR/build/parity_runner" > "$C_OUT"

# Run Python runner
echo "Running Python runner..."
(cd "$PY_DIR" && uv run tests/parity_runner.py) > "$PY_OUT"

# Compare
echo ""
echo "--- C output ($( wc -l < "$C_OUT" | tr -d ' ') lines) ---"
cat "$C_OUT"

echo ""
echo "--- Python output ($(wc -l < "$PY_OUT" | tr -d ' ') lines) ---"
cat "$PY_OUT"

echo ""
if diff -u "$C_OUT" "$PY_OUT" > /dev/null 2>&1; then
    echo "PASS: C and Python outputs are identical."
    exit 0
else
    echo "FAIL: outputs differ:"
    diff -u "$C_OUT" "$PY_OUT" || true
    exit 1
fi
