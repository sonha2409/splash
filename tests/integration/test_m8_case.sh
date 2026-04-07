#!/bin/bash
# Integration tests for 8.5: case / esac

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

echo "=== Milestone 8.5: case / esac ==="

# --- Literal match ---

OUT=$(echo 'case foo in foo) echo matched;; *) echo no;; esac' | $SHELL_BIN 2>/dev/null)
assert_eq "literal match first arm" "matched" "$OUT"

OUT=$(echo 'case bar in foo) echo no;; bar) echo yes;; *) echo other;; esac' | $SHELL_BIN 2>/dev/null)
assert_eq "literal match second arm" "yes" "$OUT"

# --- Default arm ---

OUT=$(echo 'case xyz in foo) echo no;; bar) echo no;; *) echo default;; esac' | $SHELL_BIN 2>/dev/null)
assert_eq "default arm matches" "default" "$OUT"

# --- No match, no default → no output, exit 0 ---

OUT=$(printf 'case xyz in foo) echo no;; esac\necho $?\n' | $SHELL_BIN 2>/dev/null)
assert_eq "no match, no default → \$? = 0" "0" "$OUT"

# --- Glob patterns ---

OUT=$(echo 'case abcdef in a*) echo starts_a;; *) echo no;; esac' | $SHELL_BIN 2>/dev/null)
assert_eq "glob: prefix match a*" "starts_a" "$OUT"

OUT=$(echo 'case test.c in *.c) echo c_file;; *.h) echo header;; *) echo other;; esac' | $SHELL_BIN 2>/dev/null)
assert_eq "glob: suffix match *.c" "c_file" "$OUT"

OUT=$(echo 'case test.h in *.c) echo c_file;; *.h) echo header;; *) echo other;; esac' | $SHELL_BIN 2>/dev/null)
assert_eq "glob: suffix match *.h" "header" "$OUT"

OUT=$(echo 'case abc in a?c) echo single;; *) echo no;; esac' | $SHELL_BIN 2>/dev/null)
assert_eq "glob: ? wildcard" "single" "$OUT"

# --- Alternation with | ---

OUT=$(echo 'case dog in cat|dog|bird) echo animal;; *) echo no;; esac' | $SHELL_BIN 2>/dev/null)
assert_eq "alternation: middle option" "animal" "$OUT"

OUT=$(echo 'case cat in cat|dog) echo first;; *) echo no;; esac' | $SHELL_BIN 2>/dev/null)
assert_eq "alternation: first option" "first" "$OUT"

# --- Empty string match ---

OUT=$(echo 'case "" in "") echo empty;; *) echo other;; esac' | $SHELL_BIN 2>/dev/null)
assert_eq "match empty string" "empty" "$OUT"

# --- Variable as match subject ---

OUT=$(printf 'setenv x hello\ncase $x in hello) echo greeting;; *) echo other;; esac\n' | $SHELL_BIN 2>/dev/null)
assert_eq "match against variable" "greeting" "$OUT"

# --- Multi-command body ---

OUT=$(echo 'case foo in foo) echo a; echo b;; *) echo no;; esac' | $SHELL_BIN 2>/dev/null)
assert_eq "multi-command body" "a
b" "$OUT"

# --- First match wins (no fallthrough) ---

OUT=$(echo 'case foo in foo) echo first;; foo) echo second;; esac' | $SHELL_BIN 2>/dev/null)
assert_eq "first match wins, no fallthrough" "first" "$OUT"

# --- Case nested in if ---

OUT=$(echo 'if true; then case x in x) echo yes;; esac; fi' | $SHELL_BIN 2>/dev/null)
assert_eq "case nested in if" "yes" "$OUT"

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
exit $FAIL
