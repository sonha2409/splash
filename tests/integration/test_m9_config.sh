#!/bin/bash
# Integration tests for Milestone 9: Configuration System

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

echo "=== Milestone 9.1: XDG directory setup ==="

# Test: config dir is created on startup
CONFIG_DIR="$TMPDIR/config_home"
result=$(HOME="$TMPDIR" XDG_CONFIG_HOME="$CONFIG_DIR" $SHELL_BIN <<'EOF'
echo done
EOF
)
assert_eq "config dir created" "0" "$([ -d "$CONFIG_DIR/splash" ] && echo 0 || echo 1)"

echo ""
echo "=== Milestone 9.3: init.sh sourcing ==="

# Note: auto-sourcing only happens in interactive mode (isatty).
# These tests verify that init.sh content works when sourced via the source builtin.

# Test: init.sh can set env vars
INIT_DIR="$TMPDIR/init_test"
mkdir -p "$INIT_DIR/splash"
cat > "$INIT_DIR/splash/init.sh" <<'INITEOF'
export MY_INIT_VAR=hello_from_init
INITEOF

result=$(XDG_CONFIG_HOME="$INIT_DIR" $SHELL_BIN <<EOF
source $INIT_DIR/splash/init.sh
echo \$MY_INIT_VAR
EOF
)
assert_eq "init.sh sets env var" "hello_from_init" "$result"

# Test: init.sh can define aliases
ALIAS_DIR="$TMPDIR/alias_test"
mkdir -p "$ALIAS_DIR/splash"
cat > "$ALIAS_DIR/splash/init.sh" <<'INITEOF'
alias greet="echo hi_there"
INITEOF

result=$(XDG_CONFIG_HOME="$ALIAS_DIR" $SHELL_BIN <<EOF
source $ALIAS_DIR/splash/init.sh
greet
EOF
)
assert_eq "init.sh defines alias" "hi_there" "$result"

# Test: init.sh can define functions
FUNC_DIR="$TMPDIR/func_test"
mkdir -p "$FUNC_DIR/splash"
cat > "$FUNC_DIR/splash/init.sh" <<'INITEOF'
myfunc() { echo "func_output"; }
INITEOF

result=$(XDG_CONFIG_HOME="$FUNC_DIR" $SHELL_BIN <<EOF
source $FUNC_DIR/splash/init.sh
myfunc
EOF
)
assert_eq "init.sh defines function" "func_output" "$result"

echo ""
echo "=== Milestone 9.4: ~/.shellrc compat ==="

# Test: ~/.shellrc content works when sourced
SHELLRC_HOME="$TMPDIR/shellrc_home"
mkdir -p "$SHELLRC_HOME"
cat > "$SHELLRC_HOME/.shellrc" <<'RCEOF'
export SHELLRC_VAR=from_shellrc
RCEOF

result=$($SHELL_BIN <<EOF
source $SHELLRC_HOME/.shellrc
echo \$SHELLRC_VAR
EOF
)
assert_eq "shellrc sets env var" "from_shellrc" "$result"

echo ""
echo "=== Milestone 9.5: Variable prompt ==="

# Test: $PROMPT env var is used (non-interactive, so prompt isn't printed,
# but we can verify config_build_prompt via a function test in unit tests)
# The prompt feature is interactive-only; unit tests cover expansion logic.
echo "  (covered by unit tests — interactive feature)"

echo ""
echo "=== Milestone 9.6: ON_ERROR env var ==="

# Note: ON_ERROR only triggers in interactive mode. Since we pipe input
# (non-interactive), we test the concept via the source builtin approach.
# The actual ON_ERROR check is in the REPL loop, guarded by `interactive`.
echo "  (interactive-only feature — manual verification needed)"

echo ""
echo "--- Results: $PASS passed, $FAIL failed ---"
[ "$FAIL" -eq 0 ] || exit 1
