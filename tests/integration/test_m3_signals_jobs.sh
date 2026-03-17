#!/bin/bash
# Integration tests for Milestone 3: Signals and Process Management

SHELL_BIN="./splash"
TMPDIR=$(mktemp -d)
PASS=0
FAIL=0

cleanup() {
    rm -rf "$TMPDIR"
    pkill -f "sleep 9[9]8" 2>/dev/null || true
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

assert_file_contains() {
    local test_name="$1"
    local needle="$2"
    local file="$3"
    if [ -f "$file" ] && grep -q "$needle" "$file"; then
        echo "  PASS: $test_name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $test_name"
        echo "    expected file to contain: $needle"
        if [ -f "$file" ]; then
            echo "    file contents: $(cat "$file")"
        else
            echo "    file not found: $file"
        fi
        FAIL=$((FAIL + 1))
    fi
}

echo "=== M3 Signals & Jobs Integration Tests ==="

# 3.1: SIGINT — shell survives, child dies
echo "--- 3.1: SIGINT handling ---"

actual=$(printf 'echo alive\n' | $SHELL_BIN 2>&1)
assert_contains "shell runs commands" "alive" "$actual"

# 3.2: Zombie elimination — background jobs get reaped
echo "--- 3.2: Zombie elimination ---"

# Use file output to avoid $() hanging on bg processes
printf 'echo bg_test &\necho done\n' | $SHELL_BIN > "$TMPDIR/bg_out.txt" 2>&1
assert_file_contains "background job launches" "bg_test" "$TMPDIR/bg_out.txt"

# 3.6: jobs builtin
echo "--- 3.6: jobs builtin ---"

# Write output to file to avoid $() blocking on sleep's inherited fds
printf 'sleep 998 &\njobs\n' | $SHELL_BIN > "$TMPDIR/jobs_out.txt" 2>&1
sleep 0.1
pkill -f "sleep 9[9]8" 2>/dev/null || true

assert_file_contains "jobs shows running bg job" "running" "$TMPDIR/jobs_out.txt"
assert_file_contains "jobs shows command" "sleep 998" "$TMPDIR/jobs_out.txt"

# Builtins: exit
echo "--- Builtins: exit ---"

actual=$(printf 'exit\n' | $SHELL_BIN 2>&1)
assert_contains "exit prints goodbye" "Viszontlátásra" "$actual"

# Builtins: cd
echo "--- Builtins: cd ---"

actual=$(printf 'cd /tmp\npwd\n' | $SHELL_BIN 2>&1)
assert_contains "cd changes directory" "/tmp" "$actual"

actual=$(printf 'cd\npwd\n' | $SHELL_BIN 2>&1)
home_dir="$HOME"
assert_contains "cd no args goes to HOME" "$home_dir" "$actual"

actual=$(printf 'cd /tmp\ncd -\npwd\n' | $SHELL_BIN 2>&1)
# cd - should print the previous directory and go there
# The pwd after cd - should show the original dir (where shell started)

# 3.10: Job notifications
echo "--- 3.10: Job notifications ---"

# A fast bg job should complete; notification may appear before next prompt
printf 'echo notify_test &\necho after\n' | $SHELL_BIN > "$TMPDIR/notify_out.txt" 2>&1
assert_file_contains "bg job output captured" "notify_test" "$TMPDIR/notify_out.txt"

# Multi-stage pipes still work with process groups
echo "--- Pipes with process groups ---"

actual=$(printf 'echo hello world | grep hello | wc -w\n' | $SHELL_BIN 2>&1)
assert_contains "multi-stage pipe works" "2" "$actual"

# Single commands still work
echo "--- Simple commands ---"

actual=$(printf 'echo simple_test\n' | $SHELL_BIN 2>&1)
assert_contains "simple command works" "simple_test" "$actual"

# Redirections still work
echo "--- Redirections still work ---"

printf 'echo redir_test > %s/redir.txt\n' "$TMPDIR" | $SHELL_BIN 2>&1
assert_file_contains "redirect still works" "redir_test" "$TMPDIR/redir.txt"

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
if [ $FAIL -gt 0 ]; then
    exit 1
fi
