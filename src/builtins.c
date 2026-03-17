#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "builtins.h"
#include "jobs.h"

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

int builtin_is_builtin(const char *name) {
    return strcmp(name, "exit") == 0 ||
           strcmp(name, "cd") == 0 ||
           strcmp(name, "jobs") == 0 ||
           strcmp(name, "fg") == 0 ||
           strcmp(name, "bg") == 0;
}

int builtin_execute(SimpleCommand *cmd) {
    const char *name = cmd->argv[0];

    if (strcmp(name, "exit") == 0) return builtin_exit(cmd);
    if (strcmp(name, "cd") == 0)   return builtin_cd(cmd);
    if (strcmp(name, "jobs") == 0) return builtin_jobs();
    if (strcmp(name, "fg") == 0)   return builtin_fg(cmd);
    if (strcmp(name, "bg") == 0)   return builtin_bg(cmd);

    fprintf(stderr, "splash: %s: unknown builtin\n", name);
    return 1;
}
