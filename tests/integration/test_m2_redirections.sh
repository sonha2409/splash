#!/bin/bash
# Integration tests for Milestone 2: I/O Redirections (features 2.1–2.6)

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

assert_file_exists() {
    local test_name="$1"
    local file="$2"
    if [ -f "$file" ]; then
        echo "  PASS: $test_name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $test_name (file not found: $file)"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== M2 Redirection Integration Tests ==="

# 2.1: Output redirection >
echo "--- 2.1: Output redirection > ---"

echo "echo hello > $TMPDIR/out1.txt" | $SHELL_BIN
assert_file_exists "output redirect creates file" "$TMPDIR/out1.txt"
assert_eq "output redirect content" "hello" "$(cat "$TMPDIR/out1.txt")"

echo "echo first > $TMPDIR/out2.txt" | $SHELL_BIN
echo "echo second > $TMPDIR/out2.txt" | $SHELL_BIN
assert_eq "output redirect truncates" "second" "$(cat "$TMPDIR/out2.txt")"

# 2.2: Append redirection >>
echo "--- 2.2: Append redirection >> ---"

echo "echo line1 > $TMPDIR/append.txt" | $SHELL_BIN
echo "echo line2 >> $TMPDIR/append.txt" | $SHELL_BIN
expected=$(printf 'line1\nline2')
assert_eq "append redirect adds line" "$expected" "$(cat "$TMPDIR/append.txt")"

# 2.3: Input redirection <
echo "--- 2.3: Input redirection < ---"

printf 'hello from file\n' > "$TMPDIR/input.txt"
actual=$(echo "cat < $TMPDIR/input.txt" | $SHELL_BIN)
assert_eq "input redirect reads file" "hello from file" "$actual"

# 2.4: Stderr redirection 2>
echo "--- 2.4: Stderr redirection 2> ---"

echo "ls /nonexistent_path_xyz 2> $TMPDIR/err.txt" | $SHELL_BIN
assert_file_exists "stderr redirect creates file" "$TMPDIR/err.txt"
# The file should contain an error message (non-empty)
errsize=$(wc -c < "$TMPDIR/err.txt" | tr -d ' ')
if [ "$errsize" -gt 0 ]; then
    echo "  PASS: stderr redirect captures error output"
    PASS=$((PASS + 1))
else
    echo "  FAIL: stderr redirect file is empty"
    FAIL=$((FAIL + 1))
fi

# 2.5: Combined stdout+stderr >&
echo "--- 2.5: Combined stdout+stderr >& ---"

echo "ls /nonexistent_path_xyz >& $TMPDIR/both.txt" | $SHELL_BIN
assert_file_exists "combined redirect creates file" "$TMPDIR/both.txt"

# 2.6: Combined append >>&
echo "--- 2.6: Combined append >>& ---"

echo "echo first >& $TMPDIR/bothappend.txt" | $SHELL_BIN
echo "echo second >>& $TMPDIR/bothappend.txt" | $SHELL_BIN
actual=$(cat "$TMPDIR/bothappend.txt")
expected=$(printf 'first\nsecond')
assert_eq "combined append works" "$expected" "$actual"

# Mixed: redirect with pipe
echo "--- Mixed: redirect + pipe ---"

printf 'apple\nbanana\ncherry\n' > "$TMPDIR/fruits.txt"
actual=$(echo "cat < $TMPDIR/fruits.txt | grep banana" | $SHELL_BIN)
assert_eq "input redirect with pipe" "banana" "$actual"

echo "ls /bin | grep sh > $TMPDIR/pipe_out.txt" | $SHELL_BIN
assert_file_exists "pipe with output redirect creates file" "$TMPDIR/pipe_out.txt"

# Multiple redirects on one command
echo "--- Multiple redirects ---"

printf 'test input\n' > "$TMPDIR/multi_in.txt"
echo "cat < $TMPDIR/multi_in.txt > $TMPDIR/multi_out.txt" | $SHELL_BIN
assert_eq "input+output redirect" "test input" "$(cat "$TMPDIR/multi_out.txt")"

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
if [ $FAIL -gt 0 ]; then
    exit 1
fi
