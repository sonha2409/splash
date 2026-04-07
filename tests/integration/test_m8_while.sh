#!/bin/bash
# Integration tests for 8.4: while / until loops

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

echo "=== Milestone 8.4: while / until ==="

# --- while: basic counter (use arith expansion to mutate var) ---

OUT=$(echo 'setenv i 0; while test $i -lt 3; do echo $i; setenv i $((i+1)); done' | $SHELL_BIN 2>/dev/null)
assert_eq "while: counts 0..2" "0
1
2" "$OUT"

# --- while: condition false on entry → no iterations ---

OUT=$(echo 'while false; do echo never; done; echo after' | $SHELL_BIN 2>/dev/null)
assert_eq "while: false condition skips body" "after" "$OUT"

# --- while: exit status of loop is exit status of last body command (or 0 if none ran) ---

OUT=$(printf 'while false; do echo nope; done\necho $?\n' | $SHELL_BIN 2>/dev/null)
assert_eq "while: never-ran exit status is 0" "0" "$OUT"

# --- until: stop when condition becomes true ---

OUT=$(echo 'setenv i 0; until test $i -ge 3; do echo $i; setenv i $((i+1)); done' | $SHELL_BIN 2>/dev/null)
assert_eq "until: counts until threshold" "0
1
2" "$OUT"

# --- until: true on entry → no iterations ---

OUT=$(echo 'until true; do echo never; done; echo after' | $SHELL_BIN 2>/dev/null)
assert_eq "until: true condition skips body" "after" "$OUT"

# --- while inside if ---

OUT=$(echo 'if true; then setenv i 0; while test $i -lt 2; do echo $i; setenv i $((i+1)); done; fi' | $SHELL_BIN 2>/dev/null)
assert_eq "while nested in if" "0
1" "$OUT"

# --- while with pipe in body ---

OUT=$(echo 'setenv i 0; while test $i -lt 2; do echo hi | tr a-z A-Z; setenv i $((i+1)); done' | $SHELL_BIN 2>/dev/null)
assert_eq "while: pipe in body" "HI
HI" "$OUT"

# --- while with compound condition ---

OUT=$(echo 'setenv i 0; while test $i -lt 5 && test $i -lt 2; do echo $i; setenv i $((i+1)); done' | $SHELL_BIN 2>/dev/null)
assert_eq "while: compound && condition" "0
1" "$OUT"

# --- nested while ---

OUT=$(echo 'setenv i 0; while test $i -lt 2; do setenv j 0; while test $j -lt 2; do echo $i:$j; setenv j $((j+1)); done; setenv i $((i+1)); done' | $SHELL_BIN 2>/dev/null)
assert_eq "nested while" "0:0
0:1
1:0
1:1" "$OUT"

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
exit $FAIL
