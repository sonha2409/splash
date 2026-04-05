#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

// Read the current git branch from .git/HEAD, searching up from cwd.
// Returns a newly allocated string or NULL if not in a git repo.
static char *get_git_branch(void) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        return NULL;
    }

    // Walk up directories looking for .git/HEAD
    char path[PATH_MAX];
    char *dir = cwd;
    for (;;) {
        snprintf(path, sizeof(path), "%s/.git/HEAD", dir);
        FILE *fp = fopen(path, "r");
        if (fp) {
            char line[256];
            if (fgets(line, sizeof(line), fp)) {
                fclose(fp);
                // Strip newline
                size_t len = strlen(line);
                if (len > 0 && line[len - 1] == '\n') {
                    line[len - 1] = '\0';
                }
                // "ref: refs/heads/branch_name"
                const char *prefix = "ref: refs/heads/";
                if (strncmp(line, prefix, strlen(prefix)) == 0) {
                    return strdup(line + strlen(prefix));
                }
                // Detached HEAD — return short hash
                if (len > 7) {
                    char *hash = malloc(8);
                    if (hash) {
                        memcpy(hash, line, 7);
                        hash[7] = '\0';
                    }
                    return hash;
                }
                return strdup(line);
            }
            fclose(fp);
            return NULL;
        }
        // Move up one directory
        char *slash = strrchr(dir, '/');
        if (!slash || slash == dir) {
            break;
        }
        *slash = '\0';
    }
    return NULL;
}

// Append a string to a dynamically growing buffer.
static void buf_append(char **buf, size_t *len, size_t *cap,
                       const char *str, size_t slen) {
    while (*len + slen + 1 > *cap) {
        *cap *= 2;
        *buf = realloc(*buf, *cap);
        if (!*buf) {
            fprintf(stderr, "splash: realloc failed: out of memory\n");
            abort();
        }
    }
    memcpy(*buf + *len, str, slen);
    *len += slen;
    (*buf)[*len] = '\0';
}

char *config_expand_prompt(const char *format) {
    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        fprintf(stderr, "splash: malloc failed: out of memory\n");
        abort();
    }
    buf[0] = '\0';

    const char *p = format;
    while (*p) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
                case 'u': {
                    const char *user = getenv("USER");
                    if (!user) user = "?";
                    buf_append(&buf, &len, &cap, user, strlen(user));
                    break;
                }
                case 'h': {
                    char hostname[256];
                    if (gethostname(hostname, sizeof(hostname)) == 0) {
                        // Truncate at first dot for short hostname
                        char *dot = strchr(hostname, '.');
                        if (dot) *dot = '\0';
                        buf_append(&buf, &len, &cap,
                                   hostname, strlen(hostname));
                    } else {
                        buf_append(&buf, &len, &cap, "?", 1);
                    }
                    break;
                }
                case 'w': {
                    char cwd[PATH_MAX];
                    if (getcwd(cwd, sizeof(cwd))) {
                        const char *home = getenv("HOME");
                        if (home && strncmp(cwd, home, strlen(home)) == 0 &&
                            (cwd[strlen(home)] == '/' ||
                             cwd[strlen(home)] == '\0')) {
                            buf_append(&buf, &len, &cap, "~", 1);
                            buf_append(&buf, &len, &cap,
                                       cwd + strlen(home),
                                       strlen(cwd + strlen(home)));
                        } else {
                            buf_append(&buf, &len, &cap, cwd, strlen(cwd));
                        }
                    } else {
                        buf_append(&buf, &len, &cap, "?", 1);
                    }
                    break;
                }
                case 'W': {
                    char cwd[PATH_MAX];
                    if (getcwd(cwd, sizeof(cwd))) {
                        char *base = strrchr(cwd, '/');
                        if (base && base[1]) {
                            buf_append(&buf, &len, &cap,
                                       base + 1, strlen(base + 1));
                        } else {
                            buf_append(&buf, &len, &cap, "/", 1);
                        }
                    } else {
                        buf_append(&buf, &len, &cap, "?", 1);
                    }
                    break;
                }
                case '$': {
                    const char *ch = (geteuid() == 0) ? "#" : "$";
                    buf_append(&buf, &len, &cap, ch, 1);
                    break;
                }
                case 'e': {
                    buf_append(&buf, &len, &cap, "\033", 1);
                    break;
                }
                case 'g': {
                    char *branch = get_git_branch();
                    if (branch) {
                        buf_append(&buf, &len, &cap,
                                   branch, strlen(branch));
                        free(branch);
                    }
                    break;
                }
                case '\\': {
                    buf_append(&buf, &len, &cap, "\\", 1);
                    break;
                }
                default: {
                    // Unknown escape — keep as-is
                    buf_append(&buf, &len, &cap, "\\", 1);
                    buf_append(&buf, &len, &cap, p, 1);
                    break;
                }
            }
            p++;
        } else {
            buf_append(&buf, &len, &cap, p, 1);
            p++;
        }
    }

    return buf;
}

char *config_build_prompt(void) {
    // Priority: $PROMPT env var > prompt.format config > default
    const char *fmt = getenv("PROMPT");
    if (!fmt || fmt[0] == '\0') {
        fmt = config_get_string("prompt.format");
    }
    if (!fmt || fmt[0] == '\0') {
        return strdup("splash> ");
    }
    return config_expand_prompt(fmt);
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
