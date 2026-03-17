#!/bin/bash
# Integration tests for Milestone 4: Builtins and Environment (4.1-4.6)

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
    if ! echo "$haystack" | grep -q "$needle"; then
        echo "  PASS: $test_name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $test_name"
        echo "    expected NOT to contain: $needle"
        echo "    actual: $(printf '%q' "$haystack")"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== Milestone 4: Builtins and Environment ==="

# --- 4.1 exit ---
echo "--- 4.1 exit ---"

OUT=$(echo "exit" | $SHELL_BIN 2>&1)
assert_contains "exit prints goodbye" "Viszontlátásra" "$OUT"

echo "exit 42" | $SHELL_BIN 2>/dev/null
assert_eq "exit with status code" "42" "$?"

echo "exit 0" | $SHELL_BIN 2>/dev/null
assert_eq "exit with status 0" "0" "$?"

# --- 4.2 cd ---
echo "--- 4.2 cd ---"

OUT=$(echo "cd /tmp" | $SHELL_BIN 2>&1)
# cd itself produces no output on success

OUT=$(printf 'cd /tmp\n/bin/pwd\n' | $SHELL_BIN 2>&1)
assert_contains "cd /tmp then pwd" "/tmp" "$OUT"

OUT=$(printf 'cd /tmp\ncd -\n' | $SHELL_BIN 2>&1)
# cd - should print previous directory

OUT=$(echo "cd /nonexistent_dir_xyz" | $SHELL_BIN 2>&1)
assert_contains "cd to nonexistent dir" "No such file" "$OUT"

# --- 4.3 printenv ---
echo "--- 4.3 printenv ---"

OUT=$(echo "printenv" | $SHELL_BIN 2>&1)
assert_contains "printenv shows HOME" "HOME=" "$OUT"
assert_contains "printenv shows PATH" "PATH=" "$OUT"

OUT=$(echo "printenv HOME" | HOME=/test/home $SHELL_BIN 2>&1)
assert_contains "printenv HOME shows value" "/test/home" "$OUT"

OUT=$(printf 'printenv NONEXISTENT_VAR_XYZ\n/bin/echo exitcode_check\n' | $SHELL_BIN 2>&1)
# printenv for nonexistent var should produce no output for that var
assert_not_contains "printenv nonexistent var no output" "NONEXISTENT" "$OUT"

# --- 4.4 setenv ---
echo "--- 4.4 setenv ---"

OUT=$(printf 'setenv SPLASH_TEST hello\nprintenv SPLASH_TEST\n' | $SHELL_BIN 2>&1)
assert_contains "setenv then printenv" "hello" "$OUT"

OUT=$(echo "setenv" | $SHELL_BIN 2>&1)
assert_contains "setenv no args error" "usage" "$OUT"

OUT=$(echo "setenv ONEARG" | $SHELL_BIN 2>&1)
assert_contains "setenv one arg error" "usage" "$OUT"

# --- 4.5 unsetenv ---
echo "--- 4.5 unsetenv ---"

OUT=$(printf 'setenv SPLASH_DEL bye\nunsetenv SPLASH_DEL\nprintenv SPLASH_DEL\n' | $SHELL_BIN 2>&1)
assert_not_contains "unsetenv removes var" "bye" "$OUT"

OUT=$(echo "unsetenv" | $SHELL_BIN 2>&1)
assert_contains "unsetenv no args error" "usage" "$OUT"

# --- 4.6 export ---
echo "--- 4.6 export ---"

OUT=$(printf 'export SPLASH_EXP=world\nprintenv SPLASH_EXP\n' | $SHELL_BIN 2>&1)
assert_contains "export VAR=VALUE" "world" "$OUT"

OUT=$(printf 'export SPLASH_EMPTY=\nprintenv\n' | $SHELL_BIN 2>&1)
# Should set empty value — printenv (all vars) should show SPLASH_EMPTY=
assert_contains "export VAR= sets empty" "SPLASH_EMPTY=" "$OUT"

OUT=$(echo "export" | $SHELL_BIN 2>&1)
assert_contains "export no args lists vars" "export" "$OUT"

# --- 4.7 source ---
echo "--- 4.7 source ---"

# Basic source: run commands from file
cat > "$TMPDIR/basic.sh" << 'SCRIPT'
/bin/echo hello from source
/bin/echo second line
SCRIPT
OUT=$(echo "source $TMPDIR/basic.sh" | $SHELL_BIN 2>&1)
assert_contains "source runs commands" "hello from source" "$OUT"
assert_contains "source runs multiple lines" "second line" "$OUT"

# Source sets env vars visible to subsequent commands
cat > "$TMPDIR/setvar.sh" << 'SCRIPT'
setenv SPLASH_SOURCED yes
SCRIPT
OUT=$(printf "source $TMPDIR/setvar.sh\nprintenv SPLASH_SOURCED\n" | $SHELL_BIN 2>&1)
assert_contains "source sets env vars" "yes" "$OUT"

# Source nonexistent file
OUT=$(echo "source /nonexistent_file_xyz.sh" | $SHELL_BIN 2>&1)
assert_contains "source nonexistent file error" "No such file" "$OUT"

# Source no args
OUT=$(echo "source" | $SHELL_BIN 2>&1)
assert_contains "source no args error" "usage" "$OUT"

# Nested source
cat > "$TMPDIR/inner.sh" << 'SCRIPT'
/bin/echo from inner
SCRIPT
cat > "$TMPDIR/outer.sh" << SCRIPT
source $TMPDIR/inner.sh
/bin/echo from outer
SCRIPT
OUT=$(echo "source $TMPDIR/outer.sh" | $SHELL_BIN 2>&1)
assert_contains "nested source inner" "from inner" "$OUT"
assert_contains "nested source outer" "from outer" "$OUT"

# --- 4.8 alias / unalias ---
echo "--- 4.8 alias / unalias ---"

# Basic alias
OUT=$(printf "alias greet='/bin/echo hello'\ngreet\n" | $SHELL_BIN 2>&1)
assert_contains "alias basic expansion" "hello" "$OUT"

# Alias with arguments
OUT=$(printf "alias greet='/bin/echo hi'\ngreet world\n" | $SHELL_BIN 2>&1)
assert_contains "alias with trailing args" "hi world" "$OUT"

# Alias list
OUT=$(printf "alias foo='bar'\nalias baz='qux'\nalias\n" | $SHELL_BIN 2>&1)
assert_contains "alias list shows foo" "alias foo='bar'" "$OUT"
assert_contains "alias list shows baz" "alias baz='qux'" "$OUT"

# Alias print specific
OUT=$(printf "alias myalias='myval'\nalias myalias\n" | $SHELL_BIN 2>&1)
assert_contains "alias print specific" "alias myalias='myval'" "$OUT"

# Unalias
OUT=$(printf "alias rmme='/bin/echo removed'\nunalias rmme\nrmme\n" | $SHELL_BIN 2>&1)
# After unalias, rmme should not be found (not an alias, not a command)
assert_not_contains "unalias removes alias" "removed" "$OUT"

# Unalias not found
OUT=$(echo "unalias nonexistent" | $SHELL_BIN 2>&1)
assert_contains "unalias not found error" "not found" "$OUT"

# Alias shadows builtin
OUT=$(printf "alias printenv='/bin/echo shadowed'\nprintenv\n" | $SHELL_BIN 2>&1)
assert_contains "alias shadows builtin" "shadowed" "$OUT"

# --- 4.9 type / which ---
echo "--- 4.9 type / which ---"

# type builtin
OUT=$(echo "type cd" | $SHELL_BIN 2>&1)
assert_contains "type builtin" "cd is a shell builtin" "$OUT"

# type external
OUT=$(echo "type ls" | $SHELL_BIN 2>&1)
assert_contains "type external" "ls is /" "$OUT"

# type alias
OUT=$(printf "alias foo='bar'\ntype foo\n" | $SHELL_BIN 2>&1)
assert_contains "type alias" "foo is aliased to 'bar'" "$OUT"

# type not found
OUT=$(echo "type nonexistent_cmd_xyz" | $SHELL_BIN 2>&1)
assert_contains "type not found" "not found" "$OUT"

# which external
OUT=$(echo "which ls" | $SHELL_BIN 2>&1)
assert_contains "which external path" "/ls" "$OUT"

# which builtin
OUT=$(echo "which cd" | $SHELL_BIN 2>&1)
assert_contains "which builtin" "built-in" "$OUT"

# --- 4.10 history ---
echo "--- 4.10 history ---"

OUT=$(printf '/bin/echo first\n/bin/echo second\nhistory\n' | $SHELL_BIN 2>&1)
assert_contains "history shows first" "1  /bin/echo first" "$OUT"
assert_contains "history shows second" "2  /bin/echo second" "$OUT"
assert_contains "history shows itself" "3  history" "$OUT"

# Duplicate suppression
OUT=$(printf '/bin/echo dup\n/bin/echo dup\nhistory\n' | $SHELL_BIN 2>&1)
# Should only have one "dup" entry, not two
assert_contains "history dedup count" "1  /bin/echo dup" "$OUT"
assert_not_contains "history no dup line 2" "2  /bin/echo dup" "$OUT"

# --- 4.11 auto-source config ---
echo "--- 4.11 auto-source config ---"
# Auto-source is interactive-only (requires tty), so we can't fully test
# it via pipe. Verify it doesn't crash in non-interactive mode.
CONFDIR="$TMPDIR/autosrc_home/.config/splash"
mkdir -p "$CONFDIR"
cat > "$CONFDIR/init.sh" << 'SCRIPT'
setenv SPLASH_INIT loaded
SCRIPT
# Non-interactive: init.sh should NOT be sourced (no tty)
OUT=$(echo "printenv SPLASH_INIT" | HOME="$TMPDIR/autosrc_home" $SHELL_BIN 2>&1)
assert_not_contains "auto-source skipped in non-interactive" "loaded" "$OUT"

# --- Summary ---
echo ""
echo "test_m4_builtins"
TOTAL=$((PASS + FAIL))
echo "  $TOTAL tests: $PASS passed, $FAIL failed"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
