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

# --- if/elif/else/fi ---

OUT=$(echo 'if true; then echo yes; fi' | $SHELL_BIN 2>/dev/null)
assert_eq "if: true branch" "yes" "$OUT"

OUT=$(echo 'if false; then echo no; fi' | $SHELL_BIN 2>/dev/null)
assert_eq "if: false branch (no output)" "" "$OUT"

OUT=$(echo 'if false; then echo no; else echo yes; fi' | $SHELL_BIN 2>/dev/null)
assert_eq "if-else: else branch" "yes" "$OUT"

OUT=$(echo 'if true; then echo yes; else echo no; fi' | $SHELL_BIN 2>/dev/null)
assert_eq "if-else: if branch" "yes" "$OUT"

OUT=$(echo 'if false; then echo a; elif true; then echo b; else echo c; fi' | $SHELL_BIN 2>/dev/null)
assert_eq "if-elif-else: elif branch" "b" "$OUT"

OUT=$(echo 'if false; then echo a; elif false; then echo b; else echo c; fi' | $SHELL_BIN 2>/dev/null)
assert_eq "if-elif-else: else branch" "c" "$OUT"

OUT=$(echo 'if false; then echo a; elif false; then echo b; elif true; then echo c; fi' | $SHELL_BIN 2>/dev/null)
assert_eq "if-elif-elif: third branch" "c" "$OUT"

OUT=$(echo 'if true; then echo a; echo b; fi' | $SHELL_BIN 2>/dev/null)
assert_eq "if: multi-command body" "a
b" "$OUT"

OUT=$(echo 'if true; then echo yes; fi ; echo done' | $SHELL_BIN 2>/dev/null)
assert_eq "if then semicolon" "yes
done" "$OUT"

OUT=$(echo 'if false; then echo no; fi && echo ok' | $SHELL_BIN 2>/dev/null)
assert_eq "if with && (if returns 0)" "ok" "$OUT"

OUT=$(echo 'if true && false; then echo no; else echo yes; fi' | $SHELL_BIN 2>/dev/null)
assert_eq "if: compound condition" "yes" "$OUT"

OUT=$(echo 'if true; then if false; then echo a; else echo b; fi; fi' | $SHELL_BIN 2>/dev/null)
assert_eq "nested if" "b" "$OUT"

OUT=$(echo 'if true; then echo hello | tr a-z A-Z; fi' | $SHELL_BIN 2>/dev/null)
assert_eq "if: pipe in body" "HELLO" "$OUT"

# --- Summary ---
echo ""
echo "  Results: $PASS passed, $FAIL failed"
if [ $FAIL -ne 0 ]; then
    exit 1
fi
