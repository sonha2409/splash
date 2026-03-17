#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alias.h"
#include "util.h"

#define MAX_ALIASES 256

typedef struct {
    char *name;
    char *value;
} Alias;

static Alias aliases[MAX_ALIASES];
static int num_aliases = 0;

void alias_init(void) {
    num_aliases = 0;
}

void alias_set(const char *name, const char *value) {
    // Update existing
    for (int i = 0; i < num_aliases; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            free(aliases[i].value);
            aliases[i].value = xstrdup(value);
            return;
        }
    }

    // Add new
    if (num_aliases >= MAX_ALIASES) {
        fprintf(stderr, "splash: alias: too many aliases (max %d)\n",
                MAX_ALIASES);
        return;
    }
    aliases[num_aliases].name = xstrdup(name);
    aliases[num_aliases].value = xstrdup(value);
    num_aliases++;
}

int alias_remove(const char *name) {
    for (int i = 0; i < num_aliases; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            free(aliases[i].name);
            free(aliases[i].value);
            // Shift remaining entries
            for (int j = i; j < num_aliases - 1; j++) {
                aliases[j] = aliases[j + 1];
            }
            num_aliases--;
            return 0;
        }
    }
    return -1;
}

const char *alias_get(const char *name) {
    for (int i = 0; i < num_aliases; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            return aliases[i].value;
        }
    }
    return NULL;
}

void alias_print_all(void) {
    for (int i = 0; i < num_aliases; i++) {
        printf("alias %s='%s'\n", aliases[i].name, aliases[i].value);
    }
}

int alias_count(void) {
    return num_aliases;
}

const char *alias_get_name(int index) {
    if (index < 0 || index >= num_aliases) {
        return NULL;
    }
    return aliases[index].name;
}
