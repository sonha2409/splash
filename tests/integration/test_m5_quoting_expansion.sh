#!/bin/bash
# Integration tests for Milestone 5: Quoting, Escaping, and Expansions (5.1-5.6)

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

assert_contains() {
    local test_name="$1"
    local needle="$2"
    local haystack="$3"
    if echo "$haystack" | grep -qF "$needle"; then
        echo "  PASS: $test_name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $test_name"
        echo "    expected to contain: $needle"
        echo "    actual: $(printf '%q' "$haystack")"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== Milestone 5: Quoting, Escaping, and Expansions ==="

# --- 5.1 Double quotes ---
echo "--- 5.1 Double quotes ---"

OUT=$(echo '/bin/echo "hello world"' | $SHELL_BIN 2>&1)
assert_eq "double quotes preserve spaces" "hello world" "$OUT"

OUT=$(echo '/bin/echo "one   two   three"' | $SHELL_BIN 2>&1)
assert_eq "double quotes preserve multiple spaces" "one   two   three" "$OUT"

OUT=$(echo '/bin/echo "hello" "world"' | $SHELL_BIN 2>&1)
assert_eq "two double-quoted args" "hello world" "$OUT"

# --- 5.2 Single quotes ---
echo "--- 5.2 Single quotes ---"

OUT=$(printf "/bin/echo 'hello world'\n" | $SHELL_BIN 2>&1)
assert_eq "single quotes preserve spaces" "hello world" "$OUT"

OUT=$(printf "/bin/echo '\$HOME'\n" | $SHELL_BIN 2>&1)
assert_eq "single quotes no expansion" "\$HOME" "$OUT"

OUT=$(printf "/bin/echo 'hello \"world\"'\n" | $SHELL_BIN 2>&1)
assert_eq "single quotes preserve double quotes" 'hello "world"' "$OUT"

# --- 5.3 Backslash escaping ---
echo "--- 5.3 Backslash escaping ---"

OUT=$(printf '/bin/echo hello\\ world\n' | $SHELL_BIN 2>&1)
assert_eq "backslash escapes space" "hello world" "$OUT"

OUT=$(echo '/bin/echo "hello\\\\world"' | $SHELL_BIN 2>&1)
assert_eq "backslash-backslash in double quotes" 'hello\\world' "$OUT"

# --- 5.4 Env variable expansion ---
echo "--- 5.4 Env variable expansion ---"

OUT=$(echo '/bin/echo $HOME' | HOME=/test/home $SHELL_BIN 2>&1)
assert_eq "bare \$HOME expansion" "/test/home" "$OUT"

OUT=$(echo '/bin/echo ${HOME}' | HOME=/test/home $SHELL_BIN 2>&1)
assert_eq "braced \${HOME} expansion" "/test/home" "$OUT"

OUT=$(echo '/bin/echo "$HOME"' | HOME=/test/home $SHELL_BIN 2>&1)
assert_eq "\$HOME in double quotes" "/test/home" "$OUT"

OUT=$(echo '/bin/echo "${HOME}"' | HOME=/test/home $SHELL_BIN 2>&1)
assert_eq "\${HOME} in double quotes" "/test/home" "$OUT"

OUT=$(printf '/bin/echo ${UNDEFINED_VAR_XYZ}end\n' | $SHELL_BIN 2>&1)
assert_eq "undefined var expands to empty" "end" "$OUT"

OUT=$(printf "setenv TESTVAR hello\n/bin/echo \$TESTVAR\n" | $SHELL_BIN 2>&1)
assert_eq "expand var set in session" "hello" "$OUT"

OUT=$(echo '/bin/echo prefix${HOME}suffix' | HOME=/h $SHELL_BIN 2>&1)
assert_eq "braced var in middle of word" "prefix/hsuffix" "$OUT"

# --- 5.5 Special variables ---
echo "--- 5.5 Special variables ---"

OUT=$(printf '/usr/bin/true\n/bin/echo $?\n' | $SHELL_BIN 2>&1)
assert_eq "\$? after true" "0" "$OUT"

OUT=$(printf '/usr/bin/false\n/bin/echo $?\n' | $SHELL_BIN 2>&1)
assert_eq "\$? after false" "1" "$OUT"

OUT=$(echo '/bin/echo $$' | $SHELL_BIN 2>&1)
# $$ should be a number (the shell's PID)
if echo "$OUT" | grep -qE '^[0-9]+$'; then
    echo "  PASS: \$\$ is a PID number"
    PASS=$((PASS + 1))
else
    echo "  FAIL: \$\$ is a PID number"
    echo "    actual: $(printf '%q' "$OUT")"
    FAIL=$((FAIL + 1))
fi

# --- 5.6 Tilde expansion ---
echo "--- 5.6 Tilde expansion ---"

OUT=$(echo '/bin/echo ~' | HOME=/test/home $SHELL_BIN 2>&1)
assert_eq "tilde expands to HOME" "/test/home" "$OUT"

OUT=$(echo '/bin/echo ~/subdir' | HOME=/test/home $SHELL_BIN 2>&1)
assert_eq "tilde with path" "/test/home/subdir" "$OUT"

# Tilde in single quotes should NOT expand
OUT=$(printf "/bin/echo '~'\n" | HOME=/test/home $SHELL_BIN 2>&1)
assert_eq "tilde in single quotes literal" "~" "$OUT"

# Tilde in double quotes should NOT expand (it's not at word start in unquoted context)
OUT=$(echo '/bin/echo "~"' | HOME=/test/home $SHELL_BIN 2>&1)
assert_eq "tilde in double quotes literal" "~" "$OUT"

# --- Summary ---
echo ""
echo "test_m5_quoting_expansion"
TOTAL=$((PASS + FAIL))
echo "  $TOTAL tests: $PASS passed, $FAIL failed"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
