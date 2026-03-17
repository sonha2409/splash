#ifndef SPLASH_SIGNALS_H
#define SPLASH_SIGNALS_H

// Set up signal handlers for the shell process.
// Must be called once at startup, after the shell has its own process group.
// - Ignores SIGINT, SIGTSTP, SIGTTOU, SIGTTIN (shell must not be killed/stopped)
// - SIGCHLD is left at default (we reap in the REPL loop, not async)
void signals_init(void);

// Reset all signal handlers to their defaults.
// Called in child processes after fork(), before exec().
void signals_default(void);

#endif // SPLASH_SIGNALS_H
