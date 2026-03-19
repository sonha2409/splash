#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
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
#include "pipeline.h"
#include "signals.h"
#include "tokenizer.h"
#include "util.h"

#define ALIAS_MAX_EXPAND 16

// Forward declaration — defined below execute_structured_pipeline().
static int execute_pipeline_impl(Pipeline *pl, const char *command_str);

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

// Check if a pipeline contains any structured pipes (|>).
static int pipeline_has_structured(Pipeline *pl) {
    for (int i = 0; i < pl->num_commands - 1; i++) {
        if (pl->pipe_types[i] == PIPE_STRUCTURED) {
            return 1;
        }
    }
    return 0;
}

// Check if a command name is a structured filter (builtin that operates on |> data).
// Structured filters transform pipeline stages rather than producing text output.
static int is_structured_filter(const char *name) {
    return strcmp(name, "where") == 0 ||
           strcmp(name, "sort") == 0 ||
           strcmp(name, "select") == 0 ||
           strcmp(name, "first") == 0 ||
           strcmp(name, "last") == 0 ||
           strcmp(name, "count") == 0 ||
           strcmp(name, "to-csv") == 0 ||
           strcmp(name, "to-json") == 0;
}

// Execute a pipeline that contains |> structured pipe segments.
// Structured builtins produce PipelineStage values; when a structured segment
// ends before an external command, we serialize via pipeline_stage_drain_to_fd().
//
// A structured segment is a contiguous run of commands connected by |> where
// the first is a structured builtin/filter source and the rest are structured
// filters. When the segment ends (next pipe is | or end of pipeline), we either
// drain to stdout or serialize into a text pipe feeding the next external command.
static int execute_structured_pipeline(Pipeline *pl, const char *command_str) {
    // Walk the pipeline to find structured segments.
    // A structured segment starts at a command that is a structured builtin,
    // and continues through |> connections to structured filters.
    //
    // For now, if the first command in a |> chain is NOT a structured builtin,
    // we treat the |> as a plain text pipe (fallback). This allows graceful
    // degradation until structured builtins are added in 7.6+.

    // Find the first |> and check if its source is a structured builtin
    int struct_start = -1;
    for (int i = 0; i < pl->num_commands - 1; i++) {
        if (pl->pipe_types[i] == PIPE_STRUCTURED) {
            if (builtin_is_structured(pl->commands[i]->argv[0])) {
                struct_start = i;
            }
            break;
        }
    }

    // No structured source found — fall back to text pipeline.
    // Treat all |> as regular | pipes.
    if (struct_start < 0) {
        return execute_pipeline_impl(pl, command_str);
    }

    // Build the structured pipeline stage chain starting from struct_start.
    PipelineStage *stage = NULL;

    if (struct_start > 0 && builtin_is_from_source(pl->commands[struct_start]->argv[0])) {
        // from-* source preceded by text commands: run the text prefix as a
        // sub-pipeline, pipe its output into the from-* source.
        int text_pipe[2];
        if (pipe(text_pipe) == -1) {
            fprintf(stderr, "splash: pipe: %s\n", strerror(errno));
            return -1;
        }

        pid_t text_pid = fork();
        if (text_pid < 0) {
            fprintf(stderr, "splash: fork: %s\n", strerror(errno));
            close(text_pipe[0]);
            close(text_pipe[1]);
            return -1;
        }

        if (text_pid == 0) {
            // Child: run text prefix with stdout → text_pipe[1]
            close(text_pipe[0]);
            if (dup2(text_pipe[1], STDOUT_FILENO) == -1) {
                fprintf(stderr, "splash: dup2: %s\n", strerror(errno));
                close(text_pipe[1]);
                _exit(1);
            }
            close(text_pipe[1]);

            // Detach stdin from terminal so execute_pipeline_impl
            // does not attempt interactive terminal control (tcsetpgrp)
            // which would SIGTTOU this child since the parent shell
            // still owns the terminal.
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                close(devnull);
            }

            signals_default();

            // Build sub-pipeline from commands [0..struct_start-1]
            Pipeline *pre = pipeline_new();
            for (int i = 0; i < struct_start; i++) {
                pipeline_add_command(pre, pl->commands[i]);
                if (i < struct_start - 1) {
                    pipeline_add_pipe_type(pre, pl->pipe_types[i]);
                }
            }
            if (struct_start > 1) {
                for (int i = 0; i < struct_start - 1; i++) {
                    pre->pipe_types[i] = pl->pipe_types[i];
                }
            }

            int status = execute_pipeline_impl(pre, command_str);

            // Null out borrowed commands
            for (int i = 0; i < struct_start; i++) {
                pre->commands[i] = NULL;
            }
            pre->num_commands = 0;
            pipeline_free(pre);
            _exit(status < 0 ? 1 : status);
        }

        // Parent: close write end, create from-* stage reading from pipe
        close(text_pipe[1]);
        stage = builtin_create_from_stage(pl->commands[struct_start],
                                          text_pipe[0]);
        // Wait for text prefix child
        waitpid(text_pid, NULL, 0);

        if (!stage) {
            return -1;
        }
    } else {
        // For from-* sources at the start, apply redirections for stdin
        int saved_stdin = -1;
        SimpleCommand *src_cmd = pl->commands[struct_start];
        if (builtin_is_from_source(src_cmd->argv[0]) &&
            src_cmd->num_redirects > 0) {
            saved_stdin = dup(STDIN_FILENO);
            if (apply_redirections(src_cmd) == -1) {
                if (saved_stdin >= 0) close(saved_stdin);
                return -1;
            }
        }

        stage = builtin_create_stage(src_cmd, NULL);

        if (saved_stdin >= 0) {
            dup2(saved_stdin, STDIN_FILENO);
            close(saved_stdin);
        }

        if (!stage) {
            // Structured builtin failed to create stage — fall back
            return execute_pipeline_impl(pl, command_str);
        }
    }

    // Chain structured filters connected by |>
    int struct_end = struct_start; // inclusive end of structured segment
    for (int i = struct_start; i < pl->num_commands - 1; i++) {
        if (pl->pipe_types[i] != PIPE_STRUCTURED) {
            break;
        }
        int next = i + 1;
        SimpleCommand *next_cmd = pl->commands[next];
        if (builtin_is_structured(next_cmd->argv[0]) ||
            is_structured_filter(next_cmd->argv[0])) {
            stage = builtin_create_stage(next_cmd, stage);
            if (!stage) {
                return -1;
            }
            struct_end = next;
        } else {
            // Next command is external — structured segment ends here
            break;
        }
    }

    // Now we have a structured segment [struct_start..struct_end].
    // Determine what comes after:
    //   - Nothing (struct_end is the last command): drain to stdout
    //   - A text pipe into more commands: serialize into pipe, then run
    //     the remaining commands as a sub-pipeline

    if (struct_end == pl->num_commands - 1) {
        // Structured segment is the entire rest of the pipeline — drain to stdout
        pipeline_stage_drain(stage, stdout);
        return 0;
    }

    // Structured segment feeds into an external command.
    // Create a pipe, fork a child to serialize, and set up the remaining
    // commands with stdin reading from the pipe.
    int ser_pipe[2];
    if (pipe(ser_pipe) == -1) {
        fprintf(stderr, "splash: pipe: %s\n", strerror(errno));
        pipeline_stage_free(stage);
        return -1;
    }

    pid_t ser_pid = fork();
    if (ser_pid < 0) {
        fprintf(stderr, "splash: fork: %s\n", strerror(errno));
        close(ser_pipe[0]);
        close(ser_pipe[1]);
        pipeline_stage_free(stage);
        return -1;
    }

    if (ser_pid == 0) {
        // Serializer child: write structured data to pipe, then exit
        close(ser_pipe[0]);
        signals_default();
        pipeline_stage_drain_to_fd(stage, ser_pipe[1]); // closes ser_pipe[1]
        _exit(0);
    }

    // Parent: close write end, redirect read end as stdin for remaining pipeline
    close(ser_pipe[1]);
    pipeline_stage_free(stage);

    // Build a sub-pipeline from the remaining commands (struct_end+1 onward)
    int remaining = pl->num_commands - (struct_end + 1);
    Pipeline *sub = pipeline_new();
    for (int i = struct_end + 1; i < pl->num_commands; i++) {
        // Temporarily move commands — we'll put them back after execution
        pipeline_add_command(sub, pl->commands[i]);
        if (i < pl->num_commands - 1 && i > struct_end + 1) {
            pipeline_add_pipe_type(sub, pl->pipe_types[i]);
        }
    }
    // Add pipe types for the sub-pipeline
    if (remaining > 1) {
        // We already added the commands; now fix up pipe_types
        // The pipe types between sub-pipeline commands start at index struct_end+1
        for (int i = 0; i < remaining - 1; i++) {
            sub->pipe_types[i] = pl->pipe_types[struct_end + 1 + i];
        }
    }
    sub->background = pl->background;

    // We need stdin of the first sub-command to read from ser_pipe[0].
    // Save original stdin and redirect.
    int saved_stdin = dup(STDIN_FILENO);
    if (saved_stdin == -1) {
        fprintf(stderr, "splash: dup stdin: %s\n", strerror(errno));
        close(ser_pipe[0]);
        // Null out moved commands to prevent double-free
        for (int i = 0; i < remaining; i++) {
            sub->commands[i] = NULL;
        }
        sub->num_commands = 0;
        pipeline_free(sub);
        waitpid(ser_pid, NULL, 0);
        return -1;
    }

    if (dup2(ser_pipe[0], STDIN_FILENO) == -1) {
        fprintf(stderr, "splash: dup2 stdin: %s\n", strerror(errno));
        close(ser_pipe[0]);
        close(saved_stdin);
        for (int i = 0; i < remaining; i++) {
            sub->commands[i] = NULL;
        }
        sub->num_commands = 0;
        pipeline_free(sub);
        waitpid(ser_pid, NULL, 0);
        return -1;
    }
    close(ser_pipe[0]);

    int status = execute_pipeline_impl(sub, command_str);

    // Restore stdin
    dup2(saved_stdin, STDIN_FILENO);
    close(saved_stdin);

    // Null out the borrowed commands so sub pipeline doesn't free them
    for (int i = 0; i < remaining; i++) {
        sub->commands[i] = NULL;
    }
    sub->num_commands = 0;
    pipeline_free(sub);

    // Wait for serializer child
    waitpid(ser_pid, NULL, 0);

    return status;
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

            // Handle from-* sources in child process: read stdin, produce table
            if (builtin_is_from_source(pl->commands[i]->argv[0])) {
                PipelineStage *stage = builtin_create_from_stage(
                    pl->commands[i], STDIN_FILENO);
                if (stage) {
                    pipeline_stage_drain(stage, stdout);
                    fflush(stdout);
                }
                _exit(stage ? 0 : 1);
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

    // Single structured builtin: build stage and drain to stdout
    if (pl->num_commands == 1 && !pl->background &&
        builtin_is_structured(pl->commands[0]->argv[0])) {
        SimpleCommand *cmd = pl->commands[0];

        // For from-* sources, apply redirections so they can read from
        // redirected stdin (e.g., from-csv < file.csv).
        int saved_stdin = -1;
        int saved_stdout = -1;
        if (builtin_is_from_source(cmd->argv[0]) && cmd->num_redirects > 0) {
            saved_stdin = dup(STDIN_FILENO);
            saved_stdout = dup(STDOUT_FILENO);
            if (apply_redirections(cmd) == -1) {
                if (saved_stdin >= 0) close(saved_stdin);
                if (saved_stdout >= 0) close(saved_stdout);
                return 1;
            }
        }

        PipelineStage *stage = builtin_create_stage(cmd, NULL);
        if (stage) {
            pipeline_stage_drain(stage, stdout);
            // Restore stdin/stdout if we redirected
            if (saved_stdin >= 0) {
                dup2(saved_stdin, STDIN_FILENO);
                close(saved_stdin);
            }
            if (saved_stdout >= 0) {
                dup2(saved_stdout, STDOUT_FILENO);
                close(saved_stdout);
            }
            return 0;
        }
        if (saved_stdin >= 0) {
            dup2(saved_stdin, STDIN_FILENO);
            close(saved_stdin);
        }
        if (saved_stdout >= 0) {
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdout);
        }
        // Fall through to normal execution on failure
    }

    // Check for builtins (single command, non-pipeline only)
    if (pl->num_commands == 1 && !pl->background) {
        SimpleCommand *cmd = pl->commands[0];
        if (builtin_is_builtin(cmd->argv[0])) {
            return builtin_execute(cmd);
        }
    }

    // Check for structured pipes — route through structured executor
    if (pl->num_commands > 1 && pipeline_has_structured(pl)) {
        return execute_structured_pipeline(pl, command_str);
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

// Execute a single pipeline entry: expand globs, track $_, run.
static int execute_pipeline_entry(Pipeline *pl, const char *command_str) {
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

    return executor_execute(pl, command_str);
}

// Execute an if/elif/else/fi command.
// Evaluates each clause's condition; if exit code is 0, executes the body.
// Falls through to else_body if no clause matched.
static int execute_if_command(IfCommand *cmd, const char *command_str) {
    for (int i = 0; i < cmd->num_clauses; i++) {
        int cond_status = executor_execute_list(cmd->clauses[i].condition,
                                                command_str);
        if (cond_status == 0) {
            return executor_execute_list(cmd->clauses[i].body, command_str);
        }
    }
    if (cmd->else_body) {
        return executor_execute_list(cmd->else_body, command_str);
    }
    return 0;
}

// Execute a for/in/do/done loop.
// Expands globs on the word list, then iterates: sets the variable and
// re-evaluates the body source text for each word (so $var expansions work).
static int execute_for_command(ForCommand *cmd,
                               const char *command_str __attribute__((unused))) {
    int status = 0;
    for (int i = 0; i < cmd->num_words; i++) {
        // Expand globs on each word
        int count = 0;
        char **expanded = expand_glob(cmd->words[i], &count);
        if (expanded) {
            for (int j = 0; j < count; j++) {
                expand_glob_unescape(expanded[j]);
                setenv(cmd->var_name, expanded[j], 1);
                status = executor_execute_line(cmd->body_src);
                free(expanded[j]);
            }
            free(expanded);
        } else {
            char *word = xstrdup(cmd->words[i]);
            expand_glob_unescape(word);
            setenv(cmd->var_name, word, 1);
            status = executor_execute_line(cmd->body_src);
            free(word);
        }
    }
    return status;
}

// Execute a while/until loop.
// while: loop while condition exits 0.
// until: loop until condition exits 0 (i.e., loop while condition exits non-zero).
static int execute_while_command(WhileCommand *cmd,
                                 const char *command_str __attribute__((unused))) {
    int status = 0;
    for (;;) {
        int cond_status = executor_execute_line(cmd->cond_src);
        // while: continue if cond == 0; until: continue if cond != 0
        if (cmd->is_until ? (cond_status == 0) : (cond_status != 0)) {
            break;
        }
        status = executor_execute_line(cmd->body_src);
    }
    return status;
}

// Unescape glob sentinels (GLOB_STAR → *, GLOB_QUEST → ?) in a string.
// Returns a newly allocated string. Caller must free.
static char *unescape_glob_sentinels(const char *s) {
    char *out = xmalloc(strlen(s) + 1);
    char *p = out;
    for (; *s; s++) {
        if (*s == '\x01') {
            *p++ = '*';
        } else if (*s == '\x02') {
            *p++ = '?';
        } else {
            *p++ = *s;
        }
    }
    *p = '\0';
    return out;
}

// Execute a case/in/esac command.
// Matches the word against each clause's patterns using fnmatch().
// Executes the first matching clause's body.
// The tokenizer replaces unquoted * and ? with sentinel bytes (\x01, \x02)
// for glob expansion. We unescape these back to real characters before
// passing to fnmatch().
static int execute_case_command(CaseCommand *cmd,
                                const char *command_str __attribute__((unused))) {
    char *word = unescape_glob_sentinels(cmd->word);

    int status = 0;
    for (int i = 0; i < cmd->num_clauses; i++) {
        CaseClause *clause = &cmd->clauses[i];
        int matched = 0;
        for (int j = 0; j < clause->num_patterns; j++) {
            char *pat = unescape_glob_sentinels(clause->patterns[j]);
            if (fnmatch(pat, word, 0) == 0) {
                matched = 1;
            }
            free(pat);
            if (matched) {
                break;
            }
        }
        if (matched) {
            status = executor_execute_line(clause->body_src);
            break;
        }
    }

    free(word);
    return status;
}

// Execute a single node (pipeline or compound command).
static int execute_node(Node *node, const char *command_str) {
    switch (node->type) {
        case NODE_PIPELINE:
            return execute_pipeline_entry(node->pipeline, command_str);
        case NODE_IF:
            return execute_if_command(node->if_cmd, command_str);
        case NODE_FOR:
            return execute_for_command(node->for_cmd, command_str);
        case NODE_WHILE:
            return execute_while_command(node->while_cmd, command_str);
        case NODE_CASE:
            return execute_case_command(node->case_cmd, command_str);
    }
    return 0;
}

int executor_execute_list(CommandList *list, const char *command_str) {
    if (!list || list->num_entries == 0) {
        return 0;
    }

    int status = 0;
    for (int i = 0; i < list->num_entries; i++) {
        // Check operator before this entry (skip for first)
        if (i > 0) {
            ListOpType op = list->operators[i - 1];
            if (op == LIST_AND && status != 0) {
                continue;  // skip: && requires previous success
            }
            if (op == LIST_OR && status == 0) {
                continue;  // skip: || requires previous failure
            }
        }

        status = execute_node(&list->entries[i], command_str);
        expand_set_last_status(status);
    }

    return status;
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
    CommandList *list = parser_parse(tokens, effective);
    int status = 0;
    if (list) {
        status = executor_execute_list(list, effective);
        command_list_free(list);
    }
    token_list_free(tokens);
    free(expanded);
    return status;
}
