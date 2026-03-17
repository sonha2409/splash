#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "alias.h"
#include "builtins.h"
#include "executor.h"
#include "history.h"
#include "jobs.h"
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

int builtin_is_structured(const char *name) {
    // Structured builtins will be added in 7.6+
    (void)name;
    return 0;
}

PipelineStage *builtin_create_stage(SimpleCommand *cmd,
                                    PipelineStage *upstream) {
    // Structured builtins will be added in 7.6+
    (void)cmd;
    pipeline_stage_free(upstream);
    return NULL;
}
