#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "jobs.h"
#include "util.h"

static Job job_table[MAX_JOBS];
static pid_t shell_pgid;


void jobs_init(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        job_table[i].id = 0; // 0 means empty slot
    }
}

int jobs_add(pid_t pgid, pid_t *pids, int num_pids, const char *command,
             int background) {
    // Find first empty slot and determine next job ID
    int slot = -1;
    int max_id = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].id == 0 && slot == -1) {
            slot = i;
        }
        if (job_table[i].id > max_id) {
            max_id = job_table[i].id;
        }
    }

    if (slot == -1) {
        fprintf(stderr, "splash: too many jobs\n");
        return -1;
    }

    Job *j = &job_table[slot];
    j->id = max_id + 1;
    j->pgid = pgid;
    j->num_pids = num_pids;
    j->pids = xmalloc(sizeof(pid_t) * (size_t)num_pids);
    memcpy(j->pids, pids, sizeof(pid_t) * (size_t)num_pids);
    j->status = JOB_RUNNING;
    j->exit_status = 0;
    j->command = xstrdup(command);
    j->background = background;
    j->notified = 0;

    return j->id;
}

void jobs_remove(int job_id) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].id == job_id) {
            free(job_table[i].pids);
            free(job_table[i].command);
            job_table[i].id = 0;
            job_table[i].pids = NULL;
            job_table[i].command = NULL;
            return;
        }
    }
}

Job *jobs_find_by_pgid(pid_t pgid) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].id != 0 && job_table[i].pgid == pgid) {
            return &job_table[i];
        }
    }
    return NULL;
}

Job *jobs_find_by_id(int job_id) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].id == job_id) {
            return &job_table[i];
        }
    }
    return NULL;
}

Job *jobs_find_most_recent(void) {
    Job *best = NULL;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].id != 0) {
            if (!best || job_table[i].id > best->id) {
                best = &job_table[i];
            }
        }
    }
    return best;
}

// Check if all processes in a job have exited or been stopped.
// Updates job status and exit_status accordingly.
static void check_job_processes(Job *j) {
    int all_done = 1;
    int any_stopped = 0;
    int last_exit = 0;

    for (int p = 0; p < j->num_pids; p++) {
        int status;
        pid_t result = waitpid(j->pids[p], &status, WNOHANG | WUNTRACED);

        if (result == 0) {
            // Still running
            all_done = 0;
        } else if (result > 0) {
            if (WIFSTOPPED(status)) {
                any_stopped = 1;
                all_done = 0;
            } else if (WIFEXITED(status)) {
                if (p == j->num_pids - 1) {
                    last_exit = WEXITSTATUS(status);
                }
            } else if (WIFSIGNALED(status)) {
                if (p == j->num_pids - 1) {
                    last_exit = 128 + WTERMSIG(status);
                }
            }
        } else {
            // ECHILD — process already reaped or doesn't exist
            // Treat as done
        }
    }

    if (any_stopped) {
        j->status = JOB_STOPPED;
    } else if (all_done) {
        j->status = JOB_DONE;
        j->exit_status = last_exit;
    }
}

void jobs_update_status(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].id != 0 &&
            (job_table[i].status == JOB_RUNNING ||
             job_table[i].status == JOB_STOPPED)) {
            check_job_processes(&job_table[i]);
        }
    }
}

void jobs_notify(void) {
    jobs_update_status();

    for (int i = 0; i < MAX_JOBS; i++) {
        Job *j = &job_table[i];
        if (j->id == 0) {
            continue;
        }
        if (j->status == JOB_DONE && !j->notified) {
            printf("[%d] done    %s\n", j->id, j->command);
            j->notified = 1;
            jobs_remove(j->id);
        }
    }
}

void jobs_print(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        Job *j = &job_table[i];
        if (j->id == 0) {
            continue;
        }

        const char *status_str;
        switch (j->status) {
            case JOB_RUNNING: status_str = "running"; break;
            case JOB_STOPPED: status_str = "stopped"; break;
            case JOB_DONE:    status_str = "done";    break;
        }
        printf("[%d] %s\t%s\n", j->id, status_str, j->command);
    }
}

pid_t jobs_get_shell_pgid(void) {
    return shell_pgid;
}

void jobs_set_shell_pgid(pid_t pgid) {
    shell_pgid = pgid;
}
