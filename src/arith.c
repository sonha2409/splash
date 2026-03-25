#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arith.h"
#include "expand.h"

// Recursive descent parser for arithmetic expressions.
// Grammar:
//   expr     → add_expr
//   add_expr → mul_expr (('+' | '-') mul_expr)*
//   mul_expr → unary (('+' | '-' | '%') unary)*
//   unary    → ('+' | '-') unary | primary
//   primary  → NUMBER | VARIABLE | '(' expr ')'

typedef struct {
    const char *input;
    int pos;
    int error;
} ArithParser;

static void skip_spaces(ArithParser *p) {
    while (p->input[p->pos] == ' ' || p->input[p->pos] == '\t') {
        p->pos++;
    }
}

static long long parse_expr(ArithParser *p);

static long long parse_primary(ArithParser *p) {
    skip_spaces(p);

    if (p->error) {
        return 0;
    }

    char c = p->input[p->pos];

    // Parenthesized expression
    if (c == '(') {
        p->pos++;
        long long val = parse_expr(p);
        skip_spaces(p);
        if (p->input[p->pos] == ')') {
            p->pos++;
        } else {
            fprintf(stderr, "splash: arithmetic: missing ')'\n");
            p->error = 1;
        }
        return val;
    }

    // Number
    if (isdigit((unsigned char)c)) {
        long long val = 0;
        while (isdigit((unsigned char)p->input[p->pos])) {
            val = val * 10 + (p->input[p->pos] - '0');
            p->pos++;
        }
        return val;
    }

    // Variable reference: $VAR, ${VAR}, or bare name
    if (c == '$') {
        p->pos++; // skip $
        char name[256];
        int nlen = 0;
        if (p->input[p->pos] == '{') {
            p->pos++;
            while (p->input[p->pos] && p->input[p->pos] != '}' && nlen < 255) {
                name[nlen++] = p->input[p->pos++];
            }
            if (p->input[p->pos] == '}') {
                p->pos++;
            }
        } else {
            while (isalnum((unsigned char)p->input[p->pos]) ||
                   p->input[p->pos] == '_') {
                if (nlen < 255) name[nlen++] = p->input[p->pos];
                p->pos++;
            }
        }
        name[nlen] = '\0';
        const char *val = expand_variable(name);
        if (val && *val) {
            return strtoll(val, NULL, 10);
        }
        return 0; // unset → 0
    }

    // Bare variable name (POSIX: bare identifiers in $(( )) are variable refs)
    if (isalpha((unsigned char)c) || c == '_') {
        char name[256];
        int nlen = 0;
        while (isalnum((unsigned char)p->input[p->pos]) ||
               p->input[p->pos] == '_') {
            if (nlen < 255) name[nlen++] = p->input[p->pos];
            p->pos++;
        }
        name[nlen] = '\0';
        const char *val = expand_variable(name);
        if (val && *val) {
            return strtoll(val, NULL, 10);
        }
        return 0; // unset → 0
    }

    if (c == '\0') {
        fprintf(stderr, "splash: arithmetic: unexpected end of expression\n");
    } else {
        fprintf(stderr, "splash: arithmetic: unexpected character '%c'\n", c);
    }
    p->error = 1;
    return 0;
}

static long long parse_unary(ArithParser *p) {
    skip_spaces(p);

    if (p->input[p->pos] == '+') {
        p->pos++;
        return parse_unary(p);
    }
    if (p->input[p->pos] == '-') {
        p->pos++;
        return -parse_unary(p);
    }

    return parse_primary(p);
}

static long long parse_mul_expr(ArithParser *p) {
    long long left = parse_unary(p);

    while (!p->error) {
        skip_spaces(p);
        char op = p->input[p->pos];
        if (op != '*' && op != '/' && op != '%') {
            break;
        }
        p->pos++;
        long long right = parse_unary(p);
        if (op == '*') {
            left *= right;
        } else if (op == '/') {
            if (right == 0) {
                fprintf(stderr, "splash: arithmetic: division by zero\n");
                p->error = 1;
                return 0;
            }
            left /= right;
        } else {
            if (right == 0) {
                fprintf(stderr, "splash: arithmetic: division by zero\n");
                p->error = 1;
                return 0;
            }
            left %= right;
        }
    }

    return left;
}

static long long parse_expr(ArithParser *p) {
    long long left = parse_mul_expr(p);

    while (!p->error) {
        skip_spaces(p);
        char op = p->input[p->pos];
        if (op != '+' && op != '-') {
            break;
        }
        p->pos++;
        long long right = parse_mul_expr(p);
        if (op == '+') {
            left += right;
        } else {
            left -= right;
        }
    }

    return left;
}

long long arith_eval(const char *expr, int *error) {
    ArithParser p = {.input = expr, .pos = 0, .error = 0};
    long long result = parse_expr(&p);

    // Check for trailing garbage
    skip_spaces(&p);
    if (!p.error && p.input[p.pos] != '\0') {
        fprintf(stderr,
                "splash: arithmetic: unexpected character '%c'\n",
                p.input[p.pos]);
        p.error = 1;
    }

    *error = p.error;
    return p.error ? 0 : result;
}
