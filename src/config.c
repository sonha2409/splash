#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"

static char config_dir[1024];
static int config_dir_valid = 0;


// Create a directory if it doesn't already exist.
// Returns 0 on success (or already exists), -1 on failure.
static int ensure_dir(const char *path) {
    if (mkdir(path, 0755) == -1) {
        if (errno == EEXIST) {
            return 0;
        }
        fprintf(stderr, "splash: mkdir '%s': %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

void config_init(void) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");

    if (xdg && xdg[0] != '\0') {
        // $XDG_CONFIG_HOME/splash/
        int n = snprintf(config_dir, sizeof(config_dir), "%s/splash", xdg);
        if (n < 0 || (size_t)n >= sizeof(config_dir)) {
            fprintf(stderr, "splash: config path too long\n");
            return;
        }
        // Ensure $XDG_CONFIG_HOME itself exists
        if (ensure_dir(xdg) == -1) {
            return;
        }
    } else if (home && home[0] != '\0') {
        // $HOME/.config/splash/
        char parent[1024];
        int n = snprintf(parent, sizeof(parent), "%s/.config", home);
        if (n < 0 || (size_t)n >= sizeof(parent)) {
            fprintf(stderr, "splash: config path too long\n");
            return;
        }
        if (ensure_dir(parent) == -1) {
            return;
        }
        n = snprintf(config_dir, sizeof(config_dir), "%s/.config/splash", home);
        if (n < 0 || (size_t)n >= sizeof(config_dir)) {
            fprintf(stderr, "splash: config path too long\n");
            return;
        }
    } else {
        fprintf(stderr, "splash: warning: neither $XDG_CONFIG_HOME nor "
                "$HOME is set, skipping config directory setup\n");
        return;
    }

    if (ensure_dir(config_dir) == -1) {
        return;
    }

    config_dir_valid = 1;
}

const char *config_get_dir(void) {
    if (!config_dir_valid) {
        return NULL;
    }
    return config_dir;
}

void config_reset(void) {
    config_dir[0] = '\0';
    config_dir_valid = 0;
}
