#ifndef SPLASH_UTIL_H
#define SPLASH_UTIL_H

#include <stddef.h>

// Allocate memory or abort on failure. Caller takes ownership.
void *xmalloc(size_t size);

// Allocate zeroed memory or abort on failure. Caller takes ownership.
void *xcalloc(size_t count, size_t size);

// Reallocate memory or abort on failure. Caller takes ownership.
void *xrealloc(void *ptr, size_t size);

// Duplicate a string or abort on failure. Caller takes ownership.
char *xstrdup(const char *s);

#endif // SPLASH_UTIL_H
