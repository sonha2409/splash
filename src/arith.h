#ifndef SPLASH_ARITH_H
#define SPLASH_ARITH_H

// Evaluate an arithmetic expression string.
// Supports: integer literals, variable references (bare names → $name),
// operators +, -, *, /, % with standard precedence, unary +/-, parentheses.
// Unset or non-numeric variables evaluate to 0 (POSIX behavior).
// Sets *error to 1 on error (e.g., division by zero, syntax error), 0 on success.
// Returns the result as a long long.
long long arith_eval(const char *expr, int *error);

#endif // SPLASH_ARITH_H
