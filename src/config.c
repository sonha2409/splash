#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"

#define CONFIG_MAX_ENTRIES 256
#define CONFIG_MAX_LINE 1024

typedef struct {
    char *key;    // "section.name" format, owned
    char *value;  // string value, owned
} ConfigEntry;

static char config_dir[1024];
static int config_dir_valid = 0;

static ConfigEntry config_entries[CONFIG_MAX_ENTRIES];
static int config_count = 0;


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

// Store a key-value pair. Duplicates: last one wins.
static void config_set(const char *key, const char *value) {
    // Check for existing key
    for (int i = 0; i < config_count; i++) {
        if (strcmp(config_entries[i].key, key) == 0) {
            free(config_entries[i].value);
            config_entries[i].value = strdup(value);
            return;
        }
    }
    if (config_count >= CONFIG_MAX_ENTRIES) {
        fprintf(stderr, "splash: config: too many entries (max %d)\n",
                CONFIG_MAX_ENTRIES);
        return;
    }
    config_entries[config_count].key = strdup(key);
    config_entries[config_count].value = strdup(value);
    config_count++;
}

// Strip leading and trailing whitespace in-place. Returns pointer into buf.
static char *strip(char *buf) {
    while (*buf && isspace((unsigned char)*buf)) buf++;
    if (*buf == '\0') return buf;
    char *end = buf + strlen(buf) - 1;
    while (end > buf && isspace((unsigned char)*end)) *end-- = '\0';
    return buf;
}

// Parse a TOML value: quoted string, boolean, or bare integer/string.
// Returns a newly allocated string with the parsed value. Caller frees.
static char *parse_value(const char *raw) {
    // Quoted string
    if (raw[0] == '"') {
        const char *start = raw + 1;
        // Find closing quote, handling escapes
        size_t cap = strlen(raw);
        char *result = malloc(cap + 1);
        if (!result) return strdup("");
        size_t len = 0;
        const char *p = start;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) {
                p++;
                switch (*p) {
                    case 'n':  result[len++] = '\n'; break;
                    case 't':  result[len++] = '\t'; break;
                    case '\\': result[len++] = '\\'; break;
                    case '"':  result[len++] = '"';  break;
                    default:   result[len++] = '\\'; result[len++] = *p; break;
                }
            } else {
                result[len++] = *p;
            }
            p++;
        }
        result[len] = '\0';
        return result;
    }

    // Single-quoted string (literal, no escapes)
    if (raw[0] == '\'') {
        const char *start = raw + 1;
        const char *end = strchr(start, '\'');
        if (end) {
            size_t len = (size_t)(end - start);
            char *result = malloc(len + 1);
            if (!result) return strdup("");
            memcpy(result, start, len);
            result[len] = '\0';
            return result;
        }
        return strdup(start);
    }

    // Strip inline comment: value # comment
    char *copy = strdup(raw);
    if (!copy) return strdup("");
    // Only strip # that's preceded by whitespace (avoid stripping inside values)
    char *hash = copy;
    while ((hash = strchr(hash, '#')) != NULL) {
        if (hash > copy && isspace((unsigned char)hash[-1])) {
            *hash = '\0';
            break;
        }
        hash++;
    }
    // Trim trailing whitespace
    char *end = copy + strlen(copy) - 1;
    while (end > copy && isspace((unsigned char)*end)) *end-- = '\0';

    return copy;
}

void config_load(void) {
    if (!config_dir_valid) {
        return;
    }

    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/config.toml", config_dir);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        return;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
        // File doesn't exist — not an error
        return;
    }

    char section[256] = "";
    char line_buf[CONFIG_MAX_LINE];
    int line_num = 0;

    while (fgets(line_buf, sizeof(line_buf), fp)) {
        line_num++;
        char *line = strip(line_buf);

        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        // Section header: [section_name]
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (!end) {
                fprintf(stderr, "splash: config.toml:%d: malformed section header\n",
                        line_num);
                continue;
            }
            *end = '\0';
            snprintf(section, sizeof(section), "%s", line + 1);
            continue;
        }

        // Key = value
        char *eq = strchr(line, '=');
        if (!eq) {
            fprintf(stderr, "splash: config.toml:%d: expected 'key = value'\n",
                    line_num);
            continue;
        }

        *eq = '\0';
        char *key_part = strip(line);
        char *val_part = strip(eq + 1);

        if (key_part[0] == '\0') {
            fprintf(stderr, "splash: config.toml:%d: empty key\n", line_num);
            continue;
        }

        // Build full key: section.key
        char full_key[512];
        if (section[0] != '\0') {
            snprintf(full_key, sizeof(full_key), "%s.%s", section, key_part);
        } else {
            snprintf(full_key, sizeof(full_key), "%s", key_part);
        }

        char *parsed = parse_value(val_part);
        config_set(full_key, parsed);
        free(parsed);
    }

    fclose(fp);
}

// Load config from an explicit path (for testing).
void config_load_from(const char *path) {
    // Temporarily make config_dir_valid true and set config_dir
    // so we can reuse the loading logic. Instead, just parse directly.
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return;
    }

    char section[256] = "";
    char line_buf[CONFIG_MAX_LINE];
    int line_num = 0;

    while (fgets(line_buf, sizeof(line_buf), fp)) {
        line_num++;
        char *line = strip(line_buf);

        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (!end) {
                fprintf(stderr, "splash: config.toml:%d: malformed section header\n",
                        line_num);
                continue;
            }
            *end = '\0';
            snprintf(section, sizeof(section), "%s", line + 1);
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq) {
            fprintf(stderr, "splash: config.toml:%d: expected 'key = value'\n",
                    line_num);
            continue;
        }

        *eq = '\0';
        char *key_part = strip(line);
        char *val_part = strip(eq + 1);

        if (key_part[0] == '\0') {
            fprintf(stderr, "splash: config.toml:%d: empty key\n", line_num);
            continue;
        }

        char full_key[512];
        if (section[0] != '\0') {
            snprintf(full_key, sizeof(full_key), "%s.%s", section, key_part);
        } else {
            snprintf(full_key, sizeof(full_key), "%s", key_part);
        }

        char *parsed = parse_value(val_part);
        config_set(full_key, parsed);
        free(parsed);
    }

    fclose(fp);
}

const char *config_get_string(const char *key) {
    for (int i = 0; i < config_count; i++) {
        if (strcmp(config_entries[i].key, key) == 0) {
            return config_entries[i].value;
        }
    }
    return NULL;
}

int config_get_int(const char *key, int default_val) {
    const char *val = config_get_string(key);
    if (!val) return default_val;

    char *end;
    long result = strtol(val, &end, 10);
    if (end == val || *end != '\0') {
        return default_val;
    }
    return (int)result;
}

int config_get_bool(const char *key, int default_val) {
    const char *val = config_get_string(key);
    if (!val) return default_val;

    if (strcasecmp(val, "true") == 0) return 1;
    if (strcasecmp(val, "false") == 0) return 0;
    return default_val;
}

void config_reset(void) {
    config_dir[0] = '\0';
    config_dir_valid = 0;
    for (int i = 0; i < config_count; i++) {
        free(config_entries[i].key);
        free(config_entries[i].value);
    }
    config_count = 0;
}
