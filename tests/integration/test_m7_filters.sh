#!/bin/bash
# Integration tests for 7.11-7.14: sort, select, first/last, count filters

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
    if echo "$haystack" | grep -q "$needle"; then
        echo "  FAIL: $test_name"
        echo "    expected NOT to contain: $needle"
        FAIL=$((FAIL + 1))
    else
        echo "  PASS: $test_name"
        PASS=$((PASS + 1))
    fi
}

echo "=== sort / select / first / last / count filter tests ==="

# Set up test directory with known files
mkdir -p "$TMPDIR/testdir"
echo "aaa" > "$TMPDIR/testdir/charlie.txt"
dd if=/dev/zero of="$TMPDIR/testdir/alpha.bin" bs=100 count=1 2>/dev/null
echo "bb" > "$TMPDIR/testdir/bravo.txt"

# --- sort tests ---

# Test sort by name ascending
output=$(echo "ls $TMPDIR/testdir |> sort name" | $SHELL_BIN 2>/dev/null)
first_data=$(echo "$output" | grep -v "^─" | grep -v "name" | head -1)
assert_contains "sort name asc: alpha first" "alpha" "$first_data"

# Test sort by name descending
output=$(echo "ls $TMPDIR/testdir |> sort name --desc" | $SHELL_BIN 2>/dev/null)
first_data=$(echo "$output" | grep -v "^─" | grep -v "name" | head -1)
assert_contains "sort name desc: charlie first" "charlie" "$first_data"

# Test sort by size
output=$(echo "ls $TMPDIR/testdir |> sort size --desc" | $SHELL_BIN 2>/dev/null)
first_data=$(echo "$output" | grep -v "^─" | grep -v "name" | head -1)
assert_contains "sort size desc: alpha.bin first (100 bytes)" "alpha" "$first_data"

# --- select tests ---

# Test select single column
output=$(echo "ls $TMPDIR/testdir |> select name" | $SHELL_BIN 2>/dev/null)
assert_contains "select name: has name header" "name" "$output"
assert_not_contains "select name: no size header" " size " "$output"
assert_contains "select name: has alpha.bin" "alpha" "$output"

# Test select multiple columns
output=$(echo "ls $TMPDIR/testdir |> select name size" | $SHELL_BIN 2>/dev/null)
assert_contains "select name size: has name" "name" "$output"
assert_contains "select name size: has size" "size" "$output"
assert_not_contains "select name size: no permissions" "permissions" "$output"

# --- first / last tests ---

# Test first 1
output=$(echo "ls $TMPDIR/testdir |> sort name |> first 1" | $SHELL_BIN 2>/dev/null)
data_lines=$(echo "$output" | grep -v "^─" | grep -v "^ *name" | grep -v "^$")
line_count=$(echo "$data_lines" | wc -l | tr -d ' ')
assert_eq "first 1: exactly 1 data row" "1" "$line_count"
assert_contains "first 1: shows alpha" "alpha" "$data_lines"

# Test last 1
output=$(echo "ls $TMPDIR/testdir |> sort name |> last 1" | $SHELL_BIN 2>/dev/null)
data_lines=$(echo "$output" | grep -v "^─" | grep -v "^ *name" | grep -v "^$")
line_count=$(echo "$data_lines" | wc -l | tr -d ' ')
assert_eq "last 1: exactly 1 data row" "1" "$line_count"
assert_contains "last 1: shows charlie" "charlie" "$data_lines"

# Test first 2
output=$(echo "ls $TMPDIR/testdir |> sort name |> first 2" | $SHELL_BIN 2>/dev/null)
data_lines=$(echo "$output" | grep -v "^─" | grep -v "^ *name" | grep -v "^$")
line_count=$(echo "$data_lines" | wc -l | tr -d ' ')
assert_eq "first 2: exactly 2 data rows" "2" "$line_count"

# --- count tests ---

# Test count
output=$(echo "ls $TMPDIR/testdir |> count" | $SHELL_BIN 2>/dev/null | tr -d '[:space:]')
assert_eq "count: 3 files" "3" "$output"

# --- chained filters ---

# Test where + sort + first
output=$(echo "ls $TMPDIR/testdir |> where type == file |> sort name |> first 2" | $SHELL_BIN 2>/dev/null)
assert_contains "chained: has alpha" "alpha" "$output"
assert_contains "chained: has bravo" "bravo" "$output"
assert_not_contains "chained: no charlie" "charlie" "$output"

# Test sort + select + count pipeline from spec
output=$(echo "ls $TMPDIR/testdir |> where type == file |> count" | $SHELL_BIN 2>/dev/null | tr -d '[:space:]')
assert_eq "where+count: 3 files" "3" "$output"

echo ""
echo "Results: $PASS passed, $FAIL failed"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
