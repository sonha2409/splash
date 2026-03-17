#ifndef SPLASH_JOBS_H
#define SPLASH_JOBS_H

#include <sys/types.h>

#define MAX_JOBS 64

typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE,
} JobStatus;

typedef struct {
    int id;              // Job number [1], [2], ...
    pid_t pgid;          // Process group ID (= first child's PID)
    pid_t *pids;         // All PIDs in the pipeline (owned)
    int num_pids;
    JobStatus status;
    int exit_status;     // Exit status of last process
    char *command;       // Original command string for display (owned)
    int background;      // Was launched as background job
    int notified;        // Has completion been reported to user
} Job;

// Initialize the job table. Call once at startup.
void jobs_init(void);

// Add a new job to the table. pids array and command string are copied.
// Returns the job ID, or -1 if the table is full.
int jobs_add(pid_t pgid, pid_t *pids, int num_pids, const char *command,
             int background);

// Remove a job from the table by job ID. Frees internal allocations.
void jobs_remove(int job_id);

// Find a job by its process group ID. Returns NULL if not found.
Job *jobs_find_by_pgid(pid_t pgid);

// Find a job by its job ID. Returns NULL if not found.
Job *jobs_find_by_id(int job_id);

// Find the most recent job (highest ID). Returns NULL if no jobs.
Job *jobs_find_most_recent(void);

// Update job statuses by reaping children with waitpid(WNOHANG).
// Called from the REPL loop (not from signal handlers).
void jobs_update_status(void);

// Print notifications for completed background jobs and remove them.
// Called before each prompt.
void jobs_notify(void);

// Print all jobs (for the `jobs` builtin).
void jobs_print(void);

// Return the shell's own process group ID.
pid_t jobs_get_shell_pgid(void);

// Set the shell's process group ID. Called once at startup.
void jobs_set_shell_pgid(pid_t pgid);

#endif // SPLASH_JOBS_H
