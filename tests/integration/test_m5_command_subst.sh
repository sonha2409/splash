#!/bin/bash
# Integration tests for Milestone 5.8: Command Substitution

SHELL_BIN="${SHELL_BIN:-./splash-debug}"
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

echo "=== Milestone 5.8: Command Substitution ==="

# --- Basic substitution ---
echo "--- Basic substitution ---"

OUT=$(echo '/bin/echo $(/bin/echo hello)' | $SHELL_BIN 2>&1)
assert_eq "basic command subst" "hello" "$OUT"

OUT=$(echo '/bin/echo $(/usr/bin/whoami)' | $SHELL_BIN 2>&1)
EXPECTED=$(whoami)
assert_eq "whoami substitution" "$EXPECTED" "$OUT"

# --- Trailing newlines stripped ---
echo "--- Trailing newline stripping ---"

OUT=$(echo '/bin/echo $(/bin/echo hello)' | $SHELL_BIN 2>&1)
assert_eq "trailing newline stripped" "hello" "$OUT"

# --- Empty output ---
echo "--- Empty output ---"

OUT=$(echo '/bin/echo [$(/usr/bin/true)]' | $SHELL_BIN 2>&1)
assert_eq "empty command output" "[]" "$OUT"

# --- In double quotes (preserves spaces) ---
echo "--- In double quotes ---"

OUT=$(echo '/bin/echo "$(/bin/echo "hello   world")"' | $SHELL_BIN 2>&1)
assert_eq "subst in double quotes preserves spaces" "hello   world" "$OUT"

# --- In single quotes (literal) ---
echo "--- In single quotes (literal) ---"

OUT=$(printf "/bin/echo '\$(/bin/echo hello)'\n" | $SHELL_BIN 2>&1)
assert_eq "subst in single quotes literal" '$(/bin/echo hello)' "$OUT"

# --- Inline in word ---
echo "--- Inline in word ---"

OUT=$(echo '/bin/echo prefix$(/bin/echo MID)suffix' | $SHELL_BIN 2>&1)
assert_eq "subst inline in word" "prefixMIDsuffix" "$OUT"

# --- Nested substitution ---
echo "--- Nested substitution ---"

OUT=$(echo '/bin/echo $(/bin/echo $(/bin/echo nested))' | $SHELL_BIN 2>&1)
assert_eq "nested command subst" "nested" "$OUT"

# --- Multiple substitutions ---
echo "--- Multiple substitutions ---"

OUT=$(echo '/bin/echo $(/bin/echo a) $(/bin/echo b)' | $SHELL_BIN 2>&1)
assert_eq "multiple substitutions" "a b" "$OUT"

# --- Summary ---
echo ""
echo "test_m5_command_subst"
TOTAL=$((PASS + FAIL))
echo "  $TOTAL tests: $PASS passed, $FAIL failed"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
