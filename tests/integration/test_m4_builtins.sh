#!/bin/bash
# Integration tests for Milestone 4: Builtins and Environment (4.1-4.6)

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

assert_contains() {
    local test_name="$1"
    local needle="$2"
    local haystack="$3"
    if echo "$haystack" | grep -q "$needle"; then
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
    if ! echo "$haystack" | grep -q "$needle"; then
        echo "  PASS: $test_name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $test_name"
        echo "    expected NOT to contain: $needle"
        echo "    actual: $(printf '%q' "$haystack")"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== Milestone 4: Builtins and Environment ==="

# --- 4.1 exit ---
echo "--- 4.1 exit ---"

OUT=$(echo "exit" | $SHELL_BIN 2>&1)
assert_contains "exit prints goodbye" "Viszontlátásra" "$OUT"

echo "exit 42" | $SHELL_BIN 2>/dev/null
assert_eq "exit with status code" "42" "$?"

echo "exit 0" | $SHELL_BIN 2>/dev/null
assert_eq "exit with status 0" "0" "$?"

# --- 4.2 cd ---
echo "--- 4.2 cd ---"

OUT=$(echo "cd /tmp" | $SHELL_BIN 2>&1)
# cd itself produces no output on success

OUT=$(printf 'cd /tmp\n/bin/pwd\n' | $SHELL_BIN 2>&1)
assert_contains "cd /tmp then pwd" "/tmp" "$OUT"

OUT=$(printf 'cd /tmp\ncd -\n' | $SHELL_BIN 2>&1)
# cd - should print previous directory

OUT=$(echo "cd /nonexistent_dir_xyz" | $SHELL_BIN 2>&1)
assert_contains "cd to nonexistent dir" "No such file" "$OUT"

# --- 4.3 printenv ---
echo "--- 4.3 printenv ---"

OUT=$(echo "printenv" | $SHELL_BIN 2>&1)
assert_contains "printenv shows HOME" "HOME=" "$OUT"
assert_contains "printenv shows PATH" "PATH=" "$OUT"

OUT=$(echo "printenv HOME" | HOME=/test/home $SHELL_BIN 2>&1)
assert_contains "printenv HOME shows value" "/test/home" "$OUT"

OUT=$(printf 'printenv NONEXISTENT_VAR_XYZ\n/bin/echo exitcode_check\n' | $SHELL_BIN 2>&1)
# printenv for nonexistent var should produce no output for that var
assert_not_contains "printenv nonexistent var no output" "NONEXISTENT" "$OUT"

# --- 4.4 setenv ---
echo "--- 4.4 setenv ---"

OUT=$(printf 'setenv SPLASH_TEST hello\nprintenv SPLASH_TEST\n' | $SHELL_BIN 2>&1)
assert_contains "setenv then printenv" "hello" "$OUT"

OUT=$(echo "setenv" | $SHELL_BIN 2>&1)
assert_contains "setenv no args error" "usage" "$OUT"

OUT=$(echo "setenv ONEARG" | $SHELL_BIN 2>&1)
assert_contains "setenv one arg error" "usage" "$OUT"

# --- 4.5 unsetenv ---
echo "--- 4.5 unsetenv ---"

OUT=$(printf 'setenv SPLASH_DEL bye\nunsetenv SPLASH_DEL\nprintenv SPLASH_DEL\n' | $SHELL_BIN 2>&1)
assert_not_contains "unsetenv removes var" "bye" "$OUT"

OUT=$(echo "unsetenv" | $SHELL_BIN 2>&1)
assert_contains "unsetenv no args error" "usage" "$OUT"

# --- 4.6 export ---
echo "--- 4.6 export ---"

OUT=$(printf 'export SPLASH_EXP=world\nprintenv SPLASH_EXP\n' | $SHELL_BIN 2>&1)
assert_contains "export VAR=VALUE" "world" "$OUT"

OUT=$(printf 'export SPLASH_EMPTY=\nprintenv\n' | $SHELL_BIN 2>&1)
# Should set empty value — printenv (all vars) should show SPLASH_EMPTY=
assert_contains "export VAR= sets empty" "SPLASH_EMPTY=" "$OUT"

OUT=$(echo "export" | $SHELL_BIN 2>&1)
assert_contains "export no args lists vars" "export" "$OUT"

# --- Summary ---
echo ""
echo "test_m4_builtins"
TOTAL=$((PASS + FAIL))
echo "  $TOTAL tests: $PASS passed, $FAIL failed"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
