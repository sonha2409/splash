#!/bin/bash
# Integration tests for Milestone 1: core foundation
# Verifies argv handling, exit codes, multi-stage pipes, and external commands.

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

echo "=== Milestone 1: Core Foundation ==="

# --- Single command, single arg ---

OUT=$(echo 'echo hello' | $SHELL_BIN 2>/dev/null)
assert_eq "echo single word" "hello" "$OUT"

OUT=$(echo 'echo hello world' | $SHELL_BIN 2>/dev/null)
assert_eq "echo multiple words" "hello world" "$OUT"

# --- External command via PATH ---

OUT=$(echo '/bin/echo from absolute path' | $SHELL_BIN 2>/dev/null)
assert_eq "external command via absolute path" "from absolute path" "$OUT"

OUT=$(echo 'true' | $SHELL_BIN 2>/dev/null; echo $?)
assert_eq "true builtin/external exits 0" "0" "$OUT"

# --- Exit codes preserved through $? ---

OUT=$(printf 'true\necho $?\n' | $SHELL_BIN 2>/dev/null)
assert_eq "true sets \$? to 0" "0" "$OUT"

OUT=$(printf 'false\necho $?\n' | $SHELL_BIN 2>/dev/null)
assert_eq "false sets \$? to 1" "1" "$OUT"

# --- Multi-stage pipeline ---

OUT=$(echo 'echo hello | tr a-z A-Z' | $SHELL_BIN 2>/dev/null)
assert_eq "two-stage pipeline" "HELLO" "$OUT"

OUT=$(echo 'echo hello | tr a-z A-Z | tr H X' | $SHELL_BIN 2>/dev/null)
assert_eq "three-stage pipeline" "XELLO" "$OUT"

# --- EOF handling: empty stdin should not crash ---

OUT=$(printf '' | $SHELL_BIN 2>/dev/null; echo "rc=$?")
assert_eq "empty stdin exits cleanly" "rc=0" "$OUT"

# --- Whitespace and blank lines tolerated ---

OUT=$(printf '\n\necho ok\n\n' | $SHELL_BIN 2>/dev/null)
assert_eq "blank lines are skipped" "ok" "$OUT"

OUT=$(echo '   echo   spaced   ' | $SHELL_BIN 2>/dev/null)
assert_eq "extra whitespace collapsed" "spaced" "$OUT"

# --- Command not found ---

OUT=$(printf 'xyz_no_such_cmd_42\necho $?\n' | $SHELL_BIN 2>/dev/null)
assert_eq "missing command sets non-zero \$?" "127" "$OUT"

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
exit $FAIL
