#include <dirent.h>
#include <errno.h>
#include <libproc.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/proc_info.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "alias.h"
#include "builtins.h"
#include "executor.h"
#include "history.h"
#include "jobs.h"
#include "table.h"
#include "util.h"

#define SOURCE_MAX_DEPTH 16
static int source_depth = 0;

extern char **environ;

// exit [status]
static int builtin_exit(SimpleCommand *cmd) {
    int status = 0;
    if (cmd->argc > 1) {
        status = atoi(cmd->argv[1]);
    }
    printf("Viszontlátásra!!\n");
    printf("Jó egészséget és sok szerencsét kívánok!\n");
    exit(status);
}

// cd [dir]
static int builtin_cd(SimpleCommand *cmd) {
    const char *dir;
    if (cmd->argc < 2) {
        dir = getenv("HOME");
        if (!dir) {
            fprintf(stderr, "splash: cd: HOME not set\n");
            return 1;
        }
    } else if (strcmp(cmd->argv[1], "-") == 0) {
        dir = getenv("OLDPWD");
        if (!dir) {
            fprintf(stderr, "splash: cd: OLDPWD not set\n");
            return 1;
        }
        printf("%s\n", dir);
    } else {
        dir = cmd->argv[1];
    }

    char *oldpwd = getcwd(NULL, 0);

    if (chdir(dir) == -1) {
        fprintf(stderr, "splash: cd: %s: %s\n", dir, strerror(errno));
        free(oldpwd);
        return 1;
    }

    if (oldpwd) {
        setenv("OLDPWD", oldpwd, 1);
        free(oldpwd);
    }

    char *newpwd = getcwd(NULL, 0);
    if (newpwd) {
        setenv("PWD", newpwd, 1);
        free(newpwd);
    }

    return 0;
}

// jobs
static int builtin_jobs(void) {
    jobs_print();
    return 0;
}

// Wait for a foreground job (handles both running and continued-from-stop).
// Returns the exit status.
static int wait_for_fg_job(Job *j) {
    int interactive = isatty(STDIN_FILENO);

    // Give the job the terminal
    if (interactive) {
        tcsetpgrp(STDIN_FILENO, j->pgid);
    }

    // If stopped, continue it
    if (j->status == JOB_STOPPED) {
        j->status = JOB_RUNNING;
        if (kill(-j->pgid, SIGCONT) == -1) {
            fprintf(stderr, "splash: fg: kill(SIGCONT): %s\n", strerror(errno));
        }
    }

    // Wait for all processes in the job
    int last_status = 0;
    for (int i = 0; i < j->num_pids; i++) {
        int status;
        pid_t result;
        do {
            result = waitpid(j->pids[i], &status, WUNTRACED);
        } while (result == -1 && errno == EINTR);

        if (result > 0) {
            if (WIFSTOPPED(status)) {
                j->status = JOB_STOPPED;
                fprintf(stderr, "\n[%d] stopped\t%s\n", j->id, j->command);
                // Reclaim terminal for shell
                if (interactive) {
                    tcsetpgrp(STDIN_FILENO, jobs_get_shell_pgid());
                }
                return 128 + WSTOPSIG(status);
            }
            if (i == j->num_pids - 1) {
                if (WIFEXITED(status)) {
                    last_status = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    last_status = 128 + WTERMSIG(status);
                }
            }
        }
    }

    // Reclaim terminal for shell
    if (interactive) {
        tcsetpgrp(STDIN_FILENO, jobs_get_shell_pgid());
    }

    j->status = JOB_DONE;
    j->exit_status = last_status;
    jobs_remove(j->id);
    return last_status;
}

// fg [%N]
static int builtin_fg(SimpleCommand *cmd) {
    Job *j = NULL;

    if (cmd->argc < 2) {
        j = jobs_find_most_recent();
    } else {
        const char *arg = cmd->argv[1];
        int job_id;
        if (arg[0] == '%') {
            job_id = atoi(arg + 1);
        } else {
            job_id = atoi(arg);
        }
        j = jobs_find_by_id(job_id);
    }

    if (!j) {
        fprintf(stderr, "splash: fg: no such job\n");
        return 1;
    }

    printf("%s\n", j->command);
    j->background = 0;
    return wait_for_fg_job(j);
}

// bg [%N]
static int builtin_bg(SimpleCommand *cmd) {
    Job *j = NULL;

    if (cmd->argc < 2) {
        // Find most recent stopped job
        for (int i = MAX_JOBS - 1; i >= 0; i--) {
            // We can't directly access the table, so use find_most_recent
            // and check if it's stopped. But let's just find_most_recent.
            break;
        }
        j = jobs_find_most_recent();
        if (j && j->status != JOB_STOPPED) {
            // Search for a stopped one
            j = NULL;
            for (int id = MAX_JOBS; id >= 1; id--) {
                Job *candidate = jobs_find_by_id(id);
                if (candidate && candidate->status == JOB_STOPPED) {
                    j = candidate;
                    break;
                }
            }
        }
    } else {
        const char *arg = cmd->argv[1];
        int job_id;
        if (arg[0] == '%') {
            job_id = atoi(arg + 1);
        } else {
            job_id = atoi(arg);
        }
        j = jobs_find_by_id(job_id);
    }

    if (!j) {
        fprintf(stderr, "splash: bg: no such job\n");
        return 1;
    }

    if (j->status != JOB_STOPPED) {
        fprintf(stderr, "splash: bg: job %d is not stopped\n", j->id);
        return 1;
    }

    j->status = JOB_RUNNING;
    j->background = 1;
    printf("[%d] %s &\n", j->id, j->command);

    if (kill(-j->pgid, SIGCONT) == -1) {
        fprintf(stderr, "splash: bg: kill(SIGCONT): %s\n", strerror(errno));
        return 1;
    }

    return 0;
}

// printenv [VAR]
static int builtin_printenv(SimpleCommand *cmd) {
    if (cmd->argc > 1) {
        const char *val = getenv(cmd->argv[1]);
        if (val) {
            printf("%s\n", val);
            return 0;
        }
        return 1;
    }
    for (char **env = environ; *env; env++) {
        printf("%s\n", *env);
    }
    return 0;
}

// setenv VAR VALUE
static int builtin_setenv(SimpleCommand *cmd) {
    if (cmd->argc != 3) {
        fprintf(stderr, "splash: setenv: usage: setenv VAR VALUE\n");
        return 1;
    }
    if (setenv(cmd->argv[1], cmd->argv[2], 1) == -1) {
        fprintf(stderr, "splash: setenv: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}

// unsetenv VAR
static int builtin_unsetenv(SimpleCommand *cmd) {
    if (cmd->argc != 2) {
        fprintf(stderr, "splash: unsetenv: usage: unsetenv VAR\n");
        return 1;
    }
    if (unsetenv(cmd->argv[1]) == -1) {
        fprintf(stderr, "splash: unsetenv: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}

// export VAR=VALUE or export VAR
static int builtin_export(SimpleCommand *cmd) {
    if (cmd->argc < 2) {
        // No args: print all exported vars (same as printenv)
        for (char **env = environ; *env; env++) {
            printf("export %s\n", *env);
        }
        return 0;
    }
    for (int i = 1; i < cmd->argc; i++) {
        char *eq = strchr(cmd->argv[i], '=');
        if (eq) {
            // export VAR=VALUE
            size_t name_len = (size_t)(eq - cmd->argv[i]);
            char *name = xmalloc(name_len + 1);
            memcpy(name, cmd->argv[i], name_len);
            name[name_len] = '\0';
            if (setenv(name, eq + 1, 1) == -1) {
                fprintf(stderr, "splash: export: %s\n", strerror(errno));
                free(name);
                return 1;
            }
            free(name);
        }
        // export VAR (no =) — no-op, var is already in env if it exists
    }
    return 0;
}

// source <file>
static int builtin_source(SimpleCommand *cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "splash: source: usage: source <file>\n");
        return 1;
    }

    if (source_depth >= SOURCE_MAX_DEPTH) {
        fprintf(stderr, "splash: source: max recursion depth (%d) exceeded\n",
                SOURCE_MAX_DEPTH);
        return 1;
    }

    FILE *f = fopen(cmd->argv[1], "r");
    if (!f) {
        fprintf(stderr, "splash: source: %s: %s\n",
                cmd->argv[1], strerror(errno));
        return 1;
    }

    source_depth++;
    char line[4096];
    int last_status = 0;
    while (fgets(line, sizeof(line), f)) {
        // Strip trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }
        if (len == 0) {
            continue;
        }
        last_status = executor_execute_line(line);
    }
    source_depth--;

    fclose(f);
    return last_status;
}

// Search $PATH for an executable. Returns allocated path or NULL.
// Caller must free the returned string.
static char *find_in_path(const char *name) {
    // Absolute or relative path — check directly
    if (strchr(name, '/')) {
        if (access(name, X_OK) == 0) {
            return xstrdup(name);
        }
        return NULL;
    }

    const char *path_env = getenv("PATH");
    if (!path_env) {
        return NULL;
    }

    char *path_copy = xstrdup(path_env);
    char *saveptr = NULL;
    char *dir = strtok_r(path_copy, ":", &saveptr);
    while (dir) {
        size_t len = strlen(dir) + 1 + strlen(name) + 1;
        char *full = xmalloc(len);
        snprintf(full, len, "%s/%s", dir, name);
        if (access(full, X_OK) == 0) {
            free(path_copy);
            return full;
        }
        free(full);
        dir = strtok_r(NULL, ":", &saveptr);
    }
    free(path_copy);
    return NULL;
}

// type cmd [cmd ...]
static int builtin_type(SimpleCommand *cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "splash: type: usage: type name [name ...]\n");
        return 1;
    }
    int ret = 0;
    for (int i = 1; i < cmd->argc; i++) {
        const char *name = cmd->argv[i];
        const char *alias_val = alias_get(name);
        if (alias_val) {
            printf("%s is aliased to '%s'\n", name, alias_val);
        } else if (builtin_is_builtin(name)) {
            printf("%s is a shell builtin\n", name);
        } else {
            char *path = find_in_path(name);
            if (path) {
                printf("%s is %s\n", name, path);
                free(path);
            } else {
                fprintf(stderr, "splash: type: %s: not found\n", name);
                ret = 1;
            }
        }
    }
    return ret;
}

// which cmd [cmd ...]
static int builtin_which(SimpleCommand *cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "splash: which: usage: which name [name ...]\n");
        return 1;
    }
    int ret = 0;
    for (int i = 1; i < cmd->argc; i++) {
        const char *name = cmd->argv[i];
        const char *alias_val = alias_get(name);
        if (alias_val) {
            printf("%s: aliased to %s\n", name, alias_val);
        } else if (builtin_is_builtin(name)) {
            printf("%s: shell built-in command\n", name);
        } else {
            char *path = find_in_path(name);
            if (path) {
                printf("%s\n", path);
                free(path);
            } else {
                fprintf(stderr, "%s not found\n", name);
                ret = 1;
            }
        }
    }
    return ret;
}

// alias [name[='value']]
static int builtin_alias(SimpleCommand *cmd) {
    if (cmd->argc < 2) {
        alias_print_all();
        return 0;
    }
    for (int i = 1; i < cmd->argc; i++) {
        char *eq = strchr(cmd->argv[i], '=');
        if (eq) {
            size_t name_len = (size_t)(eq - cmd->argv[i]);
            char *name = xmalloc(name_len + 1);
            memcpy(name, cmd->argv[i], name_len);
            name[name_len] = '\0';
            const char *value = eq + 1;
            // Strip surrounding quotes if present
            size_t vlen = strlen(value);
            if (vlen >= 2 &&
                ((value[0] == '\'' && value[vlen - 1] == '\'') ||
                 (value[0] == '"' && value[vlen - 1] == '"'))) {
                char *stripped = xmalloc(vlen - 1);
                memcpy(stripped, value + 1, vlen - 2);
                stripped[vlen - 2] = '\0';
                alias_set(name, stripped);
                free(stripped);
            } else {
                alias_set(name, value);
            }
            free(name);
        } else {
            // Print specific alias
            const char *val = alias_get(cmd->argv[i]);
            if (val) {
                printf("alias %s='%s'\n", cmd->argv[i], val);
            } else {
                fprintf(stderr, "splash: alias: %s: not found\n",
                        cmd->argv[i]);
                return 1;
            }
        }
    }
    return 0;
}

// unalias name
static int builtin_unalias(SimpleCommand *cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "splash: unalias: usage: unalias name\n");
        return 1;
    }
    for (int i = 1; i < cmd->argc; i++) {
        if (alias_remove(cmd->argv[i]) == -1) {
            fprintf(stderr, "splash: unalias: %s: not found\n",
                    cmd->argv[i]);
            return 1;
        }
    }
    return 0;
}

// history
static int builtin_history(void) {
    history_print();
    return 0;
}

int builtin_is_builtin(const char *name) {
    return strcmp(name, "exit") == 0 ||
           strcmp(name, "cd") == 0 ||
           strcmp(name, "jobs") == 0 ||
           strcmp(name, "fg") == 0 ||
           strcmp(name, "bg") == 0 ||
           strcmp(name, "printenv") == 0 ||
           strcmp(name, "setenv") == 0 ||
           strcmp(name, "unsetenv") == 0 ||
           strcmp(name, "export") == 0 ||
           strcmp(name, "source") == 0 ||
           strcmp(name, "alias") == 0 ||
           strcmp(name, "unalias") == 0 ||
           strcmp(name, "type") == 0 ||
           strcmp(name, "which") == 0 ||
           strcmp(name, "history") == 0;
}

int builtin_execute(SimpleCommand *cmd) {
    const char *name = cmd->argv[0];

    if (strcmp(name, "exit") == 0)     return builtin_exit(cmd);
    if (strcmp(name, "cd") == 0)       return builtin_cd(cmd);
    if (strcmp(name, "jobs") == 0)     return builtin_jobs();
    if (strcmp(name, "fg") == 0)       return builtin_fg(cmd);
    if (strcmp(name, "bg") == 0)       return builtin_bg(cmd);
    if (strcmp(name, "printenv") == 0) return builtin_printenv(cmd);
    if (strcmp(name, "setenv") == 0)   return builtin_setenv(cmd);
    if (strcmp(name, "unsetenv") == 0) return builtin_unsetenv(cmd);
    if (strcmp(name, "export") == 0)   return builtin_export(cmd);
    if (strcmp(name, "source") == 0)   return builtin_source(cmd);
    if (strcmp(name, "alias") == 0)    return builtin_alias(cmd);
    if (strcmp(name, "unalias") == 0)  return builtin_unalias(cmd);
    if (strcmp(name, "type") == 0)     return builtin_type(cmd);
    if (strcmp(name, "which") == 0)    return builtin_which(cmd);
    if (strcmp(name, "history") == 0)  return builtin_history();

    fprintf(stderr, "splash: %s: unknown builtin\n", name);
    return 1;
}

// --- Structured builtins ---

// Format a permission mode into a string like "rwxr-xr-x".
// buf must be at least 10 bytes.
static void format_permissions(mode_t mode, char *buf) {
    buf[0] = (mode & S_IRUSR) ? 'r' : '-';
    buf[1] = (mode & S_IWUSR) ? 'w' : '-';
    buf[2] = (mode & S_IXUSR) ? 'x' : '-';
    buf[3] = (mode & S_IRGRP) ? 'r' : '-';
    buf[4] = (mode & S_IWGRP) ? 'w' : '-';
    buf[5] = (mode & S_IXGRP) ? 'x' : '-';
    buf[6] = (mode & S_IROTH) ? 'r' : '-';
    buf[7] = (mode & S_IWOTH) ? 'w' : '-';
    buf[8] = (mode & S_IXOTH) ? 'x' : '-';
    buf[9] = '\0';
}

// Return a static string for the file type from stat mode.
static const char *file_type_string(mode_t mode) {
    if (S_ISDIR(mode))  return "dir";
    if (S_ISLNK(mode))  return "symlink";
    if (S_ISFIFO(mode)) return "fifo";
    if (S_ISSOCK(mode)) return "socket";
    if (S_ISBLK(mode))  return "block";
    if (S_ISCHR(mode))  return "char";
    return "file";
}

// State for structured ls pipeline stage.
typedef struct {
    int yielded;        // 0 = table not yet built/returned, 1 = done
    char *path;         // Directory path (owned)
} LsStageState;

static Value *ls_stage_next(PipelineStage *self) {
    LsStageState *s = self->state;
    if (s->yielded) {
        return NULL;
    }
    s->yielded = 1;

    const char *col_names[] = {"name", "size", "permissions", "modified", "type"};
    ValueType col_types[] = {
        VALUE_STRING, VALUE_INT, VALUE_STRING, VALUE_STRING, VALUE_STRING
    };
    Table *t = table_new(col_names, col_types, 5);

    // Check if path is a single file (not a directory)
    struct stat path_st;
    if (lstat(s->path, &path_st) == -1) {
        fprintf(stderr, "splash: ls: %s: %s\n", s->path, strerror(errno));
        return value_table(t); // Return empty table
    }

    if (!S_ISDIR(path_st.st_mode)) {
        // Single file — return 1-row table
        char perms[10];
        format_permissions(path_st.st_mode, perms);
        char timebuf[64];
        struct tm *tm = localtime(&path_st.st_mtime);
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M", tm);

        Value *row[] = {
            value_string(s->path),
            value_int(path_st.st_size),
            value_string(perms),
            value_string(timebuf),
            value_string(file_type_string(path_st.st_mode))
        };
        table_add_row(t, row, 5);
        return value_table(t);
    }

    DIR *dir = opendir(s->path);
    if (!dir) {
        fprintf(stderr, "splash: ls: %s: %s\n", s->path, strerror(errno));
        return value_table(t); // Return empty table
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Build full path for stat
        size_t path_len = strlen(s->path);
        size_t name_len = strlen(entry->d_name);
        char *full = xmalloc(path_len + 1 + name_len + 1);
        memcpy(full, s->path, path_len);
        full[path_len] = '/';
        memcpy(full + path_len + 1, entry->d_name, name_len);
        full[path_len + 1 + name_len] = '\0';

        struct stat st;
        if (lstat(full, &st) == -1) {
            free(full);
            continue; // Skip entries we can't stat
        }
        free(full);

        char perms[10];
        format_permissions(st.st_mode, perms);

        char timebuf[64];
        struct tm *tm = localtime(&st.st_mtime);
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M", tm);

        Value *row[] = {
            value_string(entry->d_name),
            value_int(st.st_size),
            value_string(perms),
            value_string(timebuf),
            value_string(file_type_string(st.st_mode))
        };
        table_add_row(t, row, 5);
    }
    closedir(dir);

    return value_table(t);
}

static void ls_stage_free(PipelineStage *self) {
    LsStageState *s = self->state;
    free(s->path);
    free(s);
}

static PipelineStage *create_ls_stage(SimpleCommand *cmd) {
    const char *path = ".";
    if (cmd->argc > 1) {
        path = cmd->argv[1];
    }
    LsStageState *s = xmalloc(sizeof(LsStageState));
    s->yielded = 0;
    s->path = xstrdup(path);
    return pipeline_stage_new(ls_stage_next, ls_stage_free, s, NULL);
}


// --- Structured ps ---

// Map macOS process status to a human-readable string.
static const char *proc_status_string(unsigned int status) {
    switch (status) {
        case 1: return "idle";
        case 2: return "running";
        case 3: return "sleeping";
        case 4: return "stopped";
        case 5: return "zombie";
        default: return "unknown";
    }
}

typedef struct {
    int yielded;
} PsStageState;

static Value *ps_stage_next(PipelineStage *self) {
    PsStageState *s = self->state;
    if (s->yielded) {
        return NULL;
    }
    s->yielded = 1;

    const char *col_names[] = {"pid", "name", "cpu_time", "mem", "status"};
    ValueType col_types[] = {
        VALUE_INT, VALUE_STRING, VALUE_FLOAT, VALUE_INT, VALUE_STRING
    };
    Table *t = table_new(col_names, col_types, 5);

    // Get list of all pids
    int est = proc_listallpids(NULL, 0);
    if (est <= 0) {
        return value_table(t);
    }

    size_t buf_count = (size_t)est * 2;
    pid_t *pids = xmalloc(sizeof(pid_t) * buf_count);
    int actual = proc_listallpids(pids, (int)(sizeof(pid_t) * buf_count));
    if (actual <= 0) {
        free(pids);
        return value_table(t);
    }

    for (int i = 0; i < actual; i++) {
        pid_t pid = pids[i];
        if (pid == 0) {
            continue;
        }

        char name[256];
        int name_len = proc_name(pid, name, sizeof(name));
        if (name_len <= 0) {
            // Can't get name — likely permission denied, skip
            continue;
        }

        struct proc_taskinfo ti;
        int ti_sz = proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &ti,
                                 (int)sizeof(ti));

        struct proc_bsdinfo bi;
        int bi_sz = proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bi,
                                 (int)sizeof(bi));

        double cpu_time = 0.0;
        int64_t mem = 0;
        if (ti_sz >= (int)sizeof(ti)) {
            cpu_time = (double)(ti.pti_total_user + ti.pti_total_system) / 1e9;
            mem = (int64_t)ti.pti_resident_size;
        }

        const char *status_str = "unknown";
        if (bi_sz >= (int)sizeof(bi)) {
            status_str = proc_status_string(bi.pbi_status);
        }

        Value *row[] = {
            value_int(pid),
            value_string(name),
            value_float(cpu_time),
            value_int(mem),
            value_string(status_str)
        };
        table_add_row(t, row, 5);
    }

    free(pids);
    return value_table(t);
}

static void ps_stage_free(PipelineStage *self) {
    free(self->state);
}

static PipelineStage *create_ps_stage(void) {
    PsStageState *s = xmalloc(sizeof(PsStageState));
    s->yielded = 0;
    return pipeline_stage_new(ps_stage_next, ps_stage_free, s, NULL);
}


// --- Structured find ---

typedef struct {
    int yielded;
    char *path;
} FindStageState;

// Recursively walk a directory, adding rows to the table.
static void find_walk(Table *t, const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return; // Silently skip unreadable directories
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        size_t dir_len = strlen(dir_path);
        size_t name_len = strlen(entry->d_name);
        char *full = xmalloc(dir_len + 1 + name_len + 1);
        memcpy(full, dir_path, dir_len);
        full[dir_len] = '/';
        memcpy(full + dir_len + 1, entry->d_name, name_len);
        full[dir_len + 1 + name_len] = '\0';

        struct stat st;
        if (lstat(full, &st) == -1) {
            free(full);
            continue;
        }

        Value *row[] = {
            value_string(full),
            value_string(entry->d_name),
            value_int(st.st_size),
            value_string(file_type_string(st.st_mode))
        };
        table_add_row(t, row, 4);

        // Recurse into subdirectories (but not symlinks to avoid loops)
        if (S_ISDIR(st.st_mode)) {
            find_walk(t, full);
        }

        free(full);
    }
    closedir(dir);
}

static Value *find_stage_next(PipelineStage *self) {
    FindStageState *s = self->state;
    if (s->yielded) {
        return NULL;
    }
    s->yielded = 1;

    const char *col_names[] = {"path", "name", "size", "type"};
    ValueType col_types[] = {VALUE_STRING, VALUE_STRING, VALUE_INT, VALUE_STRING};
    Table *t = table_new(col_names, col_types, 4);

    struct stat root_st;
    if (lstat(s->path, &root_st) == -1) {
        fprintf(stderr, "splash: find: %s: %s\n", s->path, strerror(errno));
        return value_table(t);
    }

    if (!S_ISDIR(root_st.st_mode)) {
        // Single file
        const char *basename = strrchr(s->path, '/');
        basename = basename ? basename + 1 : s->path;
        Value *row[] = {
            value_string(s->path),
            value_string(basename),
            value_int(root_st.st_size),
            value_string(file_type_string(root_st.st_mode))
        };
        table_add_row(t, row, 4);
        return value_table(t);
    }

    find_walk(t, s->path);
    return value_table(t);
}

static void find_stage_free(PipelineStage *self) {
    FindStageState *s = self->state;
    free(s->path);
    free(s);
}

static PipelineStage *create_find_stage(SimpleCommand *cmd) {
    const char *path = ".";
    if (cmd->argc > 1) {
        path = cmd->argv[1];
    }
    FindStageState *s = xmalloc(sizeof(FindStageState));
    s->yielded = 0;
    s->path = xstrdup(path);
    return pipeline_stage_new(find_stage_next, find_stage_free, s, NULL);
}


int builtin_is_structured(const char *name) {
    return strcmp(name, "ls") == 0 ||
           strcmp(name, "ps") == 0 ||
           strcmp(name, "find") == 0;
}

PipelineStage *builtin_create_stage(SimpleCommand *cmd,
                                    PipelineStage *upstream) {
    const char *name = cmd->argv[0];
    pipeline_stage_free(upstream); // All current structured builtins are sources
    if (strcmp(name, "ls") == 0) {
        return create_ls_stage(cmd);
    }
    if (strcmp(name, "ps") == 0) {
        return create_ps_stage();
    }
    if (strcmp(name, "find") == 0) {
        return create_find_stage(cmd);
    }
    return NULL;
}
