#!/bin/bash
# Integration tests for 8.11: subshell grouping ( ... )
# Verifies that ( ... ) forks: cd and setenv inside the subshell do not
# leak into the parent shell. Exit codes propagate.

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

echo "=== Milestone 8.11: subshell ( ... ) ==="

# --- Subshell sees parent environment ---

OUT=$(printf 'setenv x outer\n( echo seen=$x )\n' | $SHELL_BIN 2>/dev/null)
assert_eq "subshell inherits parent env" "seen=outer" "$OUT"

# --- Subshell setenv does NOT leak to parent ---

OUT=$(printf 'setenv x outer\n( setenv x inner )\necho parent=$x\n' | $SHELL_BIN 2>/dev/null)
assert_eq "subshell setenv does not leak" "parent=outer" "$OUT"

# --- Subshell cd does NOT leak to parent ---
# Use /tmp via /private/tmp normalization on macOS.
OUT=$(printf 'cd /tmp\n( cd / )\npwd\n' | $SHELL_BIN 2>/dev/null)
assert_eq "subshell cd does not leak" "/private/tmp" "$OUT"

# --- Subshell exit code propagates ---

OUT=$(printf '( true )\necho $?\n' | $SHELL_BIN 2>/dev/null)
assert_eq "subshell true → 0" "0" "$OUT"

OUT=$(printf '( false )\necho $?\n' | $SHELL_BIN 2>/dev/null)
assert_eq "subshell false → 1" "1" "$OUT"

# --- Multiple statements separated by ; inside subshell ---

OUT=$(echo '( echo a; echo b; echo c )' | $SHELL_BIN 2>/dev/null)
assert_eq "subshell with multiple statements" "a
b
c" "$OUT"

# --- Subshell as conditional in if ---

OUT=$(echo 'if ( true ); then echo yes; else echo no; fi' | $SHELL_BIN 2>/dev/null)
assert_eq "subshell true as if condition" "yes" "$OUT"

OUT=$(echo 'if ( false ); then echo yes; else echo no; fi' | $SHELL_BIN 2>/dev/null)
assert_eq "subshell false as if condition" "no" "$OUT"

# Note: redirection on subshell groups (e.g. `( cmds ) > file`) is currently
# a parser limitation in splash and is not tested here.

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
exit $FAIL
