#!/bin/bash
# Integration tests for 8.1: Command lists (;, &&, ||)

SHELL_BIN="./splash"
TMPDIR=$(mktemp -d)
PASS=0
FAIL=0

cleanup() {
    rm -rf "$TMPDIR"
}
trap cleanup EXIT

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

echo "=== Milestone 8.1: Command Lists ==="

# --- Semicolon (sequential execution) ---

OUT=$(echo 'echo hello ; echo world' | $SHELL_BIN 2>/dev/null)
assert_eq "semicolon: two commands" "hello
world" "$OUT"

OUT=$(echo 'echo a ; echo b ; echo c' | $SHELL_BIN 2>/dev/null)
assert_eq "semicolon: three commands" "a
b
c" "$OUT"

OUT=$(echo 'echo only ;' | $SHELL_BIN 2>/dev/null)
assert_eq "semicolon: trailing semicolon" "only" "$OUT"

# --- AND operator (&&) ---

OUT=$(echo 'true && echo yes' | $SHELL_BIN 2>/dev/null)
assert_eq "and: true && echo" "yes" "$OUT"

OUT=$(echo 'false && echo no' | $SHELL_BIN 2>/dev/null)
assert_eq "and: false && echo (skipped)" "" "$OUT"

OUT=$(echo 'true && true && echo chained' | $SHELL_BIN 2>/dev/null)
assert_eq "and: chained &&" "chained" "$OUT"

OUT=$(echo 'true && false && echo nope' | $SHELL_BIN 2>/dev/null)
assert_eq "and: chain breaks on false" "" "$OUT"

# --- OR operator (||) ---

OUT=$(echo 'false || echo fallback' | $SHELL_BIN 2>/dev/null)
assert_eq "or: false || echo" "fallback" "$OUT"

OUT=$(echo 'true || echo skip' | $SHELL_BIN 2>/dev/null)
assert_eq "or: true || echo (skipped)" "" "$OUT"

OUT=$(echo 'false || false || echo last' | $SHELL_BIN 2>/dev/null)
assert_eq "or: chained ||" "last" "$OUT"

# --- Mixed operators ---

OUT=$(echo 'true && echo ok || echo nope' | $SHELL_BIN 2>/dev/null)
assert_eq "mixed: true && echo || echo" "ok" "$OUT"

OUT=$(echo 'false && echo nope || echo fallback' | $SHELL_BIN 2>/dev/null)
assert_eq "mixed: false && echo || echo" "fallback" "$OUT"

OUT=$(echo 'echo first ; true && echo second' | $SHELL_BIN 2>/dev/null)
assert_eq "mixed: semicolon then &&" "first
second" "$OUT"

OUT=$(echo 'false || echo recovered ; echo always' | $SHELL_BIN 2>/dev/null)
assert_eq "mixed: || then semicolon" "recovered
always" "$OUT"

# --- With pipes ---

OUT=$(echo 'echo hello | tr a-z A-Z ; echo world' | $SHELL_BIN 2>/dev/null)
assert_eq "pipe then semicolon" "HELLO
world" "$OUT"

OUT=$(echo 'true && echo hello | tr a-z A-Z' | $SHELL_BIN 2>/dev/null)
assert_eq "and then pipe" "HELLO" "$OUT"

# --- Summary ---
echo ""
echo "  Results: $PASS passed, $FAIL failed"
if [ $FAIL -ne 0 ]; then
    exit 1
fi
