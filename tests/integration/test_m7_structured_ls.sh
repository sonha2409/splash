#!/bin/bash
# Integration tests for Milestone 7.5-7.6: Structured ls and auto-serialize

SHELL_BIN="./splash"
TMPDIR=$(mktemp -d)
PASS=0
FAIL=0

cleanup() {
    rm -rf "$TMPDIR"
}
trap cleanup EXIT

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
    if echo "$haystack" | grep -q "$needle"; then
        echo "  FAIL: $test_name"
        echo "    expected NOT to contain: $needle"
        FAIL=$((FAIL + 1))
    else
        echo "  PASS: $test_name"
        PASS=$((PASS + 1))
    fi
}

echo "=== Structured ls tests ==="

# Set up test directory with known contents
mkdir -p "$TMPDIR/testdir/subdir"
echo "hello" > "$TMPDIR/testdir/file1.txt"
echo "world!!" > "$TMPDIR/testdir/file2.txt"
touch "$TMPDIR/testdir/.hidden"

# Test 1: standalone ls shows table columns
output=$(echo "ls $TMPDIR/testdir" | $SHELL_BIN 2>/dev/null)
assert_contains "ls shows name column header" "name" "$output"
assert_contains "ls shows size column header" "size" "$output"
assert_contains "ls shows permissions column header" "permissions" "$output"
assert_contains "ls shows modified column header" "modified" "$output"
assert_contains "ls shows type column header" "type" "$output"

# Test 2: ls shows files and dirs
assert_contains "ls shows file1.txt" "file1.txt" "$output"
assert_contains "ls shows file2.txt" "file2.txt" "$output"
assert_contains "ls shows subdir" "subdir" "$output"
assert_contains "ls shows .hidden" ".hidden" "$output"

# Test 3: ls shows types correctly
assert_contains "ls shows dir type" "dir" "$output"
assert_contains "ls shows file type" "file" "$output"

# Test 4: ls skips . and ..
assert_not_contains "ls skips ." '^\.' "$output"

# Test 5: ls shows separator line
assert_contains "ls shows separator" "─" "$output"

# Test 6: ls on a single file
output=$(echo "ls $TMPDIR/testdir/file1.txt" | $SHELL_BIN 2>/dev/null)
assert_contains "ls single file shows name" "file1.txt" "$output"
assert_contains "ls single file shows size" "6" "$output"

# Test 7: ls on nonexistent path (should give error, empty table)
output=$(echo "ls $TMPDIR/nonexistent" | $SHELL_BIN 2>&1)
assert_contains "ls nonexistent shows error" "No such file" "$output"

# Test 8: ls |> cat (auto-serialize through pipe)
output=$(echo "ls $TMPDIR/testdir |> cat" | $SHELL_BIN 2>/dev/null)
assert_contains "ls |> cat shows file1.txt" "file1.txt" "$output"
assert_contains "ls |> cat shows name header" "name" "$output"

echo ""
echo "Results: $PASS passed, $FAIL failed"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
