#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

void *xmalloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "splash: malloc failed: out of memory\n");
        abort();
    }
    return ptr;
}

void *xcalloc(size_t count, size_t size) {
    void *ptr = calloc(count, size);
    if (!ptr) {
        fprintf(stderr, "splash: calloc failed: out of memory\n");
        abort();
    }
    return ptr;
}

void *xrealloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        fprintf(stderr, "splash: realloc failed: out of memory\n");
        abort();
    }
    return new_ptr;
}

char *xstrdup(const char *s) {
    char *dup = strdup(s);
    if (!dup) {
        fprintf(stderr, "splash: strdup failed: out of memory\n");
        abort();
    }
    return dup;
}
