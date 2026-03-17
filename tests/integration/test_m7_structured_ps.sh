#!/bin/bash
# Integration tests for 7.7: Structured ps builtin

SHELL_BIN="./splash"
PASS=0
FAIL=0

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

echo "=== Structured ps tests ==="

# Test 1: ps shows table columns
output=$(echo "ps" | $SHELL_BIN 2>/dev/null)
assert_contains "ps shows pid column" "pid" "$output"
assert_contains "ps shows name column" "name" "$output"
assert_contains "ps shows cpu_time column" "cpu_time" "$output"
assert_contains "ps shows mem column" "mem" "$output"
assert_contains "ps shows status column" "status" "$output"

# Test 2: ps shows separator
assert_contains "ps shows separator" "─" "$output"

# Test 3: ps lists at least some processes
assert_contains "ps shows running processes" "running" "$output"

# Test 4: ps lists known process (the shell itself runs as splash)
assert_contains "ps shows splash process" "splash" "$output"

# Test 5: ps |> cat (auto-serialize)
output=$(echo "ps |> cat" | $SHELL_BIN 2>/dev/null)
assert_contains "ps |> cat shows pid column" "pid" "$output"
assert_contains "ps |> cat shows running" "running" "$output"

echo ""
echo "Results: $PASS passed, $FAIL failed"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
