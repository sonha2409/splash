#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "alias.h"
#include "builtins.h"
#include "command.h"
#include "executor.h"
#include "expand.h"
#include "jobs.h"
#include "parser.h"
#include "signals.h"
#include "tokenizer.h"
#include "util.h"

#define ALIAS_MAX_EXPAND 16

// Apply all redirections for a command. Called in child process before exec.
// Returns 0 on success, -1 on failure (with error message printed).
static int apply_redirections(SimpleCommand *cmd) {
    for (int i = 0; i < cmd->num_redirects; i++) {
        Redirection *r = &cmd->redirects[i];
        int fd = -1;
        int flags = 0;

        switch (r->type) {
            case REDIRECT_OUTPUT:
                flags = O_WRONLY | O_CREAT | O_TRUNC;
                break;
            case REDIRECT_APPEND:
                flags = O_WRONLY | O_CREAT | O_APPEND;
                break;
            case REDIRECT_INPUT:
                flags = O_RDONLY;
                break;
            case REDIRECT_ERR:
                flags = O_WRONLY | O_CREAT | O_TRUNC;
                break;
            case REDIRECT_OUT_ERR:
                flags = O_WRONLY | O_CREAT | O_TRUNC;
                break;
            case REDIRECT_APPEND_ERR:
                flags = O_WRONLY | O_CREAT | O_APPEND;
                break;
        }

        fd = open(r->target, flags, 0644);
        if (fd == -1) {
            fprintf(stderr, "splash: %s: %s\n", r->target, strerror(errno));
            return -1;
        }

        switch (r->type) {
            case REDIRECT_OUTPUT:
            case REDIRECT_APPEND:
                if (dup2(fd, STDOUT_FILENO) == -1) {
                    fprintf(stderr, "splash: redirect stdout to '%s': %s\n",
                            r->target, strerror(errno));
                    close(fd);
                    return -1;
                }
                break;
            case REDIRECT_INPUT:
                if (dup2(fd, STDIN_FILENO) == -1) {
                    fprintf(stderr, "splash: redirect stdin from '%s': %s\n",
                            r->target, strerror(errno));
                    close(fd);
                    return -1;
                }
                break;
            case REDIRECT_ERR:
                if (dup2(fd, STDERR_FILENO) == -1) {
                    fprintf(stderr, "splash: redirect stderr to '%s': %s\n",
                            r->target, strerror(errno));
                    close(fd);
                    return -1;
                }
                break;
            case REDIRECT_OUT_ERR:
            case REDIRECT_APPEND_ERR:
                if (dup2(fd, STDOUT_FILENO) == -1) {
                    fprintf(stderr, "splash: redirect stdout to '%s': %s\n",
                            r->target, strerror(errno));
                    close(fd);
                    return -1;
                }
                if (dup2(fd, STDERR_FILENO) == -1) {
                    fprintf(stderr, "splash: redirect stderr to '%s': %s\n",
                            r->target, strerror(errno));
                    close(fd);
                    return -1;
                }
                break;
        }

        close(fd);
    }
    return 0;
}

// Execute a pipeline (single or multi-stage).
// Sets up process groups, job table entries, and waits for foreground jobs.
static int execute_pipeline_impl(Pipeline *pl, const char *command_str) {
    int n = pl->num_commands;
    int interactive = isatty(STDIN_FILENO);
    int (*pipes)[2] = NULL;
    pid_t *pids = xmalloc(sizeof(pid_t) * (size_t)n);
    pid_t pgid = 0;

    // Create pipes for multi-stage pipelines
    if (n > 1) {
        pipes = xmalloc(sizeof(int[2]) * (size_t)(n - 1));
        for (int i = 0; i < n - 1; i++) {
            if (pipe(pipes[i]) == -1) {
                fprintf(stderr, "splash: pipe: %s\n", strerror(errno));
                for (int j = 0; j < i; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
                free(pipes);
                free(pids);
                return -1;
            }
        }
    }

    // Fork each command
    for (int i = 0; i < n; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            fprintf(stderr, "splash: fork: %s\n", strerror(errno));
            // Close all pipe fds
            if (pipes) {
                for (int j = 0; j < n - 1; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
            }
            // Wait for any already-forked children
            for (int j = 0; j < i; j++) {
                waitpid(pids[j], NULL, 0);
            }
            free(pipes);
            free(pids);
            return -1;
        }

        if (pids[i] == 0) {
            // Child process

            // Set process group: first child creates, others join
            if (interactive) {
                pid_t child_pgid = (i == 0) ? 0 : pgid;
                setpgid(0, child_pgid);
            }

            // Reset signals to default before exec
            signals_default();

            // Wire pipes for multi-stage
            if (pipes) {
                if (i > 0) {
                    if (dup2(pipes[i - 1][0], STDIN_FILENO) == -1) {
                        fprintf(stderr, "splash: dup2 stdin: %s\n",
                                strerror(errno));
                        _exit(1);
                    }
                }
                if (i < n - 1) {
                    if (dup2(pipes[i][1], STDOUT_FILENO) == -1) {
                        fprintf(stderr, "splash: dup2 stdout: %s\n",
                                strerror(errno));
                        _exit(1);
                    }
                }
                for (int j = 0; j < n - 1; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
            }

            // Apply per-command redirections
            if (apply_redirections(pl->commands[i]) == -1) {
                _exit(1);
            }

            execvp(pl->commands[i]->argv[0], pl->commands[i]->argv);
            fprintf(stderr, "splash: %s: %s\n",
                    pl->commands[i]->argv[0], strerror(errno));
            _exit(127);
        }

        // Parent: set process group (race-safe — both parent and child call setpgid)
        if (i == 0) {
            pgid = pids[0];
        }
        if (interactive) {
            setpgid(pids[i], pgid);
        }
    }

    // Parent: close all pipe fds
    if (pipes) {
        for (int i = 0; i < n - 1; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }
        free(pipes);
    }

    // Add to job table
    int job_id = jobs_add(pgid, pids, n, command_str, pl->background);

    if (pl->background) {
        printf("[%d] %d\n", job_id, pgid);
        expand_set_last_bg_pid(pgid);
        free(pids);
        return 0;
    }

    // Foreground: give the job the terminal and wait
    if (interactive) {
        tcsetpgrp(STDIN_FILENO, pgid);
    }

    int last_status = 0;
    for (int i = 0; i < n; i++) {
        int status;
        pid_t result;
        do {
            result = waitpid(pids[i], &status, WUNTRACED);
        } while (result == -1 && errno == EINTR);

        if (result > 0) {
            if (WIFSTOPPED(status)) {
                // Job was stopped (Ctrl-Z)
                Job *j = jobs_find_by_id(job_id);
                if (j) {
                    j->status = JOB_STOPPED;
                    fprintf(stderr, "\n[%d] stopped\t%s\n", j->id, j->command);
                }
                // Reclaim terminal
                if (interactive) {
                    tcsetpgrp(STDIN_FILENO, jobs_get_shell_pgid());
                }
                free(pids);
                return 128 + WSTOPSIG(status);
            }
            if (i == n - 1) {
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

    // Remove completed foreground job
    jobs_remove(job_id);

    free(pids);
    return last_status;
}

int executor_execute(Pipeline *pl, const char *command_str) {
    if (!pl || pl->num_commands == 0) {
        return 0;
    }

    // Check for builtins (single command, non-pipeline only)
    if (pl->num_commands == 1 && !pl->background) {
        SimpleCommand *cmd = pl->commands[0];
        if (builtin_is_builtin(cmd->argv[0])) {
            return builtin_execute(cmd);
        }
    }

    return execute_pipeline_impl(pl, command_str);
}

// Expand aliases in the input line. Returns a newly allocated string if
// expansion occurred, or NULL if no expansion was needed.
// Caller must free the returned string.
static char *expand_aliases(const char *line) {
    // Find the first word (skip leading whitespace)
    const char *start = line;
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    if (*start == '\0') {
        return NULL;
    }

    const char *end = start;
    while (*end && *end != ' ' && *end != '\t' && *end != '\n') {
        end++;
    }

    size_t word_len = (size_t)(end - start);
    char word[256];
    if (word_len >= sizeof(word)) {
        return NULL;
    }
    memcpy(word, start, word_len);
    word[word_len] = '\0';

    const char *replacement = alias_get(word);
    if (!replacement) {
        return NULL;
    }

    // Build expanded line: leading whitespace + replacement + rest of line
    size_t lead_len = (size_t)(start - line);
    size_t repl_len = strlen(replacement);
    size_t rest_len = strlen(end);
    char *expanded = xmalloc(lead_len + repl_len + rest_len + 1);
    memcpy(expanded, line, lead_len);
    memcpy(expanded + lead_len, replacement, repl_len);
    memcpy(expanded + lead_len + repl_len, end, rest_len);
    expanded[lead_len + repl_len + rest_len] = '\0';
    return expanded;
}

int executor_execute_line(const char *line) {
    if (!line || *line == '\0') {
        return 0;
    }

    // Expand aliases (with depth limit to prevent infinite loops)
    const char *effective = line;
    char *expanded = NULL;
    for (int depth = 0; depth < ALIAS_MAX_EXPAND; depth++) {
        char *next = expand_aliases(effective);
        if (!next) {
            break;
        }
        free(expanded);
        expanded = next;
        effective = expanded;
    }

    TokenList *tokens = tokenizer_tokenize(effective);
    Pipeline *pl = parser_parse(tokens);
    int status = 0;
    if (pl) {
        // Expand globs in all commands
        for (int ci = 0; ci < pl->num_commands; ci++) {
            expand_glob_argv(pl->commands[ci]);
        }

        // Track $_ (last argument of the command)
        if (pl->num_commands > 0) {
            SimpleCommand *last_cmd = pl->commands[pl->num_commands - 1];
            if (last_cmd->argc > 0) {
                expand_set_last_arg(last_cmd->argv[last_cmd->argc - 1]);
            }
        }
        status = executor_execute(pl, effective);
        expand_set_last_status(status);
        pipeline_free(pl);
    }
    token_list_free(tokens);
    free(expanded);
    return status;
}
