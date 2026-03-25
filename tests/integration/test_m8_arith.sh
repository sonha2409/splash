#!/bin/bash
# Integration tests for 8.10: Arithmetic expansion

SHELL_BIN="./splash"
PASS=0
FAIL=0

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

echo "=== Milestone 8.10: Arithmetic expansion ==="

# --- Basic operations ---

OUT=$(echo 'echo $((1 + 2))' | $SHELL_BIN 2>/dev/null)
assert_eq "addition" "3" "$OUT"

OUT=$(echo 'echo $((10 - 3))' | $SHELL_BIN 2>/dev/null)
assert_eq "subtraction" "7" "$OUT"

OUT=$(echo 'echo $((4 * 5))' | $SHELL_BIN 2>/dev/null)
assert_eq "multiplication" "20" "$OUT"

OUT=$(echo 'echo $((10 / 3))' | $SHELL_BIN 2>/dev/null)
assert_eq "integer division" "3" "$OUT"

OUT=$(echo 'echo $((10 % 3))' | $SHELL_BIN 2>/dev/null)
assert_eq "modulo" "1" "$OUT"

# --- Precedence ---

OUT=$(echo 'echo $((2 + 3 * 4))' | $SHELL_BIN 2>/dev/null)
assert_eq "mul before add" "14" "$OUT"

OUT=$(echo 'echo $((2 * 3 + 4))' | $SHELL_BIN 2>/dev/null)
assert_eq "mul then add" "10" "$OUT"

# --- Parentheses ---

OUT=$(echo 'echo $(( (1 + 2) * (3 + 4) ))' | $SHELL_BIN 2>/dev/null)
assert_eq "parenthesized groups" "21" "$OUT"

OUT=$(echo 'echo $(( ((2 + 3)) ))' | $SHELL_BIN 2>/dev/null)
assert_eq "nested parens" "5" "$OUT"

# --- Unary operators ---

OUT=$(echo 'echo $((-5 + 2))' | $SHELL_BIN 2>/dev/null)
assert_eq "unary minus" "-3" "$OUT"

OUT=$(echo 'echo $((+7))' | $SHELL_BIN 2>/dev/null)
assert_eq "unary plus" "7" "$OUT"

OUT=$(echo 'echo $((-(-3)))' | $SHELL_BIN 2>/dev/null)
assert_eq "double negation" "3" "$OUT"

# --- Variables ---

OUT=$(printf 'export x=10\necho $((x + 5))\n' | $SHELL_BIN 2>/dev/null)
assert_eq "bare variable" "15" "$OUT"

OUT=$(printf 'export x=10\necho $(($x * 2))\n' | $SHELL_BIN 2>/dev/null)
assert_eq "dollar variable" "20" "$OUT"

OUT=$(echo 'echo $((y + 1))' | $SHELL_BIN 2>/dev/null)
assert_eq "unset var is 0" "1" "$OUT"

# --- Inside double quotes ---

OUT=$(echo 'echo "result=$((3+4))"' | $SHELL_BIN 2>/dev/null)
assert_eq "in double quotes" "result=7" "$OUT"

OUT=$(echo 'echo "a=$((1+1)) b=$((2+2))"' | $SHELL_BIN 2>/dev/null)
assert_eq "multiple in quotes" "a=2 b=4" "$OUT"

# --- Whitespace handling ---

OUT=$(echo 'echo $((  1  +  2  ))' | $SHELL_BIN 2>/dev/null)
assert_eq "extra whitespace" "3" "$OUT"

OUT=$(echo 'echo $((1+2))' | $SHELL_BIN 2>/dev/null)
assert_eq "no whitespace" "3" "$OUT"

# --- Division by zero ---

OUT=$(echo 'echo $((1 / 0))' | $SHELL_BIN 2>&1 | tail -1)
assert_eq "div by zero yields 0" "0" "$OUT"

# --- Combined with other expansions ---

OUT=$(printf 'export n=5\necho "count=$((n * 2)) items"\n' | $SHELL_BIN 2>/dev/null)
assert_eq "arith in string context" "count=10 items" "$OUT"

# --- Summary ---

echo ""
echo "Results: $PASS passed, $FAIL failed"
if [ $FAIL -gt 0 ]; then
    exit 1
fi
