#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "history.h"
#include "util.h"

#define HISTORY_MAX 1000

static char *entries[HISTORY_MAX];
static int num_entries = 0;

void history_init(void) {
    num_entries = 0;
}

void history_add(const char *line) {
    if (!line || *line == '\0') {
        return;
    }

    // Don't add duplicates of the last entry
    if (num_entries > 0 && strcmp(entries[num_entries - 1], line) == 0) {
        return;
    }

    if (num_entries >= HISTORY_MAX) {
        // Drop oldest entry
        free(entries[0]);
        for (int i = 0; i < num_entries - 1; i++) {
            entries[i] = entries[i + 1];
        }
        num_entries--;
    }

    entries[num_entries] = xstrdup(line);
    num_entries++;
}

void history_print(void) {
    for (int i = 0; i < num_entries; i++) {
        printf("%5d  %s\n", i + 1, entries[i]);
    }
}

int history_count(void) {
    return num_entries;
}
