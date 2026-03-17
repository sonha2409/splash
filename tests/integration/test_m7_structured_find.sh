#!/bin/bash
# Integration tests for 7.8: Structured find builtin

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

echo "=== Structured find tests ==="

# Set up test directory tree
mkdir -p "$TMPDIR/testdir/sub1/deep"
mkdir -p "$TMPDIR/testdir/sub2"
echo "hello" > "$TMPDIR/testdir/a.txt"
echo "world" > "$TMPDIR/testdir/sub1/b.txt"
echo "deep" > "$TMPDIR/testdir/sub1/deep/c.txt"
touch "$TMPDIR/testdir/sub2/.hidden"

# Test 1: find shows table columns
output=$(echo "find $TMPDIR/testdir" | $SHELL_BIN 2>/dev/null)
assert_contains "find shows path column" "path" "$output"
assert_contains "find shows name column" "name" "$output"
assert_contains "find shows size column" "size" "$output"
assert_contains "find shows type column" "type" "$output"
assert_contains "find shows separator" "─" "$output"

# Test 2: find lists files recursively
assert_contains "find shows a.txt" "a.txt" "$output"
assert_contains "find shows b.txt" "b.txt" "$output"
assert_contains "find shows c.txt (deep)" "c.txt" "$output"
assert_contains "find shows .hidden" ".hidden" "$output"

# Test 3: find shows directories
assert_contains "find shows sub1 dir" "sub1" "$output"
assert_contains "find shows sub2 dir" "sub2" "$output"
assert_contains "find shows deep dir" "deep" "$output"

# Test 4: find shows types
assert_contains "find shows file type" "file" "$output"
assert_contains "find shows dir type" "dir" "$output"

# Test 5: find shows full paths
assert_contains "find shows full path" "$TMPDIR/testdir/sub1/b.txt" "$output"

# Test 6: find on single file
output=$(echo "find $TMPDIR/testdir/a.txt" | $SHELL_BIN 2>/dev/null)
assert_contains "find single file shows name" "a.txt" "$output"

# Test 7: find on nonexistent
output=$(echo "find $TMPDIR/nonexistent" | $SHELL_BIN 2>&1)
assert_contains "find nonexistent shows error" "No such file" "$output"

# Test 8: find |> cat
output=$(echo "find $TMPDIR/testdir |> cat" | $SHELL_BIN 2>/dev/null)
assert_contains "find |> cat shows a.txt" "a.txt" "$output"

echo ""
echo "Results: $PASS passed, $FAIL failed"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
