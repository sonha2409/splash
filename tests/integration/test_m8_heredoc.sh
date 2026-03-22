#!/bin/bash
# Integration tests for 8.9: Here-documents

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

echo "=== Milestone 8.9: Here-documents ==="

# --- Basic heredoc ---

OUT=$(printf 'cat <<EOF\nhello world\nfoo bar\nEOF\n' | $SHELL_BIN 2>/dev/null)
assert_eq "basic heredoc" "hello world
foo bar" "$OUT"

# --- Empty heredoc ---

OUT=$(printf 'cat <<EOF\nEOF\n' | $SHELL_BIN 2>/dev/null)
assert_eq "empty heredoc" "" "$OUT"

# --- Heredoc with variable expansion ---

OUT=$(printf 'setenv NAME splash\ncat <<EOF\nhello $NAME\nEOF\n' | $SHELL_BIN 2>/dev/null)
assert_eq "heredoc var expansion" "hello splash" "$OUT"

# --- Quoted delimiter suppresses expansion ---

OUT=$(printf "setenv NAME splash\ncat <<'EOF'\nhello \$NAME\nEOF\n" | $SHELL_BIN 2>/dev/null)
assert_eq "quoted delimiter no expansion" 'hello $NAME' "$OUT"

# --- Heredoc in pipeline ---

OUT=$(printf 'cat <<EOF | grep hello\nhello world\nfoo bar\nEOF\n' | $SHELL_BIN 2>/dev/null)
assert_eq "heredoc in pipeline" "hello world" "$OUT"

# --- <<- strips leading tabs ---

OUT=$(printf 'cat <<-EOF\n\thello\n\tworld\nEOF\n' | $SHELL_BIN 2>/dev/null)
assert_eq "heredoc strip tabs" "hello
world" "$OUT"

# --- Multiple lines ---

OUT=$(printf 'cat <<END\nline 1\nline 2\nline 3\nEND\n' | $SHELL_BIN 2>/dev/null)
assert_eq "heredoc multiple lines" "line 1
line 2
line 3" "$OUT"

# --- Heredoc with special chars in body ---

OUT=$(printf 'cat <<EOF\n* ? [ ] |\nEOF\n' | $SHELL_BIN 2>/dev/null)
assert_eq "heredoc special chars" '* ? [ ] |' "$OUT"

# --- Double-quoted delimiter ---

OUT=$(printf 'setenv X val\ncat <<"EOF"\nhello $X\nEOF\n' | $SHELL_BIN 2>/dev/null)
assert_eq "double-quoted delimiter no expansion" 'hello $X' "$OUT"

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
exit $FAIL
