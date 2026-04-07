#!/bin/bash
# Integration tests for 8.12: brace grouping { ...; }
# Verifies that brace groups run in the current shell context (no fork):
# env changes persist, cd persists, exit codes propagate.

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

echo "=== Milestone 8.12: brace group { ...; } ==="

# --- Sequential execution ---

OUT=$(echo '{ echo a; echo b; }' | $SHELL_BIN 2>/dev/null)
assert_eq "brace group: multi-command" "a
b" "$OUT"

# --- setenv inside brace group persists in current shell ---

OUT=$(printf '{ setenv y braced; }\necho $y\n' | $SHELL_BIN 2>/dev/null)
assert_eq "brace setenv persists" "braced" "$OUT"

# --- cd inside brace group persists in current shell ---

OUT=$(printf '{ cd /tmp; }\npwd\n' | $SHELL_BIN 2>/dev/null)
assert_eq "brace cd persists" "/private/tmp" "$OUT"

# --- Exit code of brace group is exit code of last command ---

OUT=$(printf '{ true; }\necho $?\n' | $SHELL_BIN 2>/dev/null)
assert_eq "brace group: true → 0" "0" "$OUT"

OUT=$(printf '{ false; }\necho $?\n' | $SHELL_BIN 2>/dev/null)
assert_eq "brace group: false → 1" "1" "$OUT"

OUT=$(printf '{ true; false; }\necho $?\n' | $SHELL_BIN 2>/dev/null)
assert_eq "brace group: last command wins" "1" "$OUT"

# --- Brace group as if condition ---

OUT=$(echo 'if { true; }; then echo yes; else echo no; fi' | $SHELL_BIN 2>/dev/null)
assert_eq "brace group: true as if condition" "yes" "$OUT"

OUT=$(echo 'if { false; }; then echo yes; else echo no; fi' | $SHELL_BIN 2>/dev/null)
assert_eq "brace group: false as if condition" "no" "$OUT"

# --- Brace group combined with && / || ---

OUT=$(echo '{ true; } && echo and-ok' | $SHELL_BIN 2>/dev/null)
assert_eq "brace group with &&" "and-ok" "$OUT"

OUT=$(echo '{ false; } || echo or-ok' | $SHELL_BIN 2>/dev/null)
assert_eq "brace group with ||" "or-ok" "$OUT"

# --- Brace group inside for loop body ---

OUT=$(echo 'for x in 1 2; do { echo a$x; echo b$x; }; done' | $SHELL_BIN 2>/dev/null)
assert_eq "brace group inside for" "a1
b1
a2
b2" "$OUT"

# Note: redirection on brace groups (e.g. `{ cmds; } > file`) is currently
# a parser limitation in splash and is not tested here.

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
exit $FAIL
