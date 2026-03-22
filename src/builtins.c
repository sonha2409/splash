#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <libproc.h>
#include <math.h>
#include <regex.h>
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
#include "expand.h"
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

// return [N]
static int builtin_return(SimpleCommand *cmd) {
    if (!expand_in_function()) {
        fprintf(stderr, "splash: return: can only be used in a function\n");
        return 1;
    }
    int status = expand_get_last_status();
    if (cmd->argc > 1) {
        status = atoi(cmd->argv[1]);
    }
    expand_set_last_status(status);
    expand_set_return_pending(1);
    return status;
}

// local VAR=VALUE or local VAR
static int builtin_local(SimpleCommand *cmd) {
    if (!expand_in_function()) {
        fprintf(stderr, "splash: local: can only be used in a function\n");
        return 1;
    }
    if (cmd->argc < 2) {
        fprintf(stderr, "splash: local: usage: local VAR[=VALUE] ...\n");
        return 1;
    }
    for (int i = 1; i < cmd->argc; i++) {
        char *eq = strchr(cmd->argv[i], '=');
        if (eq) {
            // local VAR=VALUE
            size_t name_len = (size_t)(eq - cmd->argv[i]);
            char *name = xmalloc(name_len + 1);
            memcpy(name, cmd->argv[i], name_len);
            name[name_len] = '\0';
            expand_save_local(name, eq + 1);
            free(name);
        } else {
            // local VAR (no value — set to empty)
            expand_save_local(cmd->argv[i], NULL);
        }
    }
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
           strcmp(name, "history") == 0 ||
           strcmp(name, "local") == 0 ||
           strcmp(name, "return") == 0;
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
    if (strcmp(name, "local") == 0)    return builtin_local(cmd);
    if (strcmp(name, "return") == 0)   return builtin_return(cmd);

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


// --- Structured env ---

typedef struct {
    int yielded;
} EnvStageState;

static Value *env_stage_next(PipelineStage *self) {
    EnvStageState *s = self->state;
    if (s->yielded) {
        return NULL;
    }
    s->yielded = 1;

    const char *col_names[] = {"key", "value"};
    ValueType col_types[] = {VALUE_STRING, VALUE_STRING};
    Table *t = table_new(col_names, col_types, 2);

    for (char **env = environ; *env; env++) {
        char *eq = strchr(*env, '=');
        if (!eq) {
            continue;
        }
        size_t key_len = (size_t)(eq - *env);
        char *key = xmalloc(key_len + 1);
        memcpy(key, *env, key_len);
        key[key_len] = '\0';

        Value *row[] = {
            value_string(key),
            value_string(eq + 1)
        };
        table_add_row(t, row, 2);
        free(key);
    }

    return value_table(t);
}

static void env_stage_free(PipelineStage *self) {
    free(self->state);
}

static PipelineStage *create_env_stage(void) {
    EnvStageState *s = xmalloc(sizeof(EnvStageState));
    s->yielded = 0;
    return pipeline_stage_new(env_stage_next, env_stage_free, s, NULL);
}


// --- Structured where filter ---

typedef enum {
    WHERE_EQ,       // ==
    WHERE_NE,       // !=
    WHERE_GT,       // >
    WHERE_LT,       // <
    WHERE_GE,       // >=
    WHERE_LE,       // <=
    WHERE_REGEX     // =~
} WhereOp;

typedef struct {
    char *col_name;     // Column to compare (owned)
    WhereOp op;
    char *raw_val;      // Raw comparison value string (owned)
    regex_t *compiled;  // Compiled regex for =~ (owned, or NULL)
} WhereStageState;

// Parse comparison value and compare against cell. Returns 1 if match.
static int where_compare(const Value *cell, WhereOp op, const char *raw_val,
                          regex_t *compiled) {
    if (!cell || cell->type == VALUE_NIL) {
        return 0;
    }

    if (op == WHERE_REGEX) {
        if (!compiled) {
            return 0;
        }
        char *cell_str = value_to_string(cell);
        int match = regexec(compiled, cell_str, 0, NULL, 0) == 0;
        free(cell_str);
        return match;
    }

    // Try numeric comparison first
    if (cell->type == VALUE_INT) {
        char *end;
        long long cmp_val = strtoll(raw_val, &end, 10);
        if (*end == '\0') {
            int64_t a = cell->integer;
            int64_t b = (int64_t)cmp_val;
            switch (op) {
                case WHERE_EQ: return a == b;
                case WHERE_NE: return a != b;
                case WHERE_GT: return a > b;
                case WHERE_LT: return a < b;
                case WHERE_GE: return a >= b;
                case WHERE_LE: return a <= b;
                default: return 0;
            }
        }
    }

    if (cell->type == VALUE_FLOAT) {
        char *end;
        double cmp_val = strtod(raw_val, &end);
        if (*end == '\0') {
            double a = cell->floating;
            double b = cmp_val;
            switch (op) {
                case WHERE_EQ: return a == b;
                case WHERE_NE: return a != b;
                case WHERE_GT: return a > b;
                case WHERE_LT: return a < b;
                case WHERE_GE: return a >= b;
                case WHERE_LE: return a <= b;
                default: return 0;
            }
        }
    }

    // String comparison
    char *cell_str = value_to_string(cell);
    int cmp = strcmp(cell_str, raw_val);
    free(cell_str);

    switch (op) {
        case WHERE_EQ: return cmp == 0;
        case WHERE_NE: return cmp != 0;
        case WHERE_GT: return cmp > 0;
        case WHERE_LT: return cmp < 0;
        case WHERE_GE: return cmp >= 0;
        case WHERE_LE: return cmp <= 0;
        default: return 0;
    }
}

static Value *where_stage_next(PipelineStage *self) {
    WhereStageState *s = self->state;

    Value *v;
    while ((v = self->upstream->next(self->upstream)) != NULL) {
        if (v->type != VALUE_TABLE) {
            return v; // Pass through non-table values
        }

        Table *src = v->table;
        int col_idx = table_col_index(src, s->col_name);
        if (col_idx < 0) {
            fprintf(stderr, "splash: where: column '%s' not found\n",
                    s->col_name);
            return v; // Return unfiltered if column not found
        }

        // Build filtered table with same schema
        const char **names = xmalloc(src->col_count * sizeof(char *));
        ValueType *types = xmalloc(src->col_count * sizeof(ValueType));
        for (size_t i = 0; i < src->col_count; i++) {
            names[i] = src->columns[i].name;
            types[i] = src->columns[i].type;
        }
        Table *filtered = table_new(names, types, src->col_count);
        free(names);
        free(types);

        for (size_t r = 0; r < src->row_count; r++) {
            Value *cell = table_get(src, r, (size_t)col_idx);
            if (where_compare(cell, s->op, s->raw_val, s->compiled)) {
                // Clone row values into filtered table
                Value **row_vals = xmalloc(src->col_count * sizeof(Value *));
                for (size_t c = 0; c < src->col_count; c++) {
                    row_vals[c] = value_clone(table_get(src, r, c));
                }
                table_add_row(filtered, row_vals, src->col_count);
                free(row_vals);
            }
        }

        value_free(v);
        return value_table(filtered);
    }

    return NULL;
}

static void where_stage_free(PipelineStage *self) {
    WhereStageState *s = self->state;
    free(s->col_name);
    free(s->raw_val);
    if (s->compiled) {
        regfree(s->compiled);
        free(s->compiled);
    }
    free(s);
}

static PipelineStage *create_where_stage(SimpleCommand *cmd,
                                         PipelineStage *upstream) {
    // Usage: where <col> <op> <val>
    if (cmd->argc < 4) {
        fprintf(stderr, "splash: where: usage: where <column> <op> <value>\n");
        return upstream; // Return upstream unchanged
    }

    const char *col = cmd->argv[1];
    const char *op_str = cmd->argv[2];
    const char *val = cmd->argv[3];

    WhereOp op;
    if (strcmp(op_str, "==") == 0)      op = WHERE_EQ;
    else if (strcmp(op_str, "!=") == 0) op = WHERE_NE;
    else if (strcmp(op_str, ">") == 0)  op = WHERE_GT;
    else if (strcmp(op_str, "<") == 0)  op = WHERE_LT;
    else if (strcmp(op_str, ">=") == 0) op = WHERE_GE;
    else if (strcmp(op_str, "<=") == 0) op = WHERE_LE;
    else if (strcmp(op_str, "=~") == 0) op = WHERE_REGEX;
    else {
        fprintf(stderr, "splash: where: unknown operator '%s'\n", op_str);
        return upstream;
    }

    WhereStageState *s = xmalloc(sizeof(WhereStageState));
    s->col_name = xstrdup(col);
    s->op = op;
    s->raw_val = xstrdup(val);
    s->compiled = NULL;

    if (op == WHERE_REGEX) {
        s->compiled = xmalloc(sizeof(regex_t));
        int rc = regcomp(s->compiled, val, REG_EXTENDED | REG_NOSUB);
        if (rc != 0) {
            char errbuf[128];
            regerror(rc, s->compiled, errbuf, sizeof(errbuf));
            fprintf(stderr, "splash: where: bad regex '%s': %s\n", val, errbuf);
            regfree(s->compiled);
            free(s->compiled);
            s->compiled = NULL;
        }
    }

    return pipeline_stage_new(where_stage_next, where_stage_free, s, upstream);
}


// --- Structured sort filter ---

typedef struct {
    char *col_name;
    int descending;
} SortStageState;

// Comparison context for qsort_r — we need col index and direction.
typedef struct {
    int col_idx;
    int descending;
} SortCtx;

static int sort_cmp(void *ctx_ptr, const void *a, const void *b) {
    SortCtx *ctx = ctx_ptr;
    const Row *ra = a;
    const Row *rb = b;
    Value *va = ra->values[ctx->col_idx];
    Value *vb = rb->values[ctx->col_idx];

    // NIL sorts last
    if (va->type == VALUE_NIL && vb->type == VALUE_NIL) return 0;
    if (va->type == VALUE_NIL) return 1;
    if (vb->type == VALUE_NIL) return -1;

    int result = 0;
    if (va->type == VALUE_INT && vb->type == VALUE_INT) {
        if (va->integer < vb->integer) result = -1;
        else if (va->integer > vb->integer) result = 1;
    } else if (va->type == VALUE_FLOAT && vb->type == VALUE_FLOAT) {
        if (va->floating < vb->floating) result = -1;
        else if (va->floating > vb->floating) result = 1;
    } else {
        char *sa = value_to_string(va);
        char *sb = value_to_string(vb);
        result = strcmp(sa, sb);
        free(sa);
        free(sb);
    }

    return ctx->descending ? -result : result;
}

static Value *sort_stage_next(PipelineStage *self) {
    SortStageState *s = self->state;

    Value *v;
    while ((v = self->upstream->next(self->upstream)) != NULL) {
        if (v->type != VALUE_TABLE) {
            return v;
        }

        Table *src = v->table;
        int col_idx = table_col_index(src, s->col_name);
        if (col_idx < 0) {
            fprintf(stderr, "splash: sort: column '%s' not found\n",
                    s->col_name);
            return v;
        }

        // Clone the table and sort its rows in-place
        Table *sorted = table_clone(src);
        value_free(v);

        SortCtx ctx = {.col_idx = col_idx, .descending = s->descending};
        qsort_r(sorted->rows, sorted->row_count, sizeof(Row), &ctx, sort_cmp);

        return value_table(sorted);
    }
    return NULL;
}

static void sort_stage_free(PipelineStage *self) {
    SortStageState *s = self->state;
    free(s->col_name);
    free(s);
}

static PipelineStage *create_sort_stage(SimpleCommand *cmd,
                                        PipelineStage *upstream) {
    if (cmd->argc < 2) {
        fprintf(stderr, "splash: sort: usage: sort <column> [--desc]\n");
        return upstream;
    }
    SortStageState *s = xmalloc(sizeof(SortStageState));
    s->col_name = xstrdup(cmd->argv[1]);
    s->descending = 0;
    for (int i = 2; i < cmd->argc; i++) {
        if (strcmp(cmd->argv[i], "--desc") == 0) {
            s->descending = 1;
        }
    }
    return pipeline_stage_new(sort_stage_next, sort_stage_free, s, upstream);
}


// --- Structured select filter ---

typedef struct {
    char **col_names;
    int num_cols;
} SelectStageState;

static Value *select_stage_next(PipelineStage *self) {
    SelectStageState *s = self->state;

    Value *v;
    while ((v = self->upstream->next(self->upstream)) != NULL) {
        if (v->type != VALUE_TABLE) {
            return v;
        }

        Table *src = v->table;

        // Resolve column indices
        int *indices = xmalloc((size_t)s->num_cols * sizeof(int));
        const char **names = xmalloc((size_t)s->num_cols * sizeof(char *));
        ValueType *types = xmalloc((size_t)s->num_cols * sizeof(ValueType));
        int valid = 0;
        for (int i = 0; i < s->num_cols; i++) {
            int idx = table_col_index(src, s->col_names[i]);
            if (idx >= 0) {
                indices[valid] = idx;
                names[valid] = src->columns[idx].name;
                types[valid] = src->columns[idx].type;
                valid++;
            } else {
                fprintf(stderr, "splash: select: column '%s' not found\n",
                        s->col_names[i]);
            }
        }

        if (valid == 0) {
            free(indices);
            free(names);
            free(types);
            return v; // No valid columns — return original
        }

        Table *result = table_new(names, types, (size_t)valid);
        for (size_t r = 0; r < src->row_count; r++) {
            Value **row_vals = xmalloc((size_t)valid * sizeof(Value *));
            for (int c = 0; c < valid; c++) {
                row_vals[c] = value_clone(table_get(src, r, (size_t)indices[c]));
            }
            table_add_row(result, row_vals, (size_t)valid);
            free(row_vals);
        }

        free(indices);
        free(names);
        free(types);
        value_free(v);
        return value_table(result);
    }
    return NULL;
}

static void select_stage_free(PipelineStage *self) {
    SelectStageState *s = self->state;
    for (int i = 0; i < s->num_cols; i++) {
        free(s->col_names[i]);
    }
    free(s->col_names);
    free(s);
}

static PipelineStage *create_select_stage(SimpleCommand *cmd,
                                          PipelineStage *upstream) {
    if (cmd->argc < 2) {
        fprintf(stderr, "splash: select: usage: select <col1> [col2] ...\n");
        return upstream;
    }
    SelectStageState *s = xmalloc(sizeof(SelectStageState));
    s->num_cols = cmd->argc - 1;
    s->col_names = xmalloc((size_t)s->num_cols * sizeof(char *));
    for (int i = 0; i < s->num_cols; i++) {
        s->col_names[i] = xstrdup(cmd->argv[i + 1]);
    }
    return pipeline_stage_new(select_stage_next, select_stage_free, s, upstream);
}


// --- Structured first / last filters ---

typedef struct {
    int n;
    int is_last;    // 0 = first, 1 = last
} FirstLastStageState;

static Value *first_last_stage_next(PipelineStage *self) {
    FirstLastStageState *s = self->state;

    Value *v;
    while ((v = self->upstream->next(self->upstream)) != NULL) {
        if (v->type != VALUE_TABLE) {
            return v;
        }

        Table *src = v->table;
        size_t total = src->row_count;
        size_t take = (size_t)s->n;
        if (take > total) {
            take = total;
        }

        // Determine start index
        size_t start = 0;
        if (s->is_last) {
            start = total - take;
        }

        // Build new table with subset of rows
        const char **names = xmalloc(src->col_count * sizeof(char *));
        ValueType *types = xmalloc(src->col_count * sizeof(ValueType));
        for (size_t i = 0; i < src->col_count; i++) {
            names[i] = src->columns[i].name;
            types[i] = src->columns[i].type;
        }
        Table *result = table_new(names, types, src->col_count);
        free(names);
        free(types);

        for (size_t r = start; r < start + take; r++) {
            Value **row_vals = xmalloc(src->col_count * sizeof(Value *));
            for (size_t c = 0; c < src->col_count; c++) {
                row_vals[c] = value_clone(table_get(src, r, c));
            }
            table_add_row(result, row_vals, src->col_count);
            free(row_vals);
        }

        value_free(v);
        return value_table(result);
    }
    return NULL;
}

static void first_last_stage_free(PipelineStage *self) {
    free(self->state);
}

static PipelineStage *create_first_last_stage(SimpleCommand *cmd,
                                              PipelineStage *upstream,
                                              int is_last) {
    int n = 10; // Default
    if (cmd->argc > 1) {
        n = atoi(cmd->argv[1]);
        if (n <= 0) n = 10;
    }
    FirstLastStageState *s = xmalloc(sizeof(FirstLastStageState));
    s->n = n;
    s->is_last = is_last;
    return pipeline_stage_new(first_last_stage_next, first_last_stage_free,
                              s, upstream);
}


// --- Structured count filter ---

static Value *count_stage_next(PipelineStage *self) {
    Value *v;
    while ((v = self->upstream->next(self->upstream)) != NULL) {
        if (v->type != VALUE_TABLE) {
            value_free(v);
            continue;
        }
        size_t n = v->table->row_count;
        value_free(v);
        return value_int((int64_t)n);
    }
    return NULL;
}

static PipelineStage *create_count_stage(PipelineStage *upstream) {
    return pipeline_stage_new(count_stage_next, NULL, NULL, upstream);
}


// --- Helper: read all data from a FILE* into a dynamically allocated buffer ---

static char *read_all_from_file(FILE *f, size_t *out_len) {
    size_t cap = 4096;
    size_t len = 0;
    char *buf = xmalloc(cap);

    for (;;) {
        size_t n = fread(buf + len, 1, cap - len, f);
        len += n;
        if (n == 0) break;
        if (len == cap) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
    }
    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
}


// --- Structured from-lines source ---
// Reads text from input_fd, splits by newline, produces a single-column table.

typedef struct {
    Value *table_val;
    int yielded;
} FromLinesState;

static Value *from_lines_stage_next(PipelineStage *self) {
    FromLinesState *s = self->state;
    if (s->yielded || !s->table_val) return NULL;
    s->yielded = 1;
    return value_clone(s->table_val);
}

static void from_lines_stage_free(PipelineStage *self) {
    FromLinesState *s = self->state;
    if (s->table_val) value_free(s->table_val);
    free(s);
}

static PipelineStage *create_from_lines_stage(int input_fd) {
    // Dup stdin so fclose won't close the original fd
    int fd = (input_fd == STDIN_FILENO) ? dup(input_fd) : input_fd;
    if (fd < 0) {
        fprintf(stderr, "splash: from-lines: %s\n", strerror(errno));
        return NULL;
    }
    FILE *f = fdopen(fd, "r");
    if (!f) {
        fprintf(stderr, "splash: from-lines: %s\n", strerror(errno));
        close(fd);
        return NULL;
    }

    size_t len = 0;
    char *buf = read_all_from_file(f, &len);
    fclose(f);

    const char *col_names[] = {"line"};
    ValueType col_types[] = {VALUE_STRING};
    Table *t = table_new(col_names, col_types, 1);

    // Split by newline
    char *p = buf;
    while (*p) {
        char *nl = strchr(p, '\n');
        size_t line_len = nl ? (size_t)(nl - p) : strlen(p);
        if (line_len == 0 && !nl) break; // trailing empty after last newline

        char *line = xmalloc(line_len + 1);
        memcpy(line, p, line_len);
        line[line_len] = '\0';

        Value *row[] = {value_string(line)};
        table_add_row(t, row, 1);
        free(line);

        p = nl ? nl + 1 : p + line_len;
    }

    free(buf);

    FromLinesState *s = xmalloc(sizeof(FromLinesState));
    s->table_val = value_table(t);
    s->yielded = 0;
    return pipeline_stage_new(from_lines_stage_next, from_lines_stage_free,
                              s, NULL);
}


// --- Structured from-csv source ---
// Reads CSV text from input_fd. First line = headers, subsequent lines = data.
// Supports RFC 4180 quoted fields. Type inference: int → float → string.

// Parse a single CSV field starting at *pos. Advances *pos past the field
// (and the delimiter/newline). Returns an owned string.
static char *csv_parse_field(const char *buf, size_t len, size_t *pos) {
    size_t cap = 64;
    size_t flen = 0;
    char *field = xmalloc(cap);

    if (*pos < len && buf[*pos] == '"') {
        // Quoted field
        (*pos)++; // skip opening quote
        while (*pos < len) {
            if (buf[*pos] == '"') {
                if (*pos + 1 < len && buf[*pos + 1] == '"') {
                    // Escaped quote
                    if (flen + 1 >= cap) { cap *= 2; field = xrealloc(field, cap); }
                    field[flen++] = '"';
                    *pos += 2;
                } else {
                    // End of quoted field
                    (*pos)++; // skip closing quote
                    break;
                }
            } else {
                if (flen + 1 >= cap) { cap *= 2; field = xrealloc(field, cap); }
                field[flen++] = buf[*pos];
                (*pos)++;
            }
        }
        // Skip trailing comma or newline
        if (*pos < len && buf[*pos] == ',') (*pos)++;
    } else {
        // Unquoted field — read until comma or newline or end
        while (*pos < len && buf[*pos] != ',' && buf[*pos] != '\n' && buf[*pos] != '\r') {
            if (flen + 1 >= cap) { cap *= 2; field = xrealloc(field, cap); }
            field[flen++] = buf[*pos];
            (*pos)++;
        }
        if (*pos < len && buf[*pos] == ',') (*pos)++;
    }

    field[flen] = '\0';
    return field;
}

// Parse one CSV line into an array of fields. Returns field count.
// Advances *pos past the line (including newline).
static size_t csv_parse_line(const char *buf, size_t len, size_t *pos,
                              char ***out_fields) {
    size_t cap = 8;
    size_t count = 0;
    char **fields = xmalloc(sizeof(char *) * cap);

    while (*pos < len && buf[*pos] != '\n' && buf[*pos] != '\r') {
        if (count >= cap) { cap *= 2; fields = xrealloc(fields, sizeof(char *) * cap); }
        fields[count++] = csv_parse_field(buf, len, pos);
    }
    // Handle empty line case: if we haven't parsed any fields, still return 0
    // Skip newline
    if (*pos < len && buf[*pos] == '\r') (*pos)++;
    if (*pos < len && buf[*pos] == '\n') (*pos)++;

    *out_fields = fields;
    return count;
}

// Try to infer the best Value type for a string.
static Value *csv_infer_value(const char *s) {
    if (!s || !*s) return value_string("");

    // Try integer
    char *end;
    errno = 0;
    long long iv = strtoll(s, &end, 10);
    if (*end == '\0' && errno == 0 && end != s) {
        return value_int((int64_t)iv);
    }

    // Try float
    errno = 0;
    double fv = strtod(s, &end);
    if (*end == '\0' && errno == 0 && end != s && isfinite(fv)) {
        return value_float(fv);
    }

    // Boolean
    if (strcmp(s, "true") == 0) return value_bool(true);
    if (strcmp(s, "false") == 0) return value_bool(false);

    return value_string(s);
}

typedef struct {
    Value *table_val;
    int yielded;
} FromCsvState;

static Value *from_csv_stage_next(PipelineStage *self) {
    FromCsvState *s = self->state;
    if (s->yielded || !s->table_val) return NULL;
    s->yielded = 1;
    return value_clone(s->table_val);
}

static void from_csv_stage_free(PipelineStage *self) {
    FromCsvState *s = self->state;
    if (s->table_val) value_free(s->table_val);
    free(s);
}

static PipelineStage *create_from_csv_stage(int input_fd) {
    int fd = (input_fd == STDIN_FILENO) ? dup(input_fd) : input_fd;
    if (fd < 0) {
        fprintf(stderr, "splash: from-csv: %s\n", strerror(errno));
        return NULL;
    }
    FILE *f = fdopen(fd, "r");
    if (!f) {
        fprintf(stderr, "splash: from-csv: %s\n", strerror(errno));
        close(fd);
        return NULL;
    }

    size_t len = 0;
    char *buf = read_all_from_file(f, &len);
    fclose(f);

    if (len == 0) {
        free(buf);
        FromCsvState *s = xmalloc(sizeof(FromCsvState));
        s->table_val = NULL;
        s->yielded = 0;
        return pipeline_stage_new(from_csv_stage_next, from_csv_stage_free,
                                  s, NULL);
    }

    // Parse header line
    size_t pos = 0;
    char **headers = NULL;
    size_t ncols = csv_parse_line(buf, len, &pos, &headers);

    if (ncols == 0) {
        free(headers);
        free(buf);
        FromCsvState *s = xmalloc(sizeof(FromCsvState));
        s->table_val = NULL;
        s->yielded = 0;
        return pipeline_stage_new(from_csv_stage_next, from_csv_stage_free,
                                  s, NULL);
    }

    // Create table with STRING types initially (we'll infer later)
    ValueType *types = xmalloc(sizeof(ValueType) * ncols);
    for (size_t i = 0; i < ncols; i++) types[i] = VALUE_STRING;

    Table *t = table_new((const char **)headers, types, ncols);
    free(types);

    // Parse data rows
    while (pos < len) {
        // Skip blank lines
        if (buf[pos] == '\n' || buf[pos] == '\r') {
            if (buf[pos] == '\r') pos++;
            if (pos < len && buf[pos] == '\n') pos++;
            continue;
        }

        char **fields = NULL;
        size_t nfields = csv_parse_line(buf, len, &pos, &fields);

        Value **row = xmalloc(sizeof(Value *) * ncols);
        for (size_t i = 0; i < ncols; i++) {
            if (i < nfields) {
                row[i] = csv_infer_value(fields[i]);
            } else {
                row[i] = value_string("");
            }
        }
        table_add_row(t, row, ncols);

        for (size_t i = 0; i < nfields; i++) free(fields[i]);
        free(fields);
        free(row);
    }

    for (size_t i = 0; i < ncols; i++) free(headers[i]);
    free(headers);
    free(buf);

    // Update column types based on dominant type in each column
    for (size_t c = 0; c < t->col_count; c++) {
        int all_int = 1, all_float = 1;
        for (size_t r = 0; r < t->row_count; r++) {
            Value *v = table_get(t, r, c);
            if (!v) continue;
            if (v->type != VALUE_INT) all_int = 0;
            if (v->type != VALUE_FLOAT && v->type != VALUE_INT) all_float = 0;
        }
        if (all_int && t->row_count > 0) {
            t->columns[c].type = VALUE_INT;
        } else if (all_float && t->row_count > 0) {
            t->columns[c].type = VALUE_FLOAT;
        }
    }

    FromCsvState *s = xmalloc(sizeof(FromCsvState));
    s->table_val = value_table(t);
    s->yielded = 0;
    return pipeline_stage_new(from_csv_stage_next, from_csv_stage_free,
                              s, NULL);
}


// --- Structured from-json source ---
// Parses JSON array of objects into a table.
// Minimal hand-rolled JSON parser — supports: objects, arrays, strings,
// numbers, bools, null. Nested values are stringified.

// JSON parser state
typedef struct {
    const char *buf;
    size_t len;
    size_t pos;
} JsonParser;

static void json_skip_whitespace(JsonParser *p) {
    while (p->pos < p->len && isspace((unsigned char)p->buf[p->pos])) {
        p->pos++;
    }
}

static int json_peek(JsonParser *p) {
    json_skip_whitespace(p);
    return p->pos < p->len ? p->buf[p->pos] : -1;
}

static int json_consume(JsonParser *p, char c) {
    json_skip_whitespace(p);
    if (p->pos < p->len && p->buf[p->pos] == c) {
        p->pos++;
        return 1;
    }
    return 0;
}

// Parse a JSON string (opening " already peeked). Returns owned string, or NULL.
static char *json_parse_string(JsonParser *p) {
    if (!json_consume(p, '"')) return NULL;

    size_t cap = 64;
    size_t len = 0;
    char *s = xmalloc(cap);

    while (p->pos < p->len && p->buf[p->pos] != '"') {
        if (p->buf[p->pos] == '\\' && p->pos + 1 < p->len) {
            p->pos++;
            char esc = p->buf[p->pos++];
            char c;
            switch (esc) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'u': {
                    // Simple: skip 4 hex digits, replace with '?'
                    for (int i = 0; i < 4 && p->pos < p->len; i++) p->pos++;
                    c = '?';
                    break;
                }
                default: c = esc; break;
            }
            if (len + 1 >= cap) { cap *= 2; s = xrealloc(s, cap); }
            s[len++] = c;
        } else {
            if (len + 1 >= cap) { cap *= 2; s = xrealloc(s, cap); }
            s[len++] = p->buf[p->pos++];
        }
    }
    json_consume(p, '"'); // closing quote
    s[len] = '\0';
    return s;
}

// Parse a JSON value. Returns an owned Value*, or NULL on parse error.
static Value *json_parse_value(JsonParser *p) {
    int c = json_peek(p);
    if (c < 0) return NULL;

    if (c == '"') {
        char *s = json_parse_string(p);
        if (!s) return NULL;
        Value *v = value_string(s);
        free(s);
        return v;
    }

    if (c == 't') {
        if (p->pos + 4 <= p->len && strncmp(p->buf + p->pos, "true", 4) == 0) {
            p->pos += 4;
            return value_bool(true);
        }
        return NULL;
    }

    if (c == 'f') {
        if (p->pos + 5 <= p->len && strncmp(p->buf + p->pos, "false", 5) == 0) {
            p->pos += 5;
            return value_bool(false);
        }
        return NULL;
    }

    if (c == 'n') {
        if (p->pos + 4 <= p->len && strncmp(p->buf + p->pos, "null", 4) == 0) {
            p->pos += 4;
            return value_nil();
        }
        return NULL;
    }

    if (c == '[') {
        // Parse array — stringify it
        // We need to save position and re-parse to stringify
        size_t start = p->pos;
        p->pos++; // skip [
        int depth = 1;
        int in_string = 0;
        while (p->pos < p->len && depth > 0) {
            char ch = p->buf[p->pos];
            if (in_string) {
                if (ch == '\\') { p->pos++; } // skip escaped char
                else if (ch == '"') { in_string = 0; }
            } else {
                if (ch == '"') { in_string = 1; }
                else if (ch == '[') { depth++; }
                else if (ch == ']') { depth--; }
            }
            p->pos++;
        }
        size_t slen = p->pos - start;
        char *s = xmalloc(slen + 1);
        memcpy(s, p->buf + start, slen);
        s[slen] = '\0';
        Value *v = value_string(s);
        free(s);
        return v;
    }

    if (c == '{') {
        // Nested object — stringify it
        size_t start = p->pos;
        p->pos++; // skip {
        int depth = 1;
        int in_string = 0;
        while (p->pos < p->len && depth > 0) {
            char ch = p->buf[p->pos];
            if (in_string) {
                if (ch == '\\') { p->pos++; }
                else if (ch == '"') { in_string = 0; }
            } else {
                if (ch == '"') { in_string = 1; }
                else if (ch == '{') { depth++; }
                else if (ch == '}') { depth--; }
            }
            p->pos++;
        }
        size_t slen = p->pos - start;
        char *s = xmalloc(slen + 1);
        memcpy(s, p->buf + start, slen);
        s[slen] = '\0';
        Value *v = value_string(s);
        free(s);
        return v;
    }

    // Number
    if (c == '-' || (c >= '0' && c <= '9')) {
        char *end;
        const char *start = p->buf + p->pos;

        // Try integer first
        errno = 0;
        long long iv = strtoll(start, &end, 10);
        if (end > start && (*end == ',' || *end == '}' || *end == ']' ||
            isspace((unsigned char)*end) || *end == '\0')) {
            if (errno == 0) {
                p->pos = (size_t)(end - p->buf);
                return value_int((int64_t)iv);
            }
        }

        // Try float
        errno = 0;
        double fv = strtod(start, &end);
        if (end > start && errno == 0) {
            p->pos = (size_t)(end - p->buf);
            return value_float(fv);
        }
        return NULL;
    }

    return NULL;
}

// Parse a JSON object into parallel arrays of keys and values.
// Returns number of key-value pairs, or -1 on error.
typedef struct {
    char **keys;
    Value **values;
    size_t count;
} JsonObject;

static int json_parse_object(JsonParser *p, JsonObject *obj) {
    if (!json_consume(p, '{')) return -1;

    size_t cap = 8;
    obj->keys = xmalloc(sizeof(char *) * cap);
    obj->values = xmalloc(sizeof(Value *) * cap);
    obj->count = 0;

    if (json_peek(p) == '}') {
        p->pos++;
        return 0;
    }

    for (;;) {
        char *key = json_parse_string(p);
        if (!key) goto fail;

        if (!json_consume(p, ':')) {
            free(key);
            goto fail;
        }

        Value *val = json_parse_value(p);
        if (!val) {
            free(key);
            goto fail;
        }

        if (obj->count >= cap) {
            cap *= 2;
            obj->keys = xrealloc(obj->keys, sizeof(char *) * cap);
            obj->values = xrealloc(obj->values, sizeof(Value *) * cap);
        }
        obj->keys[obj->count] = key;
        obj->values[obj->count] = val;
        obj->count++;

        if (json_peek(p) == ',') {
            p->pos++;
        } else {
            break;
        }
    }

    if (!json_consume(p, '}')) goto fail;
    return 0;

fail:
    for (size_t i = 0; i < obj->count; i++) {
        free(obj->keys[i]);
        value_free(obj->values[i]);
    }
    free(obj->keys);
    free(obj->values);
    obj->count = 0;
    return -1;
}

typedef struct {
    Value *table_val;
    int yielded;
} FromJsonState;

static Value *from_json_stage_next(PipelineStage *self) {
    FromJsonState *s = self->state;
    if (s->yielded || !s->table_val) return NULL;
    s->yielded = 1;
    return value_clone(s->table_val);
}

static void from_json_stage_free(PipelineStage *self) {
    FromJsonState *s = self->state;
    if (s->table_val) value_free(s->table_val);
    free(s);
}

static PipelineStage *create_from_json_stage(int input_fd) {
    int fd = (input_fd == STDIN_FILENO) ? dup(input_fd) : input_fd;
    if (fd < 0) {
        fprintf(stderr, "splash: from-json: %s\n", strerror(errno));
        return NULL;
    }
    FILE *f = fdopen(fd, "r");
    if (!f) {
        fprintf(stderr, "splash: from-json: %s\n", strerror(errno));
        close(fd);
        return NULL;
    }

    size_t len = 0;
    char *buf = read_all_from_file(f, &len);
    fclose(f);

    JsonParser parser = {buf, len, 0};

    // Expect a JSON array of objects
    if (!json_consume(&parser, '[')) {
        fprintf(stderr, "splash: from-json: expected JSON array\n");
        free(buf);
        FromJsonState *s = xmalloc(sizeof(FromJsonState));
        s->table_val = NULL;
        s->yielded = 0;
        return pipeline_stage_new(from_json_stage_next, from_json_stage_free,
                                  s, NULL);
    }

    // Parse all objects to collect column names and values
    size_t obj_cap = 16;
    size_t obj_count = 0;
    JsonObject *objects = xmalloc(sizeof(JsonObject) * obj_cap);

    // Collect all unique column names in order
    size_t col_cap = 16;
    size_t col_count = 0;
    char **col_names = xmalloc(sizeof(char *) * col_cap);

    if (json_peek(&parser) != ']') {
        for (;;) {
            if (obj_count >= obj_cap) {
                obj_cap *= 2;
                objects = xrealloc(objects, sizeof(JsonObject) * obj_cap);
            }
            if (json_parse_object(&parser, &objects[obj_count]) < 0) {
                fprintf(stderr, "splash: from-json: failed to parse object at position %zu\n",
                        parser.pos);
                break;
            }

            // Collect new column names
            for (size_t k = 0; k < objects[obj_count].count; k++) {
                int found = 0;
                for (size_t c = 0; c < col_count; c++) {
                    if (strcmp(col_names[c], objects[obj_count].keys[k]) == 0) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    if (col_count >= col_cap) {
                        col_cap *= 2;
                        col_names = xrealloc(col_names, sizeof(char *) * col_cap);
                    }
                    col_names[col_count++] = xstrdup(objects[obj_count].keys[k]);
                }
            }
            obj_count++;

            if (json_peek(&parser) == ',') {
                parser.pos++;
            } else {
                break;
            }
        }
    }

    // Build table
    Table *t = NULL;
    if (col_count > 0) {
        ValueType *types = xmalloc(sizeof(ValueType) * col_count);
        for (size_t i = 0; i < col_count; i++) types[i] = VALUE_STRING;
        t = table_new((const char **)col_names, types, col_count);
        free(types);

        // Add rows
        for (size_t r = 0; r < obj_count; r++) {
            Value **row = xmalloc(sizeof(Value *) * col_count);
            for (size_t c = 0; c < col_count; c++) {
                row[c] = NULL;
                for (size_t k = 0; k < objects[r].count; k++) {
                    if (strcmp(objects[r].keys[k], col_names[c]) == 0) {
                        row[c] = value_clone(objects[r].values[k]);
                        break;
                    }
                }
                if (!row[c]) row[c] = value_nil();
            }
            table_add_row(t, row, col_count);
            free(row);
        }

        // Update column types based on actual values
        for (size_t c = 0; c < t->col_count; c++) {
            int all_int = 1, all_float = 1, all_bool = 1;
            int has_data = 0;
            for (size_t r = 0; r < t->row_count; r++) {
                Value *v = table_get(t, r, c);
                if (!v || v->type == VALUE_NIL) continue;
                has_data = 1;
                if (v->type != VALUE_INT) all_int = 0;
                if (v->type != VALUE_FLOAT && v->type != VALUE_INT) all_float = 0;
                if (v->type != VALUE_BOOL) all_bool = 0;
            }
            if (has_data) {
                if (all_int) t->columns[c].type = VALUE_INT;
                else if (all_float) t->columns[c].type = VALUE_FLOAT;
                else if (all_bool) t->columns[c].type = VALUE_BOOL;
            }
        }
    }

    // Cleanup
    for (size_t r = 0; r < obj_count; r++) {
        for (size_t k = 0; k < objects[r].count; k++) {
            free(objects[r].keys[k]);
            value_free(objects[r].values[k]);
        }
        free(objects[r].keys);
        free(objects[r].values);
    }
    free(objects);
    for (size_t i = 0; i < col_count; i++) free(col_names[i]);
    free(col_names);
    free(buf);

    FromJsonState *s = xmalloc(sizeof(FromJsonState));
    s->table_val = t ? value_table(t) : NULL;
    s->yielded = 0;
    return pipeline_stage_new(from_json_stage_next, from_json_stage_free,
                              s, NULL);
}


// --- Structured to-csv serializer ---
// Pulls a table from upstream and serializes it as RFC 4180 CSV text.

typedef struct {
    Value *result;
    int yielded;
} ToCsvState;

// Check if a CSV field needs quoting (contains comma, quote, or newline).
static int csv_needs_quoting(const char *s) {
    for (const char *p = s; *p; p++) {
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') {
            return 1;
        }
    }
    return 0;
}

// Append a CSV field to a dynamic buffer, quoting if necessary.
// buf/len/cap are pointer-to so we can grow.
static void csv_append_field(char **buf, size_t *len, size_t *cap,
                             const char *field) {
    int need_quote = csv_needs_quoting(field);
    size_t flen = strlen(field);
    // Worst case: 2 quotes + every char doubled + null
    size_t needed = *len + flen * 2 + 3;
    if (needed > *cap) {
        while (needed > *cap) *cap *= 2;
        *buf = xrealloc(*buf, *cap);
    }

    char *p = *buf + *len;
    if (need_quote) {
        *p++ = '"';
        for (const char *s = field; *s; s++) {
            if (*s == '"') {
                *p++ = '"'; // escape quote as ""
            }
            *p++ = *s;
        }
        *p++ = '"';
    } else {
        memcpy(p, field, flen);
        p += flen;
    }
    *len = (size_t)(p - *buf);
}

static Value *to_csv_stage_next(PipelineStage *self) {
    ToCsvState *s = self->state;
    if (s->yielded) return NULL;
    s->yielded = 1;

    if (s->result) {
        return value_clone(s->result);
    }
    return NULL;
}

static void to_csv_stage_free(PipelineStage *self) {
    ToCsvState *s = self->state;
    if (s->result) value_free(s->result);
    free(s);
}

static PipelineStage *create_to_csv_stage(PipelineStage *upstream) {
    // Pull the table from upstream
    Value *v = upstream->next(upstream);
    if (!v || v->type != VALUE_TABLE) {
        // Non-table: pass through as-is
        ToCsvState *s = xmalloc(sizeof(ToCsvState));
        s->result = v; // may be NULL
        s->yielded = 0;
        return pipeline_stage_new(to_csv_stage_next, to_csv_stage_free,
                                  s, upstream);
    }

    Table *t = v->table;
    size_t ncols = table_col_count(t);
    size_t nrows = table_row_count(t);

    size_t cap = 4096;
    size_t len = 0;
    char *buf = xmalloc(cap);

    // Header row
    for (size_t c = 0; c < ncols; c++) {
        if (c > 0) {
            if (len + 1 >= cap) { cap *= 2; buf = xrealloc(buf, cap); }
            buf[len++] = ',';
        }
        csv_append_field(&buf, &len, &cap, t->columns[c].name);
    }
    if (len + 1 >= cap) { cap *= 2; buf = xrealloc(buf, cap); }
    buf[len++] = '\n';

    // Data rows
    for (size_t r = 0; r < nrows; r++) {
        for (size_t c = 0; c < ncols; c++) {
            if (c > 0) {
                if (len + 1 >= cap) { cap *= 2; buf = xrealloc(buf, cap); }
                buf[len++] = ',';
            }
            Value *cell = table_get(t, r, c);
            if (cell && cell->type != VALUE_NIL) {
                char *str = value_to_string(cell);
                csv_append_field(&buf, &len, &cap, str);
                free(str);
            }
            // NIL → empty field (nothing appended)
        }
        if (len + 1 >= cap) { cap *= 2; buf = xrealloc(buf, cap); }
        buf[len++] = '\n';
    }

    buf[len] = '\0';

    // Trim the trailing newline so drain doesn't add a double newline
    if (len > 0 && buf[len - 1] == '\n') {
        buf[--len] = '\0';
    }

    ToCsvState *s = xmalloc(sizeof(ToCsvState));
    s->result = value_string(buf);
    s->yielded = 0;
    free(buf);

    value_free(v);
    return pipeline_stage_new(to_csv_stage_next, to_csv_stage_free,
                              s, upstream);
}


// --- Structured to-json serializer ---
// Pulls a table from upstream and serializes it as a JSON array of objects.

typedef struct {
    Value *result;
    int yielded;
} ToJsonState;

// Append a JSON-escaped string (with surrounding quotes) to a dynamic buffer.
static void json_append_string(char **buf, size_t *len, size_t *cap,
                               const char *str) {
    // Worst case: every char becomes \uXXXX (6 chars) + 2 quotes
    size_t slen = strlen(str);
    size_t needed = *len + slen * 6 + 3;
    if (needed > *cap) {
        while (needed > *cap) *cap *= 2;
        *buf = xrealloc(*buf, *cap);
    }

    char *p = *buf + *len;
    *p++ = '"';
    for (const char *s = str; *s; s++) {
        switch (*s) {
        case '"':  *p++ = '\\'; *p++ = '"';  break;
        case '\\': *p++ = '\\'; *p++ = '\\'; break;
        case '\n': *p++ = '\\'; *p++ = 'n';  break;
        case '\r': *p++ = '\\'; *p++ = 'r';  break;
        case '\t': *p++ = '\\'; *p++ = 't';  break;
        case '\b': *p++ = '\\'; *p++ = 'b';  break;
        case '\f': *p++ = '\\'; *p++ = 'f';  break;
        default:
            if ((unsigned char)*s < 0x20) {
                // Control character → \u00XX
                p += snprintf(p, 7, "\\u%04x", (unsigned char)*s);
            } else {
                *p++ = *s;
            }
            break;
        }
    }
    *p++ = '"';
    *len = (size_t)(p - *buf);
}

// Append a raw string (no escaping) to the dynamic buffer.
static void json_append_raw(char **buf, size_t *len, size_t *cap,
                            const char *str) {
    size_t slen = strlen(str);
    size_t needed = *len + slen + 1;
    if (needed > *cap) {
        while (needed > *cap) *cap *= 2;
        *buf = xrealloc(*buf, *cap);
    }
    memcpy(*buf + *len, str, slen);
    *len += slen;
}

// Append a single JSON value based on its type.
static void json_append_value(char **buf, size_t *len, size_t *cap,
                              const Value *val) {
    if (!val || val->type == VALUE_NIL) {
        json_append_raw(buf, len, cap, "null");
        return;
    }
    switch (val->type) {
    case VALUE_STRING:
        json_append_string(buf, len, cap, val->string);
        break;
    case VALUE_INT: {
        char num[32];
        snprintf(num, sizeof(num), "%lld", (long long)val->integer);
        json_append_raw(buf, len, cap, num);
        break;
    }
    case VALUE_FLOAT: {
        char num[64];
        snprintf(num, sizeof(num), "%g", val->floating);
        json_append_raw(buf, len, cap, num);
        break;
    }
    case VALUE_BOOL:
        json_append_raw(buf, len, cap, val->boolean ? "true" : "false");
        break;
    default: {
        // TABLE, LIST, etc. → stringify
        char *s = value_to_string(val);
        json_append_string(buf, len, cap, s);
        free(s);
        break;
    }
    }
}

static Value *to_json_stage_next(PipelineStage *self) {
    ToJsonState *s = self->state;
    if (s->yielded) return NULL;
    s->yielded = 1;

    if (s->result) {
        return value_clone(s->result);
    }
    return NULL;
}

static void to_json_stage_free(PipelineStage *self) {
    ToJsonState *s = self->state;
    if (s->result) value_free(s->result);
    free(s);
}

static PipelineStage *create_to_json_stage(PipelineStage *upstream) {
    // Pull the table from upstream
    Value *v = upstream->next(upstream);
    if (!v || v->type != VALUE_TABLE) {
        ToJsonState *s = xmalloc(sizeof(ToJsonState));
        s->result = v;
        s->yielded = 0;
        return pipeline_stage_new(to_json_stage_next, to_json_stage_free,
                                  s, upstream);
    }

    Table *t = v->table;
    size_t ncols = table_col_count(t);
    size_t nrows = table_row_count(t);

    size_t cap = 4096;
    size_t len = 0;
    char *buf = xmalloc(cap);

    json_append_raw(&buf, &len, &cap, "[\n");

    for (size_t r = 0; r < nrows; r++) {
        json_append_raw(&buf, &len, &cap, "  {");
        for (size_t c = 0; c < ncols; c++) {
            if (c > 0) {
                json_append_raw(&buf, &len, &cap, ", ");
            }
            json_append_string(&buf, &len, &cap, t->columns[c].name);
            json_append_raw(&buf, &len, &cap, ": ");
            Value *cell = table_get(t, r, c);
            json_append_value(&buf, &len, &cap, cell);
        }
        if (r < nrows - 1) {
            json_append_raw(&buf, &len, &cap, "},\n");
        } else {
            json_append_raw(&buf, &len, &cap, "}\n");
        }
    }

    json_append_raw(&buf, &len, &cap, "]");

    // Null-terminate
    if (len >= cap) { cap = len + 1; buf = xrealloc(buf, cap); }
    buf[len] = '\0';

    ToJsonState *s = xmalloc(sizeof(ToJsonState));
    s->result = value_string(buf);
    s->yielded = 0;
    free(buf);

    value_free(v);
    return pipeline_stage_new(to_json_stage_next, to_json_stage_free,
                              s, upstream);
}


// --- Check if a command is a from-* text-to-structured source ---

static int is_from_source(const char *name) {
    return strcmp(name, "from-csv") == 0 ||
           strcmp(name, "from-json") == 0 ||
           strcmp(name, "from-lines") == 0;
}


int builtin_is_structured(const char *name) {
    return strcmp(name, "ls") == 0 ||
           strcmp(name, "ps") == 0 ||
           strcmp(name, "find") == 0 ||
           strcmp(name, "env") == 0 ||
           strcmp(name, "where") == 0 ||
           strcmp(name, "sort") == 0 ||
           strcmp(name, "select") == 0 ||
           strcmp(name, "first") == 0 ||
           strcmp(name, "last") == 0 ||
           strcmp(name, "count") == 0 ||
           strcmp(name, "from-csv") == 0 ||
           strcmp(name, "from-json") == 0 ||
           strcmp(name, "from-lines") == 0 ||
           strcmp(name, "to-csv") == 0 ||
           strcmp(name, "to-json") == 0;
}

int builtin_is_from_source(const char *name) {
    return is_from_source(name);
}

PipelineStage *builtin_create_stage(SimpleCommand *cmd,
                                    PipelineStage *upstream) {
    const char *name = cmd->argv[0];

    // Source builtins: ignore upstream (free it if non-NULL)
    if (strcmp(name, "ls") == 0) {
        pipeline_stage_free(upstream);
        return create_ls_stage(cmd);
    }
    if (strcmp(name, "ps") == 0) {
        pipeline_stage_free(upstream);
        return create_ps_stage();
    }
    if (strcmp(name, "find") == 0) {
        pipeline_stage_free(upstream);
        return create_find_stage(cmd);
    }
    if (strcmp(name, "env") == 0) {
        pipeline_stage_free(upstream);
        return create_env_stage();
    }

    // from-* sources: read from stdin by default
    if (is_from_source(name)) {
        pipeline_stage_free(upstream);
        return builtin_create_from_stage(cmd, STDIN_FILENO);
    }

    // Filter builtins: chain onto upstream
    if (strcmp(name, "where") == 0) {
        return create_where_stage(cmd, upstream);
    }
    if (strcmp(name, "sort") == 0) {
        return create_sort_stage(cmd, upstream);
    }
    if (strcmp(name, "select") == 0) {
        return create_select_stage(cmd, upstream);
    }
    if (strcmp(name, "first") == 0) {
        return create_first_last_stage(cmd, upstream, 0);
    }
    if (strcmp(name, "last") == 0) {
        return create_first_last_stage(cmd, upstream, 1);
    }
    if (strcmp(name, "count") == 0) {
        return create_count_stage(upstream);
    }
    if (strcmp(name, "to-csv") == 0) {
        return create_to_csv_stage(upstream);
    }
    if (strcmp(name, "to-json") == 0) {
        return create_to_json_stage(upstream);
    }

    pipeline_stage_free(upstream);
    return NULL;
}

PipelineStage *builtin_create_from_stage(SimpleCommand *cmd, int input_fd) {
    const char *name = cmd->argv[0];
    if (strcmp(name, "from-csv") == 0) {
        return create_from_csv_stage(input_fd);
    }
    if (strcmp(name, "from-json") == 0) {
        return create_from_json_stage(input_fd);
    }
    if (strcmp(name, "from-lines") == 0) {
        return create_from_lines_stage(input_fd);
    }
    close(input_fd);
    return NULL;
}
