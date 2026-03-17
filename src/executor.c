#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "command.h"
#include "executor.h"
#include "util.h"

// Execute a single command (no pipes).
static int execute_single(SimpleCommand *cmd, int background) {
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "splash: fork: %s\n", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        // Child
        execvp(cmd->argv[0], cmd->argv);
        fprintf(stderr, "splash: %s: %s\n", cmd->argv[0], strerror(errno));
        _exit(127);
    }

    // Parent
    if (background) {
        printf("[%d]\n", pid);
        return 0;
    }

    int status;
    if (waitpid(pid, &status, 0) == -1) {
        fprintf(stderr, "splash: waitpid: %s\n", strerror(errno));
        return -1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return -1;
}

// Execute a multi-stage pipeline.
static int execute_pipeline(Pipeline *pl) {
    int n = pl->num_commands;

    // Allocate pipe file descriptors: (n-1) pipes, each with 2 fds
    int (*pipes)[2] = xmalloc(sizeof(int[2]) * (size_t)(n - 1));

    // Create all pipes
    for (int i = 0; i < n - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            fprintf(stderr, "splash: pipe: %s\n", strerror(errno));
            // Close any already-created pipes
            for (int j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            free(pipes);
            return -1;
        }
    }

    // Fork each command
    pid_t *pids = xmalloc(sizeof(pid_t) * (size_t)n);

    for (int i = 0; i < n; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            fprintf(stderr, "splash: fork: %s\n", strerror(errno));
            // Close all pipe fds
            for (int j = 0; j < n - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
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

            // Wire stdin from previous pipe (except first command)
            if (i > 0) {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) == -1) {
                    fprintf(stderr, "splash: dup2 stdin: %s\n",
                            strerror(errno));
                    _exit(1);
                }
            }

            // Wire stdout to next pipe (except last command)
            if (i < n - 1) {
                if (dup2(pipes[i][1], STDOUT_FILENO) == -1) {
                    fprintf(stderr, "splash: dup2 stdout: %s\n",
                            strerror(errno));
                    _exit(1);
                }
            }

            // Close all pipe fds in child
            for (int j = 0; j < n - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            execvp(pl->commands[i]->argv[0], pl->commands[i]->argv);
            fprintf(stderr, "splash: %s: %s\n",
                    pl->commands[i]->argv[0], strerror(errno));
            _exit(127);
        }
    }

    // Parent: close all pipe fds
    for (int i = 0; i < n - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // Wait for all children
    int last_status = 0;
    if (pl->background) {
        printf("[%d]\n", pids[n - 1]);
    } else {
        for (int i = 0; i < n; i++) {
            int status;
            if (waitpid(pids[i], &status, 0) == -1) {
                fprintf(stderr, "splash: waitpid: %s\n", strerror(errno));
                continue;
            }
            // Return status of last command
            if (i == n - 1) {
                if (WIFEXITED(status)) {
                    last_status = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    last_status = 128 + WTERMSIG(status);
                }
            }
        }
    }

    free(pipes);
    free(pids);
    return last_status;
}

int executor_execute(Pipeline *pl) {
    if (!pl || pl->num_commands == 0) {
        return 0;
    }

    if (pl->num_commands == 1) {
        return execute_single(pl->commands[0], pl->background);
    }

    return execute_pipeline(pl);
}
