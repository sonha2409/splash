#ifndef SPLASH_TEST_H
#define SPLASH_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(expr) do { \
    tests_run++; \
    if (!(expr)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while (0)

#define ASSERT_STR_EQ(a, b) do { \
    tests_run++; \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "  FAIL: %s:%d: \"%s\" != \"%s\"\n", \
                __FILE__, __LINE__, (a), (b)); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while (0)

#define ASSERT_INT_EQ(a, b) do { \
    tests_run++; \
    if ((a) != (b)) { \
        fprintf(stderr, "  FAIL: %s:%d: %d != %d\n", \
                __FILE__, __LINE__, (a), (b)); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while (0)

#define ASSERT_NULL(expr) ASSERT((expr) == NULL)
#define ASSERT_NOT_NULL(expr) ASSERT((expr) != NULL)

#define TEST_REPORT() do { \
    printf("  %d tests: %d passed, %d failed\n", \
           tests_run, tests_passed, tests_failed); \
    return tests_failed > 0 ? 1 : 0; \
} while (0)

#endif // SPLASH_TEST_H
