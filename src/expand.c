#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "expand.h"
#include "util.h"

static int last_status = 0;
static int last_bg_pid = 0;
static char *last_arg = NULL;

// Static buffers for special variable string conversion
static char status_buf[16];
static char pid_buf[16];
static char bg_pid_buf[16];

void expand_set_last_status(int status) {
    last_status = status;
}

int expand_get_last_status(void) {
    return last_status;
}

void expand_set_last_bg_pid(int pid) {
    last_bg_pid = pid;
}

void expand_set_last_arg(const char *arg) {
    free(last_arg);
    last_arg = arg ? xstrdup(arg) : NULL;
}

const char *expand_variable(const char *name) {
    if (!name || *name == '\0') {
        return NULL;
    }

    // Special variables (single character)
    if (name[1] == '\0') {
        switch (name[0]) {
            case '?':
                snprintf(status_buf, sizeof(status_buf), "%d", last_status);
                return status_buf;
            case '$':
                snprintf(pid_buf, sizeof(pid_buf), "%d", getpid());
                return pid_buf;
            case '!':
                if (last_bg_pid == 0) {
                    return "";
                }
                snprintf(bg_pid_buf, sizeof(bg_pid_buf), "%d", last_bg_pid);
                return bg_pid_buf;
            case '_':
                return last_arg ? last_arg : "";
        }
    }

    return getenv(name);
}

char *expand_tilde(const char *word) {
    if (!word || word[0] != '~') {
        return NULL;
    }

    // ~ or ~/...
    if (word[1] == '\0' || word[1] == '/') {
        const char *home = getenv("HOME");
        if (!home) {
            return NULL;
        }
        size_t hlen = strlen(home);
        size_t rlen = strlen(word + 1); // includes the / if present
        char *result = xmalloc(hlen + rlen + 1);
        memcpy(result, home, hlen);
        memcpy(result + hlen, word + 1, rlen);
        result[hlen + rlen] = '\0';
        return result;
    }

    // ~user or ~user/...
    const char *slash = strchr(word + 1, '/');
    size_t ulen = slash ? (size_t)(slash - word - 1) : strlen(word + 1);
    char username[256];
    if (ulen >= sizeof(username)) {
        return NULL;
    }
    memcpy(username, word + 1, ulen);
    username[ulen] = '\0';

    struct passwd *pw = getpwnam(username);
    if (!pw) {
        return NULL;
    }

    size_t hlen = strlen(pw->pw_dir);
    const char *rest = slash ? slash : "";
    size_t rlen = strlen(rest);
    char *result = xmalloc(hlen + rlen + 1);
    memcpy(result, pw->pw_dir, hlen);
    memcpy(result + hlen, rest, rlen);
    result[hlen + rlen] = '\0';
    return result;
}
