#!/bin/bash
# Integration tests for 8.6: Shell functions

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

echo "=== Milestone 8.6: Shell Functions ==="

# --- Basic function definition and call ---

OUT=$(echo 'greet() { echo hello; }; greet' | $SHELL_BIN 2>/dev/null)
assert_eq "basic function" "hello" "$OUT"

OUT=$(echo 'f() { echo a; echo b; }; f' | $SHELL_BIN 2>/dev/null)
assert_eq "function with multiple commands" "a
b" "$OUT"

# --- Positional parameters ---

OUT=$(echo 'greet() { echo "hello $1"; }; greet world' | $SHELL_BIN 2>/dev/null)
assert_eq "function \$1" "hello world" "$OUT"

OUT=$(echo 'add() { echo "$1 $2"; }; add foo bar' | $SHELL_BIN 2>/dev/null)
assert_eq "function \$1 \$2" "foo bar" "$OUT"

OUT=$(echo 'f() { echo "$#"; }; f a b c' | $SHELL_BIN 2>/dev/null)
assert_eq "function \$#" "3" "$OUT"

OUT=$(echo 'f() { echo "$@"; }; f x y z' | $SHELL_BIN 2>/dev/null)
assert_eq "function \$@" "x y z" "$OUT"

OUT=$(echo 'f() { echo "$*"; }; f x y z' | $SHELL_BIN 2>/dev/null)
assert_eq "function \$*" "x y z" "$OUT"

OUT=$(echo 'f() { echo "$#"; }; f' | $SHELL_BIN 2>/dev/null)
assert_eq "function no args \$# is 0" "0" "$OUT"

OUT=$(echo 'f() { echo "x${1}y"; }; f mid' | $SHELL_BIN 2>/dev/null)
assert_eq "function \${1} braced" "xmidy" "$OUT"

# --- Function redefinition ---

OUT=$(echo 'f() { echo first; }; f; f() { echo second; }; f' | $SHELL_BIN 2>/dev/null)
assert_eq "function redefinition" "first
second" "$OUT"

# --- Nested function calls ---

OUT=$(echo 'outer() { echo "outer: $1"; inner $2; }; inner() { echo "inner: $1"; }; outer A B' | $SHELL_BIN 2>/dev/null)
assert_eq "nested function calls" "outer: A
inner: B" "$OUT"

# --- Parameter isolation ---

OUT=$(echo 'f() { echo "$1"; g; echo "$1"; }; g() { echo "$1"; }; f hello' | $SHELL_BIN 2>/dev/null)
assert_eq "param isolation across calls" "hello

hello" "$OUT"

# --- Recursive function ---
# Simple recursion: call with decremented arg via $(expr)
OUT=$(echo 'count() { if test "$1" = 0; then echo done; else echo "$1"; count $(expr "$1" - 1); fi; }; count 3' | $SHELL_BIN 2>/dev/null)
assert_eq "recursive function" "3
2
1
done" "$OUT"

# --- Function with control flow ---

OUT=$(echo 'check() { if test "$1" = yes; then echo ok; else echo no; fi; }; check yes; check no' | $SHELL_BIN 2>/dev/null)
assert_eq "function with if" "ok
no" "$OUT"

# --- Function with for loop ---

OUT=$(echo 'each() { for x in $1 $2 $3; do echo "item: $x"; done; }; each a b c' | $SHELL_BIN 2>/dev/null)
assert_eq "function with for loop" "item: a
item: b
item: c" "$OUT"

# --- $0 returns shell name ---

OUT=$(echo 'f() { echo "$0"; }; f' | $SHELL_BIN 2>/dev/null)
assert_eq "\$0 is splash" "splash" "$OUT"

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
exit $FAIL
