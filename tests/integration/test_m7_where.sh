#!/bin/bash
# Integration tests for 7.10: where filter

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

echo "=== where filter tests ==="

# Set up test directory
mkdir -p "$TMPDIR/testdir/subdir"
echo "hello" > "$TMPDIR/testdir/small.txt"
dd if=/dev/zero of="$TMPDIR/testdir/big.bin" bs=1024 count=2 2>/dev/null

# Test 1: where type == file
output=$(echo "ls $TMPDIR/testdir |> where type == file" | $SHELL_BIN 2>/dev/null)
assert_contains "where type==file shows small.txt" "small.txt" "$output"
assert_contains "where type==file shows big.bin" "big.bin" "$output"
assert_not_contains "where type==file excludes subdir" "subdir" "$output"

# Test 2: where type == dir
output=$(echo "ls $TMPDIR/testdir |> where type == dir" | $SHELL_BIN 2>/dev/null)
assert_contains "where type==dir shows subdir" "subdir" "$output"
assert_not_contains "where type==dir excludes small.txt" "small.txt" "$output"

# Test 3: where size > N (quoted operator)
output=$(echo "ls $TMPDIR/testdir |> where size \">\" 1000" | $SHELL_BIN 2>/dev/null)
assert_contains "where size>1000 shows big.bin" "big.bin" "$output"
assert_not_contains "where size>1000 excludes small.txt" "small.txt" "$output"

# Test 4: where with != operator
output=$(echo "ls $TMPDIR/testdir |> where name != small.txt" | $SHELL_BIN 2>/dev/null)
assert_not_contains "where name!=small.txt excludes it" "small.txt" "$output"
assert_contains "where name!=small.txt shows big.bin" "big.bin" "$output"

# Test 5: where with regex
output=$(echo "ls $TMPDIR/testdir |> where name =~ '\\.txt$'" | $SHELL_BIN 2>/dev/null)
assert_contains "where regex .txt shows small.txt" "small.txt" "$output"
assert_not_contains "where regex .txt excludes big.bin" "big.bin" "$output"

# Test 6: where preserves table structure
assert_contains "where preserves name header" "name" "$output"
assert_contains "where preserves separator" "─" "$output"

echo ""
echo "Results: $PASS passed, $FAIL failed"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
