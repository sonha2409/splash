#include <stdlib.h>
#include <string.h>

#include "functions.h"
#include "util.h"

typedef struct {
    char *name;      // Function name (owned)
    char *body_src;  // Raw body source text (owned)
} FuncEntry;

static FuncEntry *entries = NULL;
static int num_entries = 0;
static int capacity = 0;

// Find the index of a function by name, or -1 if not found.
static int find_entry(const char *name) {
    for (int i = 0; i < num_entries; i++) {
        if (strcmp(entries[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

void functions_define(const char *name, const char *body_src) {
    int idx = find_entry(name);
    if (idx >= 0) {
        // Redefine existing function
        free(entries[idx].body_src);
        entries[idx].body_src = xstrdup(body_src);
        return;
    }

    // Add new entry
    if (num_entries >= capacity) {
        capacity = capacity ? capacity * 2 : 8;
        entries = xrealloc(entries, sizeof(FuncEntry) * (size_t)capacity);
    }
    entries[num_entries].name = xstrdup(name);
    entries[num_entries].body_src = xstrdup(body_src);
    num_entries++;
}

const char *functions_lookup(const char *name) {
    int idx = find_entry(name);
    if (idx >= 0) {
        return entries[idx].body_src;
    }
    return NULL;
}

void functions_unset(const char *name) {
    int idx = find_entry(name);
    if (idx < 0) {
        return;
    }
    free(entries[idx].name);
    free(entries[idx].body_src);
    // Shift remaining entries down
    for (int i = idx; i < num_entries - 1; i++) {
        entries[i] = entries[i + 1];
    }
    num_entries--;
}

void functions_free_all(void) {
    for (int i = 0; i < num_entries; i++) {
        free(entries[i].name);
        free(entries[i].body_src);
    }
    free(entries);
    entries = NULL;
    num_entries = 0;
    capacity = 0;
}
