#!/bin/bash
# Integration tests for Milestone 5.7: Wildcarding (glob expansion)

SHELL_BIN="${SHELL_BIN:-./splash-debug}"
PASS=0
FAIL=0
TMPDIR=$(mktemp -d /tmp/splash_glob_XXXXXX)

# Create test files
touch "$TMPDIR/foo.c" "$TMPDIR/bar.c" "$TMPDIR/main.c"
touch "$TMPDIR/readme.txt" "$TMPDIR/.hidden"
mkdir "$TMPDIR/subdir"

assert_eq() {
    local test_name="$1"
    local expected="$2"
    local actual="$3"
    if [ "$expected" = "$actual" ]; then
        echo "  PASS: $test_name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $test_name"
        echo "    expected: $(printf '%q' "$expected")"
        echo "    actual:   $(printf '%q' "$actual")"
        FAIL=$((FAIL + 1))
    fi
}

assert_contains() {
    local test_name="$1"
    local needle="$2"
    local haystack="$3"
    if echo "$haystack" | grep -qF "$needle"; then
        echo "  PASS: $test_name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $test_name"
        echo "    expected to contain: $needle"
        echo "    actual: $(printf '%q' "$haystack")"
        FAIL=$((FAIL + 1))
    fi
}

assert_not_contains() {
    local test_name="$1"
    local needle="$2"
    local haystack="$3"
    if echo "$haystack" | grep -qF "$needle"; then
        echo "  FAIL: $test_name"
        echo "    should NOT contain: $needle"
        echo "    actual: $(printf '%q' "$haystack")"
        FAIL=$((FAIL + 1))
    else
        echo "  PASS: $test_name"
        PASS=$((PASS + 1))
    fi
}

echo "=== Milestone 5.7: Wildcarding ==="

# --- Star glob ---
echo "--- Star glob ---"

OUT=$(echo "/bin/echo $TMPDIR/*.c" | $SHELL_BIN 2>&1)
assert_contains "*.c matches bar.c" "$TMPDIR/bar.c" "$OUT"
assert_contains "*.c matches foo.c" "$TMPDIR/foo.c" "$OUT"
assert_contains "*.c matches main.c" "$TMPDIR/main.c" "$OUT"
assert_not_contains "*.c does not match .txt" "readme.txt" "$OUT"

# --- Question mark glob ---
echo "--- Question mark glob ---"

OUT=$(echo "/bin/echo $TMPDIR/???.c" | $SHELL_BIN 2>&1)
assert_contains "???.c matches bar.c" "$TMPDIR/bar.c" "$OUT"
assert_contains "???.c matches foo.c" "$TMPDIR/foo.c" "$OUT"
assert_not_contains "???.c does not match main.c" "$TMPDIR/main.c" "$OUT"

# --- No hidden files ---
echo "--- Hidden file exclusion ---"

OUT=$(echo "/bin/echo $TMPDIR/*" | $SHELL_BIN 2>&1)
assert_not_contains "* does not match .hidden" ".hidden" "$OUT"

# --- Quoted globs are literal ---
echo "--- Quoted globs literal ---"

OUT=$(echo "/bin/echo '$TMPDIR/*.c'" | $SHELL_BIN 2>&1)
assert_eq "single-quoted glob is literal" "$TMPDIR/*.c" "$OUT"

OUT=$(echo "/bin/echo \"$TMPDIR/*.c\"" | $SHELL_BIN 2>&1)
assert_eq "double-quoted glob is literal" "$TMPDIR/*.c" "$OUT"

# --- No match keeps literal ---
echo "--- No match keeps literal ---"

OUT=$(echo "/bin/echo $TMPDIR/*.xyz" | $SHELL_BIN 2>&1)
assert_eq "no match keeps literal" "$TMPDIR/*.xyz" "$OUT"

# --- Cleanup ---
rm -rf "$TMPDIR"

# --- Summary ---
echo ""
echo "test_m5_wildcarding"
TOTAL=$((PASS + FAIL))
echo "  $TOTAL tests: $PASS passed, $FAIL failed"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
