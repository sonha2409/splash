#!/bin/bash
# Integration tests for 7.9: Structured env builtin

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

echo "=== Structured env tests ==="

# Test 1: env shows table columns
output=$(echo "env" | $SHELL_BIN 2>/dev/null)
assert_contains "env shows key column" "key" "$output"
assert_contains "env shows value column" "value" "$output"
assert_contains "env shows separator" "─" "$output"

# Test 2: env shows standard environment variables
assert_contains "env shows HOME" "HOME" "$output"
assert_contains "env shows PATH" "PATH" "$output"
assert_contains "env shows USER" "USER" "$output"

# Test 3: env |> cat (auto-serialize)
output=$(echo "env |> cat" | $SHELL_BIN 2>/dev/null)
assert_contains "env |> cat shows key column" "key" "$output"
assert_contains "env |> cat shows HOME" "HOME" "$output"

echo ""
echo "Results: $PASS passed, $FAIL failed"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
